#pragma once
#include "DirectX.h"
#include "DMMesh.h"
#include <algorithm>
#include <fstream>

namespace GS
{

class MeshLoader
{
public:
	MeshLoader();
	~MeshLoader();

	template<typename MeshVertexStruct>
	AbstractMesh* loadFromFile( uint32_t id, const std::string& name, const std::string& filename )
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
			DirectX::XMFLOAT3 position;			
			DirectX::XMFLOAT3 normal;
			DirectX::XMFLOAT2 texture;
			DirectX::XMFLOAT3 tangent;
			DirectX::XMFLOAT3 binormal;
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

		// Необязательные блоки после индексов (Tools/import_gltf.py): 4 байта метки и данные на каждую вершину — WIND (ветер
		// дерева, VertexData::Wind), SKIN (кости и веса, VertexData::Skin)
		std::vector<VertexData::Wind> wind;
		std::vector<VertexData::Skin> skin;
		char marker[4] = {};
		while( fin.read( marker, sizeof( marker ) ) )
		{
			if( std::equal( marker, marker + 4, "WIND" ) )
			{
				wind.resize( vertex_count );
				fin.read( reinterpret_cast<char*>( wind.data() ), wind.size() * sizeof( VertexData::Wind ) );
			}
			else if( std::equal( marker, marker + 4, "SKIN" ) )
			{
				skin.resize( vertex_count );
				fin.read( reinterpret_cast<char*>( skin.data() ), skin.size() * sizeof( VertexData::Skin ) );
			}
			else
				throw std::exception( "Unknown block after the indices" );
			if( !fin )
				throw std::exception( "A block is shorter than the vertex count" );
		}

		// Close the model file.
		fin.close();

		AbstractMesh* mesh = new DMMesh<MeshVertexStruct>( id, name, std::move( indices ), std::move( vertices ) );
		mesh->setWind( std::move( wind ) );
		mesh->setSkin( std::move( skin ) );
		return mesh;
	}
};

}