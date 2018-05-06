#include "VertexPool.h"
#include "System.h"

namespace GS
{

VertexPool::VertexPool()
{

}

VertexPool::~VertexPool()
{

}

bool VertexPool::prepareMeshes()
{
	std::vector<VertexData::PTNTB> vertexes;
	std::vector<uint32_t> indexes;

	// подготовка бкферов для вершин и индексов
	vertexes.resize( System::meshes().vertexCount() );
	indexes.resize( System::meshes().indexCount() );

	// переменные для сохранения смещения
	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;

	// перебираем все меши какие есть
	for( auto& pair : System::meshes() )
	{
		AbstractMesh* mesh = pair.second.get();
		char* vertexBuffer = mesh->getVertices();

		// копируем вершины в общий буфер
		memcpy( (char*)(&vertexes[0]) + vertexOffset * sizeof( VertexData::PTNTB ),
				vertexBuffer, 
				mesh->vertexCount() * sizeof( VertexData::PTNTB ) );

		// копируем индексы в общий буфер
		memcpy( (char*)(&indexes[0]) + indexOffset * sizeof( uint32_t ),
				&mesh->getIndices()[0], 
				mesh->indexCount() * sizeof(uint32_t) );

		// записываем в меш значение смещения его личных вершин в общем буфере
		mesh->setOffsets( vertexOffset, indexOffset );

		// сохраняем смещение
		vertexOffset += mesh->vertexCount();
		indexOffset += mesh->indexCount();
	}

	// создание буферов на видюхе с нашими общими буферами
	DMD3D::instance().createVertexBuffer( m_vertexBuffer, &vertexes[0], System::meshes().vertexCount() * sizeof( VertexData::PTNTB ) );
	DMD3D::instance().createIndexBuffer( m_indexBuffer, &indexes[0], System::meshes().indexCount() * sizeof( uint32_t ) );

	return true;
}

bool VertexPool::setBuffers()
{	
	uint32_t strides = sizeof( VertexData::PTNTB );
	uint32_t offset = 0;
	ID3D11Buffer* vertexBuffer = m_vertexBuffer.get();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &vertexBuffer, &strides, &offset );
	
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_indexBuffer.get(), DXGI_FORMAT_R32_UINT, 0 );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	return true;
}

}