#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

// Буферы и параметры слоя расстановки. Экземпляр растения раскладывается один раз, а в какой список он попадёт,
// решает его LOD по расстоянию: у каждого LOD свой участок буфера инстансов и свои indirect-аргументы, поэтому при
// смене LOD у экземпляра меняется только меш — положение, поворот и размер те же
class ScatterPass
{
public:
	static constexpr uint32_t maxLods = 4;
	// Ёмкость буфера инстансов слоя; делится поровну между LOD
	static constexpr uint32_t capacity = 262144;

	ScatterPass();
	ScatterPass( const ScatterPass& ) = delete;
	~ScatterPass();

	bool createBuffers( uint32_t lodCount );
	// Indirect-аргументы LOD lod перед расстановкой: его меш и ноль инстансов
	void resetArgs( DMComputeShader& shader, uint32_t lod, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( DMComputeShader& shader, uint16_t gridDim );

	// Инстансы LOD lod для вершинного шейдера: участок буфера с начала его списка (SV_InstanceID считается от нуля)
	const com_unique_ptr<ID3D11ShaderResourceView>& instances( uint32_t lod );
	ID3D11Buffer* args();
	// Смещение аргументов LOD lod в буфере аргументов (DrawIndexedInstancedIndirect), байты
	static uint32_t argsOffset( uint32_t lod ) { return lod * 5 * sizeof( uint32_t ); }

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
		uint32_t lodCapacity;	// ёмкость списка одного LOD, заполняет createBuffers()
		float castShadow;		// 1 — слой отбрасывает тень солнца (Cast Shadow в UE)
		uint32_t lodCount;		// заполняет createBuffers()
		XMFLOAT2 padding;
		XMFLOAT4 lodEnd;		// дальность LOD 0…2, м (ModelProperties.range): дальше — следующий LOD
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

	com_unique_ptr<ID3D11Buffer> m_instanceBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_instanceUAV;
	com_unique_ptr<ID3D11ShaderResourceView> m_instanceSRVs[maxLods];
	com_unique_ptr<ID3D11Buffer> m_argsBuffer;
	com_unique_ptr<ID3D11UnorderedAccessView> m_argsUAV;

	com_unique_ptr<ID3D11Buffer> m_initArgsBuffer;
	com_unique_ptr<ID3D11Buffer> m_populateParamsBuffer;
};

}
