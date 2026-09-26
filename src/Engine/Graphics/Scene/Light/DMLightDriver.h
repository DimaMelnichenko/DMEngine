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
	uint32_t setBuffer( int8_t slot, SRVType type );
	// Окружающий свет полусферой (hemispheric ambient): цвет неба сверху, цвет земли снизу,
	// секция [Ambient] в Lights.ini (SkyColor, GroundColor)
	const XMFLOAT3& ambientSkyColor() const;
	const XMFLOAT3& ambientGroundColor() const;

private:
	LightList m_light_list;
	XMFLOAT3 m_ambientSkyColor = XMFLOAT3( 0.15f, 0.15f, 0.156f );
	XMFLOAT3 m_ambientGroundColor = XMFLOAT3( 0.075f, 0.069f, 0.063f );
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

