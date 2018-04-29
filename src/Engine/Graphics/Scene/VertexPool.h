#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"

namespace GS
{

class VertexPool
{
public:
	VertexPool();
	~VertexPool();

	bool prepareMeshes();
	bool setBuffers();


private:
	bool createBuffers( uint32_t vertexCount, uint32_t indexCount );
	
private:
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
};

}