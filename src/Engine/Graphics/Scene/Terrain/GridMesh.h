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
	const Buffer& vertexBuffer() const { return m_vertexBuffer; }
	const Buffer& indexBuffer() const { return m_indexBuffer; }
	uint32_t indexCount();

private:
	Buffer m_vertexBuffer;
	Buffer m_indexBuffer;
	uint32_t m_vertexCount = 0;
	uint32_t m_indexCount = 0;
};

}
