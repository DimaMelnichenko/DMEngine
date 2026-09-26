#pragma once

#include <stdint.h>
#include "DirectX.h"
#include "D3D/DMD3D.h"

namespace GS
{

// Регулярная сетка width × height вершин в плоскости XZ с шагом 1 (вершина (i, 0, j)), индексы R32
class GridMesh
{
public:
	void initialize( uint16_t width, uint16_t height );
	ID3D11Buffer* vertexBuffer();
	ID3D11Buffer* indexBuffer();
	uint32_t indexCount();

private:
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;
};

}
