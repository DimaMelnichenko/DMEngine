#pragma once
#include "DirectX.h"
#include "Path.h"
#include "Scene/Shaders/DMShader.h"
#include "D3D/DMStructuredBuffer.h"

namespace GS
{

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

	std::vector<float>* tessFactor();

private:
	Path m_path;
	GS::DMShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constBuffer;
	std::vector<float> m_tessParameters;
	DMStructuredBuffer m_structuredBuffer;
	uint16_t m_ringCount;
	uint16_t m_pathSize;
};

}