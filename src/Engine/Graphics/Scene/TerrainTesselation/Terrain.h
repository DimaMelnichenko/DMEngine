#pragma once
#include "DirectX.h"
#include "Path.h"
#include "D3D/DMStructuredBuffer.h"
#include "Properties/PropertyContainer.h"
#include "TerrainShader.h"

namespace GS
{

class Terrain
{
public:
	Terrain();
	~Terrain();

	bool initialize();

	void render( const XMFLOAT3& cameraPosition );

	struct Param
	{
		XMFLOAT4 tessFactors;
		float innerTess;
		float hightMultipler;
		XMFLOAT2 snapOffset;
	};

	PropertyContainer* properties();

	bool wireframe();

private:
	XMFLOAT2& calcWorldOffset( const XMFLOAT3& cameraPosition );

private:
	Path m_path;
	TerrainShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constBuffer;
	DMStructuredBuffer m_structuredBuffer;
	uint16_t m_ringCount;
	uint16_t m_pathSize;
	PropertyContainer m_properties;
};

}