#pragma once

#include <map>
#include <vector>
#include <memory>
#include <unordered_map>

#include "DirectX.h"

#include "Terrain\DMTerrain.h"
#include "Terrain\DMTerrainShader.h"
#include "Camera\DMFrustum.h"
#include "..\..\..\Utils\DMTimer.h"

#include "DMSceneObject.h"
#include "..\Common\DMAABB.h"
#include "Shaders\DMColorShader.h"


class DMQuadTree
{
public:

	struct NodeType
	{
		DMAABB aabb;
		NodeType* parent;
		std::vector<std::unique_ptr<NodeType>> child;
		float size;		
		std::vector<DMSceneObject*> objects;
	};

public:
	DMQuadTree();
	~DMQuadTree();

	bool Initialize( float side_length, float min_length );
	void checkObjects( const DMFrustum& frustum, const DMCamera& );
	void addObject( DMSceneObject* );
	void setDrawBuffers();
	void Render( DMColorShader*, const DMCamera& );
	float sideLength();
	float minLength();
	
	std::vector<DMSceneObject*> m_visible_objects;

private:
	void addNodes( NodeType*, DMSceneObject* );
	void drawNode( NodeType*, DMColorShader*, const DMCamera& );
	void checkNode( NodeType*, const DMFrustum&, const DMCamera& );

	__declspec( align( 16 ) ) struct Vertex
	{
		D3DXVECTOR3 pos;
		D3DXVECTOR3 color;
	};

private:	
	std::unique_ptr<NodeType> m_main_node;
	float m_side_length, m_min_length;
	com_unique_ptr<ID3D11Buffer> m_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_index_buffer;
	const unsigned long m_vertex_count = 8;
	const unsigned long m_index_count = 24;	
};

