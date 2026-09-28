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
// indirect-аргументов
class ScatterPass
{
public:
	static constexpr uint32_t maxLods = 4;
	static constexpr uint32_t maxVariants = 8;
	static constexpr uint32_t maxLists = maxVariants * maxLods;
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
	// Список пары «вариант × LOD»
	static uint32_t listIndex( uint32_t variant, uint32_t lod ) { return variant * maxLods + lod; }
	// Начальные indirect-аргументы секции списка: её меш и ноль инстансов; в буфер их пишет resetArgs
	void setSectionArgs( uint32_t list, uint32_t section, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	// Аргументы всех секций перед расстановкой — одной записью в буфер
	void resetArgs();
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( DMComputeShader& shader, uint16_t gridDim );
	// После расстановки: число инстансов списка (оно в записи секции 0) — в записи остальных его секций; в D3D11
	// каждый indirect-вызов читает свою запись. Без многосекционных LOD ничего не делает
	void copySectionCounts( DMComputeShader& shader );

	// Инстансы списка для вершинного шейдера: участок буфера с начала списка (SV_InstanceID считается от нуля)
	const com_unique_ptr<ID3D11ShaderResourceView>& instances( uint32_t list );
	ID3D11Buffer* args();
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

	static constexpr uint32_t argsSize = 5 * sizeof( uint32_t );	// D3D11_DRAW_INDEXED_INSTANCED_INDIRECT_ARGS

	// cbuffer ScatterVariantsBuffer в Shaders\scatter.cs (b7)
	struct alignas( 16 ) VariantsBuffer
	{
		XMFLOAT4 variants[maxVariants];		// x — накопленная доля варианта (0…1), y — число LOD
		XMFLOAT4 lodEnd[maxVariants];		// дальности LOD 0…2 варианта, м
		uint32_t lists[maxLists][4];		// x — начало списка в буфере инстансов, y — его ёмкость, z — число секций
	};

	VariantsBuffer m_variants = {};
	com_unique_ptr<ID3D11Buffer> m_instanceBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_instanceUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_instanceSRVs[maxLists];
	com_unique_ptr<ID3D11Buffer> m_argsBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_argsUAV;
	// Начальные аргументы (меш секции, ноль инстансов): каждый кадр копируются в m_argsBuffer
	uint32_t m_initialArgs[maxLists * maxSections * 5] = {};
	bool m_hasSections = false;	// есть LOD из нескольких секций
	com_unique_ptr<ID3D11Buffer> m_populateParamsBuffer;
	com_unique_ptr<ID3D11Buffer> m_variantsBuffer;
};

}
