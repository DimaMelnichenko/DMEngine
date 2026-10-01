#include "VertexPool.h"
#include <DirectXPackedVector.h>
#include "System.h"

using namespace DirectX;

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
	std::vector<VertexData::WindHalf> wind;
	std::vector<uint32_t> indexes;

	// подготовка бкферов для вершин и индексов
	vertexes.resize( System::meshes().vertexCount() );
	wind.resize( System::meshes().vertexCount() );	// нули: у мешей без ветра дерева
	indexes.resize( System::meshes().indexCount() );

	// переменные для сохранения смещения
	uint32_t vertexOffset = 0;
	uint32_t indexOffset = 0;

	// перебираем все меши какие есть
	for( auto& pair : System::meshes() )
	{
		AbstractMesh* mesh = pair.second.get();
		char* structuredBuffer = mesh->getVertices();

		// копируем вершины в общий буфер
		memcpy( (char*)(&vertexes[0]) + vertexOffset * sizeof( VertexData::PTNTB ),
				structuredBuffer, 
				mesh->vertexCount() * sizeof( VertexData::PTNTB ) );
		if( mesh->wind().size() == mesh->vertexCount() )
		{
			for( size_t i = 0; i < mesh->wind().size(); ++i )
			{
				const VertexData::Wind& source = mesh->wind()[i];
				VertexData::WindHalf& target = wind[vertexOffset + i];
				const float* values[] = { &source.branch1.x, &source.branch2.x };
				for( int b = 0; b < 2; ++b )
				for( int c = 0; c < 4; ++c )
					( b ? target.branch2 : target.branch1 )[c] = PackedVector::XMConvertFloatToHalf( values[b][c] );
				target.weights[0] = PackedVector::XMConvertFloatToHalf( source.weights.x );
				target.weights[1] = PackedVector::XMConvertFloatToHalf( source.weights.y );
			}
		}

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
	DMD3D::instance().createVertexBuffer( m_windBuffer, &wind[0], System::meshes().vertexCount() * sizeof( VertexData::WindHalf ) );
	DMD3D::instance().createIndexBuffer( m_indexBuffer, &indexes[0], System::meshes().indexCount() * sizeof( uint32_t ) );
	DMD3D::instance().setName( m_vertexBuffer, "Vertex pool" );
	DMD3D::instance().setName( m_windBuffer, "Vertex pool wind" );
	DMD3D::instance().setName( m_indexBuffer, "Index pool" );

	return true;
}

bool VertexPool::setBuffers()
{	
	const uint32_t strides[] = { sizeof( VertexData::PTNTB ), sizeof( VertexData::WindHalf ) };
	const uint32_t offsets[] = { 0, 0 };
	const Buffer* buffers[] = { &m_vertexBuffer, &m_windBuffer };
	DMD3D& d3d = DMD3D::instance();
	d3d.setVertexBuffers( 2, buffers, strides, offsets );
	d3d.setIndexBuffer( m_indexBuffer, DXGI_FORMAT_R32_UINT );

	return true;
}

}