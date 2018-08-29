#pragma once

#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"

namespace GS
{

class DMGrass
{
public:
	DMGrass();
	~DMGrass();

	bool Initialize();
	void Render();

	void Populate();

	ID3D11ShaderResourceView* vertexBuffer();
	ID3D11Buffer* indirectArgsBuffer();

	void setInstanceParameters( uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset );

private:
	struct GrassVertex
	{
		XMFLOAT3 position;
		float size;
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
	uint64_t m_maxVertexNum = 1048576;
	com_unique_ptr<ID3D11Buffer> m_initArgsBuffer;
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_argsBuffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvVertex;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvArgs;
	com_unique_ptr<ID3D11UnorderedAccessView> m_uavVertex;
	com_unique_ptr<ID3D11UnorderedAccessView> m_uavArgs;
	DMComputeShader m_computeShader;
	DMComputeShader m_initShader;
};

}