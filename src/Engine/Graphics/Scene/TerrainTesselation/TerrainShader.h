#pragma once

#include "Shaders/DMShader.h"
#include "D3D/DMStructuredBuffer.h"

namespace GS
{

class TerrainShader
{
public:
	TerrainShader();
	~TerrainShader();

	PropertyContainer* properties();

	bool initialize();
	void render( XMFLOAT2& snapOffset, DMStructuredBuffer& structBuffer, uint32_t indexCount, uint32_t instanceCount );

	struct Parameters
	{
		XMFLOAT4 tessFactors;
		float innerTess;
		float hightMultipler;
		XMFLOAT2 snapOffset;
	};

private:
	PropertyContainer m_properties;
	com_unique_ptr<ID3D11Buffer> m_constBuffer;
	GS::DMShader m_shader;
};

}