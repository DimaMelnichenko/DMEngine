#pragma once
#include "DirectX.h"
#include "Path.h"
#include "Scene/Shaders/DMShader.h"
#include "D3D/DMStructuredBuffer.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

class Terrain
{
public:
	Terrain();
	~Terrain();

	bool initialize();

	void render(  );

	struct Param
	{
		XMFLOAT4 tessFactors;
		XMFLOAT2 modes;
		XMFLOAT2 hightMultipler;
	};
	
	XMMATRIX worldMatrix( const XMFLOAT3& cameraPosition );

	PropertyContainer* properties();

	bool wireframe();

private:
	Path m_path;
	GS::DMShader m_shader;
	com_unique_ptr<ID3D11Buffer> m_constBuffer;
	DMStructuredBuffer m_structuredBuffer;
	uint16_t m_ringCount;
	uint16_t m_pathSize;
	PropertyContainer m_properties;
};

}