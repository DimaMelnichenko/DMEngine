#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

class GrassLod
{
public:
	GrassLod();
	GrassLod( const GrassLod& ) = delete;
	~GrassLod();

	
	void setInstanceParameters( DMComputeShader& shader, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );
	void populate( DMComputeShader& shader );
	bool createBuffers();

	const com_unique_ptr<ID3D11ShaderResourceView>& vertexBuffer();
	ID3D11Buffer* args();

public:
	struct PopulateParams
	{
		float nearBorder;
		float farBorder;
		float sizeMultipler;
		float density;
		float nearFallow;
		float farFallow;
		float dumb[2];
	} m_populateParams;

	PopulateParams& populateParams();

private:

	struct GrassInstanceItem
	{
		XMFLOAT3 position;
		float size;
		XMFLOAT4 rotation;
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