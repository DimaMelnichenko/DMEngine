#pragma once
#include "Utils\utilites.h"

class DMMesh
{
private:
	struct VertexPTN
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
	};

	struct VertexPTNTB
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
		D3DXVECTOR3 tangent;
		D3DXVECTOR3 binormal;
	};

public:
	DMMesh(uint32_t id);
	~DMMesh();

	uint32_t id();

	DMMesh( const DMMesh& mesh ) = delete;
	DMMesh& operator=( const DMMesh& mesh ) = delete;

	enum VertexCombination
	{
		V_PTN, V_PTNTB
	};

	bool loadMeshData( VertexCombination, WCHAR* );

	int vertexSize() const;
	uint32_t GetIndexCount() const;
	uint32_t GetVertexCount() const;
	char* getVertices() const;
	uint32_t* getIndices() const;

private:
	bool readFile( WCHAR* filename );

private:
	uint32_t m_id;
	uint32_t m_vertex_count;
	uint32_t m_index_count;

	VertexCombination m_vertex_combination;

	// переменные локальные
	uint32_t* m_indices;
	char* m_vertices;
};

