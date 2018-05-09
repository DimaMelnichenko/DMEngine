#pragma once

#include "DirectX.h"
#include <vector>
#include "DMLight.h"
#include "D3D\DMStructuredBuffer.h"

class DMLightDriver
{
public:
	using LightList = std::vector<std::unique_ptr<DMLight>>;
public:
	DMLightDriver( );
	~DMLightDriver();
	bool loadFromFile( const std::string& file );
	bool Initialize();
	void addLight( std::unique_ptr<DMLight>&& );
	uint32_t setBuffer( int8_t slot, SRVType type );
	LightList& lights();
	

private:
	LightList m_light_list;
	struct alignas( 16 ) LightBuffer
	{
		D3DXVECTOR3 lightPos;
		int lightType;
		D3DXVECTOR3 lightDir;
		float spotAngle;
		D3DXVECTOR3 lightColor;
		float attenuation;
	};
	DMStructuredBuffer m_structBuffer;
	std::vector<LightBuffer> m_lightParamBuffer;
};

