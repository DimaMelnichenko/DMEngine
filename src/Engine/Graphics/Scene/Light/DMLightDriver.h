#pragma once

#include "DirectX.h"
#include <vector>
#include "DMLight.h"
#include "D3D\DMStructuredBuffer.h"

class DMLightDriver
{
public:
	using LightList = std::vector<DMLight>;
public:
	DMLightDriver( );
	~DMLightDriver();
	bool loadFromFile( const std::string& file );
	bool Initialize();
	void addLight( DMLight&& );
	uint32_t setBuffer( int8_t slot, SRVType type );
	LightList& lights();
	

private:
	LightList m_light_list;
	struct alignas( 16 ) LightBuffer
	{
		XMFLOAT3 lightPos;
		int lightType;
		XMFLOAT3 lightDir;
		float spotAngle;
		XMFLOAT3 lightColor;
		float attenuation;
	};
	DMStructuredBuffer m_structBuffer;
	std::vector<LightBuffer> m_lightParamBuffer;
};

