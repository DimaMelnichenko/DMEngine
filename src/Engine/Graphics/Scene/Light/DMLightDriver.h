#pragma once

#include "DirectX.h"
#include <vector>
#include "DMLight.h"
#include "D3D\DMStructuredBuffer.h"

class DMLightDriver
{
public:
	using LightList = std::vector<DMLight>;
	// Размер буфера источников: лишние включённые источники не освещают
	static constexpr uint32_t maxLights = 32;
public:
	DMLightDriver( );
	~DMLightDriver();
	bool loadFromFile( const std::string& file );
	bool Initialize();
	uint32_t setBuffer( int8_t slot, SRVType type );
	// Солнце — первый включённый направленный источник: направление на свет (нормированное) и цвет.
	// Без него — запасной свет, как в setBuffer
	void directionalLight( XMFLOAT3& direction, XMFLOAT3& color ) const;

private:
	// Направление запасного света, когда источников нет
	static XMFLOAT3 fallbackDirection();

	LightList m_light_list;
	// Раскладка — struct Light в Shaders/lighting.sh
	struct alignas( 16 ) LightBuffer
	{
		XMFLOAT3 position;
		int type;
		XMFLOAT3 direction;		// куда идёт свет, нормированное
		float attenuationRadius;
		XMFLOAT3 color;
		float cosOuterCone;
		float cosInnerCone;
		XMFLOAT3 padding;
	};
	static_assert( sizeof( LightBuffer ) == 64, "LightBuffer must match struct Light in Shaders/lighting.sh" );
	DMStructuredBuffer m_structBuffer;
	std::vector<LightBuffer> m_lightParamBuffer;
};

