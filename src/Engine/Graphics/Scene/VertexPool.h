#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"
#include "D3D\GpuResources.h"

namespace GS
{

// Общие буферы вершин и индексов всех мешей. Два потока вершин: основной (VertexData::PTNTB, слот 0) и данные ветра
// дерева (VertexData::WindHalf — VertexData::Wind в половинной точности, слот 1; у мешей без них — нули): второй читают
// только шейдеры с ветром дерева
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
	Buffer m_vertexBuffer;
	Buffer m_windBuffer;
	Buffer m_indexBuffer;
};

}
