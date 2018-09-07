#pragma once

#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"
#include "../DMModel.h"

namespace GS
{

class DMGrass
{
public:
	DMGrass();
	~DMGrass();

	bool Initialize( uint32_t modelId );
	void Render( uint16_t lod, uint16_t instanceSlot );
	uint16_t lodCount();

	void prerender();

	ID3D11ShaderResourceView* vertexBuffer( uint16_t lod );
	ID3D11Buffer* indirectArgsBuffer( uint16_t lod );

	const DMModel::LodBlock* lodBlock( uint16_t );

private:
	void Populate();
	void setInstanceParameters( uint16_t lod, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );

private:
	struct ModelBuffers
	{
		com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
		com_unique_ptr<ID3D11Buffer> m_argsBuffer;
		com_unique_ptr<ID3D11ShaderResourceView> m_srvVertex;
		com_unique_ptr<ID3D11ShaderResourceView> m_srvArgs;
		com_unique_ptr<ID3D11UnorderedAccessView> m_uavVertex;
		com_unique_ptr<ID3D11UnorderedAccessView> m_uavArgs;
	};

	struct GrassVertex
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
private:
	bool createBuffers( ModelBuffers& modelBuffer );

private:
	std::unordered_map<uint16_t, const DMModel::LodBlock*> m_modelLODs;
	uint64_t m_maxVertexNum = 4194304;
	com_unique_ptr<ID3D11Buffer> m_initArgsBuffer;
	

	std::unordered_map<uint16_t, ModelBuffers> m_LODBuffer;
	DMComputeShader m_computeShader;
	DMComputeShader m_initShader;
};

}