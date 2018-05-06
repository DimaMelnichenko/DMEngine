#pragma once
#include "DirectX.h"
#include "DMMesh.h"
#include <fstream>

namespace GS
{

class MeshLoader
{
public:
	MeshLoader();
	~MeshLoader();

	template<typename MeshVertexStruct>
	AbstractMesh* loadFromFile( const std::string& filename, uint32_t id )
	{
		std::ifstream fin;

		// Open the model file.
		fin.open( filename, std::fstream::in | std::fstream::binary );

		// If it could not open the file then exit.
		if( fin.fail() )
		{
			throw std::exception( "Can`t open file" );
		}

		struct FileHeader
		{
			unsigned int vertex_count;
			unsigned int index_count;
		} file_header;



		fin.read( reinterpret_cast<char*>( &file_header ), sizeof( FileHeader ) );

		uint32_t vertex_count = file_header.vertex_count;

		std::vector<MeshVertexStruct> vertices;
		vertices.reserve( vertex_count );

		uint32_t index_count = file_header.index_count;
		std::vector<uint32_t> indices;
		indices.reserve( index_count );

		struct MyFormatVertex
		{
			D3DXVECTOR3 position;			
			D3DXVECTOR3 normal;
			D3DXVECTOR2 texture;
			D3DXVECTOR3 tangent;
			D3DXVECTOR3 binormal;
		};

		// Read in the vertex data.
		for( size_t i = 0; i < file_header.vertex_count; i++ )
		{
			MyFormatVertex file_vertex;

			fin.read( reinterpret_cast<char*>( &file_vertex ), sizeof( MyFormatVertex ) );

			MeshVertexStruct innerVertex;
			innerVertex.position = file_vertex.position;
			innerVertex.normal = file_vertex.normal;
			innerVertex.texture = file_vertex.texture;
			innerVertex.tangent = file_vertex.tangent;
			innerVertex.binormal = file_vertex.binormal;

			vertices.push_back( innerVertex );
		}

		// Read in the index data.
		for( size_t i = 0; i < file_header.index_count; ++i )
		{
			unsigned long index;
			fin.read( reinterpret_cast<char*>( &index ), sizeof( uint32_t ) );
			indices.push_back( index );
		}

		// Close the model file.
		fin.close();

		return new DMMesh<MeshVertexStruct>( id, filename, std::move( indices ), std::move( vertices ) ) ;
	}
};

}