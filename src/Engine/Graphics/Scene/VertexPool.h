#pragma once
#include "DirectX.h"
#include "Utils\utilites.h"

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
	com_unique_ptr<ID3D11Buffer> m_vertexBuffer;
	com_unique_ptr<ID3D11Buffer> m_windBuffer;
	com_unique_ptr<ID3D11Buffer> m_indexBuffer;
};

}