#pragma once
#include "..\DMSceneObject.h"
#include "Utils\utilites.h"

class DMMesh : public DMSceneObject
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
	DMMesh();
	DMMesh( DMMesh&& mesh );
	~DMMesh();

	DMMesh& operator=( const DMMesh& mesh );

	enum VertexCombination
	{
		V_PTN, V_PTNTB
	};

	void Render();
	bool loadMesh( VertexCombination, WCHAR* );
	int vertexSize();
	ID3D11Buffer* vertexBuffer();
	unsigned long GetIndexCount();

private:
	bool InitializeBuffers( WCHAR* );
	void RenderBuffers();
	bool readFile( WCHAR* filename );

private:
	// переменные общие для копий	
	com_shared_ptr<ID3D11Buffer> m_vertex_buffer;
	com_shared_ptr<ID3D11Buffer> m_index_buffer;
	unsigned long m_vertex_count;
	unsigned long m_index_count;

	VertexCombination m_vertex_combination;

	// переменные локальные
	unsigned long* m_indices;
	char* m_vertices;

};

