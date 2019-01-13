#pragma once
#include "DirectX.h"
#include "Path.h"
#include "Scene/Shaders/DMShader.h"


class Terrain
{
public:
	Terrain();
	~Terrain();

	bool initialize();

	void render();

	struct Param
	{
		XMFLOAT4 tessFactors;
		XMFLOAT4 modes;
	};

private:
	Path m_path;
	GS::DMShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constBuffer;

	
};