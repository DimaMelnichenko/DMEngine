#pragma once
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

// Буферы и параметры слоя расстановки. Экземпляр растения раскладывается один раз: его вариант (одна из моделей слоя,
// по весам) и LOD по расстоянию решают, в какой список он попадёт. У каждой пары «вариант × LOD» свой участок буфера
// инстансов и свои indirect-аргументы, поэтому при смене LOD у экземпляра меняется только меш — положение, поворот и
// размер те же
class ScatterPass
{
public:
	static constexpr uint32_t maxLods = 4;
	static constexpr uint32_t maxVariants = 8;
	static constexpr uint32_t maxLists = maxVariants * maxLods;
	// Ёмкость буфера инстансов слоя; делится между списками по ожидаемому числу инстансов
	static constexpr uint32_t capacity = 262144;

	// Модель слоя для раскладки: доля по весу и дальности LOD (ModelProperties.range; последний — до конца кольца)
	struct Variant
	{
		float weight = 1.0f;
		uint32_t lodCount = 1;
		float lodEnd[maxLods] = {};
	};

	ScatterPass();
	ScatterPass( const ScatterPass& ) = delete;
	~ScatterPass();

	// Параметры слоя (populateParams) уже заданы: по кольцу и дальностям LOD делится ёмкость
	bool createBuffers( const std::vector<Variant>& variants );
	// Список пары «вариант × LOD»
	static uint32_t listIndex( uint32_t variant, uint32_t lod ) { return variant * maxLods + lod; }
	// Indirect-аргументы списка перед расстановкой: меш LOD и ноль инстансов
	void resetArgs( DMComputeShader& shader, uint32_t list, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( DMComputeShader& shader, uint16_t gridDim );

	// Инстансы списка для вершинного шейдера: участок буфера с начала списка (SV_InstanceID считается от нуля)
	const com_unique_ptr<ID3D11ShaderResourceView>& instances( uint32_t list );
	ID3D11Buffer* args();
	// Смещение аргументов списка в буфере аргументов (DrawIndexedInstancedIndirect), байты
	static uint32_t argsOffset( uint32_t list ) { return list * 5 * sizeof( uint32_t ); }

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

	// cbuffer ArgsBuffer в Shaders\scatter.cs
	struct alignas( 16 ) ArgsBuffer
	{
		uint32_t indexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startIndexLocation;
		int32_t baseVertexLocation;
		uint32_t startInstanceLocation;
		uint32_t argsOffset;	// куда записать аргументы, байты
	};

	// cbuffer ScatterVariantsBuffer в Shaders\scatter.cs (b7)
	struct alignas( 16 ) VariantsBuffer
	{
		XMFLOAT4 variants[maxVariants];		// x — накопленная доля варианта (0…1), y — число LOD
		XMFLOAT4 lodEnd[maxVariants];		// дальности LOD 0…2 варианта, м
		uint32_t lists[maxLists][4];		// x — начало списка в буфере инстансов, y — его ёмкость
	};

	VariantsBuffer m_variants = {};
	com_unique_ptr<ID3D11Buffer> m_instanceBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_instanceUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_instanceSRVs[maxLists];
	com_unique_ptr<ID3D11Buffer> m_argsBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_argsUAV;

	com_unique_ptr<ID3D11Buffer> m_initArgsBuffer;
	com_unique_ptr<ID3D11Buffer> m_populateParamsBuffer;
	com_unique_ptr<ID3D11Buffer> m_variantsBuffer;
};

}
