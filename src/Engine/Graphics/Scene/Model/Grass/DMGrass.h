#pragma once

#include "DirectX.h"
#include "Utils\utilites.h"

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
	ID3D11ShaderResourceView* indirectArgsBuffer();

private:
	struct GrassVertex
	{
		XMFLOAT3 position;
		float size;
	};

	struct ArgsBuffer
	{
		uint32_t vertexCountPerInstance;
		uint32_t instanceCount;
		uint32_t startVertexLocation;
		uint32_t startInstanceLocation;
	};

private:
	uint64_t m_maxVertexNum = 100000;
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indirectBuffer;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvVertex;
	com_unique_ptr<ID3D11ShaderResourceView> m_srvArgs;
	com_unique_ptr<ID3D11UnorderedAccessView>m_uavVertex;
};

}