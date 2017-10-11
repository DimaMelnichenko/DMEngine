#pragma once

#include "D3D/DMD3D.h"
#include <vector>

class DMSkyBox
{
public:
	DMSkyBox();
	~DMSkyBox();
	void Draw();

private:
	struct alignas( 16 ) Vertex
	{
		D3DXVECTOR3 position;
	};

	com_unique_ptr<ID3D11Buffer> m_vertex_buffer;
	com_unique_ptr<ID3D11Buffer> m_index_buffer;
};

