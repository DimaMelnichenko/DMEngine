#pragma once


#include "Terrain\TerrainClass.h"
#include "Terrain\terrainshaderclass.h"

/////////////
// GLOBALS //
/////////////
const int MAX_TRIANGLES = 10000;


class QuadTreeClass : public DMSceneObject
{
private:
	struct VertexType
	{
		D3DXVECTOR3 position;
		D3DXVECTOR2 texture;
		D3DXVECTOR3 normal;
	};

	struct NodeType
	{
		float positionX, positionZ, width;
		int triangleCount;
		ID3D11Buffer *vertexBuffer, *indexBuffer;
		NodeType* nodes[4];
	};

public:
	QuadTreeClass( DMD3D* parent );
	~QuadTreeClass();

	bool Initialize( TerrainClass* );
	void Shutdown( );
	void Render( std::unique_ptr<DMFrustum>&, TerrainShaderClass* );

	int GetDrawCount( );

private:
	void CalculateMeshDimensions( int, float&, float&, float& );
	void CreateTreeNode( NodeType*, float, float, float );
	int CountTriangles( float, float, float );
	bool IsTriangleContained( int, float, float, float );
	void ReleaseNode( NodeType* );
	void RenderNode( NodeType*, std::unique_ptr<DMFrustum>&, TerrainShaderClass* );

private:
	int m_triangleCount, m_drawCount;
	VertexType* m_vertexList;
	NodeType* m_parentNode;
};

