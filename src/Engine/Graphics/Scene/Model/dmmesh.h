#pragma once
#include "Utils\utilites.h"
#include "DMResource.h"
#include "MeshVertexData.h"

namespace GS
{

class DMMesh : public DMResource
{
public:
	DMMesh( uint32_t id );
	~DMMesh();

	DMMesh( const DMMesh& mesh ) = delete;
	DMMesh& operator=( const DMMesh& mesh ) = delete;

	bool loadMeshData( MeshVertexData::Type, WCHAR* );

	MeshVertexData::Type vertexType() const;
	uint32_t GetIndexCount() const;
	uint32_t GetVertexCount() const;
	char* getVertices() const;
	uint32_t* getIndices() const;

private:
	bool readFile( WCHAR* filename );

private:
	uint32_t m_vertex_count;
	uint32_t m_index_count;

	MeshVertexData::Type m_vertex_combination;

	// переменные локальные
	uint32_t* m_indices;
	char* m_vertices;
};

}

