#pragma once

#include "DMSceneObject.h"

class DMGrass :	public DMSceneObject
{
public:
	DMGrass( DMD3D* );
	~DMGrass();

	bool Initialize( float patch_size );
	void Render();

	void Generate();
	void EndGenerate();

	unsigned int grassCount();

	ID3D11Buffer* vertexBuffer();

	float grassMapSize();

private:
	struct GrassVertex
	{
		D3DXVECTOR3 position;
	};

private:
	std::shared_ptr<ID3D11Buffer> m_vertex_buffer;
	unsigned int m_max_grass_count;
	float m_patch_width_size;
};

