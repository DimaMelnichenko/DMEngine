#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

class ScatterPass
{
public:
	ScatterPass();
	ScatterPass( const ScatterPass& ) = delete;
	~ScatterPass();

	
	void setInstanceParameters( DMComputeShader& shader, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( DMComputeShader& shader, uint16_t gridDim );
	bool createBuffers();

	const com_unique_ptr<ID3D11ShaderResourceView>& structuredBuffer();
	ID3D11Buffer* args();

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
		uint32_t capacity;		// ёмкость буфера инстансов, заполняет createBuffers()
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

	struct ArgsBuffer
	{
		uint32_t indexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startIndexLocation;
		int32_t baseVertexLocation;
		uint32_t startInstanceLocation;
		XMFLOAT3 padding;
	};

	

	struct PopulateBuffersStruct
	{
		com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
		com_unique_ptr<ID3D11Buffer> m_argsBuffer;
		com_unique_ptr<ID3D11ShaderResourceView> m_srvVertex;
		com_unique_ptr<ID3D11ShaderResourceView> m_srvArgs;
		com_unique_ptr<ID3D11UnorderedAccessView> m_uavVertex;
		com_unique_ptr<ID3D11UnorderedAccessView> m_uavArgs;
	} m_populateBuffers;

	uint64_t m_maxVertexNum = 262144;

	com_unique_ptr<ID3D11Buffer> m_initArgsBuffer;
	com_unique_ptr<ID3D11Buffer> m_populateParamsBuffer;
};

}