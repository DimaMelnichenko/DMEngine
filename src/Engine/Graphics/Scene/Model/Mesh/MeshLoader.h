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
	std::unique_ptr<AbstractMesh>&& loadFromFile( const std::wstring& filename, uint32_t id )
	{
		std::ifstream fin;

		// Open the model file.
		fin.open( filename, std::fstream::in | std::fstream::binary );

		// If it could not open the file then exit.
		if( fin.fail() )
		{
			return false;
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

		MeshVertexStruct* vertex = nullptr;

		// Read in the vertex data.
		for( size_t i = 0; i < file_header.vertex_count; i++ )
		{
			MeshVertexStruct file_vertex;

			fin.read( reinterpret_cast<char*>( &file_vertex ), sizeof( MeshVertexStruct ) );

			vertices.push_back( file_vertex );
		}

		// Read in the index data.
		for( size_t i = 0; i < file_header.index_count; ++i )
		{
			unsigned long index;
			fin.read( reinterpret_cast<char*>( &index ), sizeof( uint32_t ) );
			indices[i] = index;
		}

		// Close the model file.
		fin.close();

		std::unique_ptr<AbstractMesh> mesh( new DMMesh<MeshVertexStruct>( id, std::move( indices ), std::move( vertices ) ) );

		return std::move( mesh );
	}
};

}