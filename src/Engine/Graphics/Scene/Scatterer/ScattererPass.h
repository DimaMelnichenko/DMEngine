#pragma once
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

// Буферы и параметры слоя расстановки. Экземпляр растения раскладывается один раз: его вариант (одна из моделей слоя,
// по весам) и LOD по расстоянию решают, в какой список он попадёт. У каждой пары «вариант × LOD» свой участок буфера
// инстансов, поэтому при смене LOD у экземпляра меняется только меш — положение, поворот и размер те же. Секции LOD
// (меши со своими материалами: стебель и лепестки) рисуют один список инстансов, у каждой — своя запись
// indirect-аргументов. Экземпляры в полосе смены LOD (Dithered LOD Transition в UE, Shaders/lod_transition.h) лежат
// в списках перехода — у пары свой, в отдельном буфере с долей перехода: такой экземпляр есть в списках перехода
// обоих LOD, и каждый рисует свою долю пикселей
class ScatterPass
{
public:
	static constexpr uint32_t maxLods = 4;
	static constexpr uint32_t maxVariants = 8;
	static constexpr uint32_t pairCount = maxVariants * maxLods;	// пар «вариант × LOD»
	static constexpr uint32_t maxLists = pairCount * 2;				// обычные списки и списки перехода; MAX_LISTS в Shaders\scatter.cs
	static constexpr uint32_t maxSections = 4;	// MAX_SECTIONS в Shaders\scatter.cs
	// Ёмкость буфера инстансов слоя; делится между списками по ожидаемому числу инстансов
	static constexpr uint32_t capacity = 262144;

	// Модель слоя для раскладки: доля по весу и дальности LOD (ModelProperties.range; последний — до конца кольца)
	struct Variant
	{
		float weight = 1.0f;
		uint32_t lodCount = 1;
		float lodEnd[maxLods] = {};
		uint32_t sectionCount[maxLods] = { 1, 1, 1, 1 };	// секций у LOD, не больше maxSections
	};

	ScatterPass();
	ScatterPass( const ScatterPass& ) = delete;
	~ScatterPass();

	// Параметры слоя (populateParams) уже заданы: по кольцу и дальностям LOD делится ёмкость
	bool createBuffers( const std::vector<Variant>& variants );
	// Список пары «вариант × LOD»: обычный или перехода (экземпляры в полосе смены LOD)
	static uint32_t listIndex( uint32_t variant, uint32_t lod, bool transition = false )
	{
		return ( transition ? pairCount : 0 ) + variant * maxLods + lod;
	}
	// Смена LOD дизерингом у варианта (у всех его секций материал с DitheredLODTransition): без неё списки перехода
	// пусты и LOD сменяется мгновенно на дальности экземпляра. Буфер вариантов обновляется, когда флаг меняется
	void setDitheredLodTransition( uint32_t variant, bool dithered );
	// Начальные indirect-аргументы секции списка: её меш и ноль инстансов; в буфер их копирует resetArgs
	void setSectionArgs( uint32_t list, uint32_t section, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	// Аргументы всех секций перед расстановкой: копия GPU-буфера начальных аргументов в буфер аргументов (в D3D12 —
	// CopyBufferRegion + барьер INDIRECT_ARGUMENT); сам буфер начальных аргументов обновляется, только когда они менялись
	void resetArgs();
	// Объявление прохода раскладки (GpuPass.h): что слой пишет — аргументы и инстансы; что читает, добавляет Scatterer
	PassDesc passDesc( const char* name ) const;
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( DMComputeShader& shader, uint16_t gridDim );
	// После расстановки: число инстансов списка (оно в записи секции 0) — в записи остальных его секций; в D3D11
	// каждый indirect-вызов читает свою запись. Без многосекционных LOD ничего не делает
	void copySectionCounts( DMComputeShader& shader );

	// Инстансы списка для вершинного шейдера: участок буфера с начала списка (SV_InstanceID считается от нуля); у списка
	// перехода — буфер перехода (InstanceParam с LOD_DITHER в Shaders\instance.sh)
	const ShaderView& instances( uint32_t list );
	const Buffer& args();
	// Смещение аргументов секции списка в буфере аргументов (DrawIndexedInstancedIndirect), байты
	static uint32_t argsOffset( uint32_t list, uint32_t section ) { return ( list * maxSections + section ) * argsSize; }

public:
	// Параметры слоя для Shaders\scatter.cs (cbuffer ScatterLayerBuffer, b4)
	struct alignas( 16 ) PopulateParams
	{
		float nearBorder;		// кольцо вокруг камеры, метры
		float farBorder;
		float nearFade;			// ширина плавного исчезания у ближней и дальней границы
		float farFade;
		float sizeMultiplier;
		float cellSize;			// шаг сетки, метры
		float jitter;			// смещение внутри ячейки, доля шага
		float alignToTerrain;	// 1 — ось Y инстанса по нормали террейна
		XMFLOAT3 rotationRange;	// предел случайного поворота вокруг осей X, Y, Z, радианы
		float castShadow;		// 1 — слой отбрасывает тень солнца (Cast Shadow в UE)
		uint32_t variantCount;	// заполняет createBuffers()
		XMFLOAT3 padding;
	} m_populateParams;

	PopulateParams& populateParams();

private:

	// Совпадает с InstanceParam в Shaders\instance.sh при INST_POS, INST_SCALE и INST_ROTATE
	struct ScatterItem
	{
		XMFLOAT3 position;
		float size;
		XMFLOAT4 rotation;	// кватернион
	};

	// Экземпляр списка перехода: то же и доля смены LOD — InstanceParam с LOD_DITHER
	struct ScatterTransitionItem
	{
		ScatterItem item;
		float lodDither;	// (0; 1) — уходящий LOD, (−1; 0) — приходящий (Shaders\lod_dither.sh)
		XMFLOAT3 padding;
	};

	static constexpr uint32_t argsSize = 5 * sizeof( uint32_t );	// аргументы DrawIndexedInstancedIndirect

	// cbuffer ScatterVariantsBuffer в Shaders\scatter.cs (b7)
	struct alignas( 16 ) VariantsBuffer
	{
		// x — накопленная доля варианта (0…1), y — число LOD, z — 1: смена LOD дизерингом
		XMFLOAT4 variants[maxVariants];
		XMFLOAT4 lodEnd[maxVariants];		// дальности LOD 0…2 варианта, м
		// x — начало списка в его буфере инстансов (обычном или перехода), y — ёмкость, z — число секций
		uint32_t lists[maxLists][4];
	};

	// Буфер инстансов с видом на участок каждого списка и UAV для расстановки
	struct InstanceBuffer
	{
		Buffer buffer;
		StorageView uav;
	};
	bool createInstanceBuffer( InstanceBuffer& buffer, uint32_t stride, uint32_t count );

	VariantsBuffer m_variants = {};
	bool m_variantsChanged = false;
	InstanceBuffer m_instances;
	InstanceBuffer m_transitions;
	ShaderView m_instanceSRVs[maxLists];
	Buffer m_argsBuffer;
	StorageView m_argsUAV;
	// Начальные аргументы (меш секции, ноль инстансов): копия на CPU и в GPU-буфере, откуда каждый кадр копируются
	// в m_argsBuffer
	uint32_t m_initialArgs[maxLists * maxSections * 5] = {};
	Buffer m_initialArgsBuffer;
	bool m_initialArgsChanged = true;
	bool m_hasSections = false;	// есть LOD из нескольких секций
	Buffer m_populateParamsBuffer;
	Buffer m_variantsBuffer;
};

}
