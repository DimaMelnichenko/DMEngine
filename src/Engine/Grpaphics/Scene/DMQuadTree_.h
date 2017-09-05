#pragma once

#include <d3d11.h>
#include <d3dx10math.h>
#include "DMObject.h"

class DMQuadTree_ : public DMObject
{
private:
	struct Node
	{
		Node* parent;
		Node* child[4];
		Node* neighbor[4];
		D3DXVECTOR3 center;
		D3DXVECTOR3	outer_points[4];
	};

	struct VertexType
	{
		D3DXVECTOR3 pos;
		D3DXVECTOR3 color;
	};

public:
	DMQuadTree_(  );
	~DMQuadTree_();

	bool Initialize( ID3D11Device* );	
	void Render();
	void Shutdown();

private:
	bool InitNode( Node* _parent, float _pos_x, float _pos_y, float _width );
	bool Update();
	bool InitializeBuffers( ID3D11Device* );

private:
	Node* m_main_node;
	ID3D11Buffer* m_vertex_buffer;
	ID3D11Buffer* m_index_buffer;	
	DWORD m_patch_vetex_count;
};

