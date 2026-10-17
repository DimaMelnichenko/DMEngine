#pragma once

// Подделка рельефа для тестов: квадрат 256 × 256 м, высота — функцией (x, z); вне квадрата земли нет
#include <functional>
#include "Terrain\TerrainHeightSource.h"

class FakeTerrain : public GS::TerrainHeightSource
{
public:
	explicit FakeTerrain( std::function<float( float x, float z )> height ) : m_height( std::move( height ) ) {}
	GS::TerrainHeight terrainHeight() const override
	{
		GS::TerrainHeight height;
		height.worldSize = 256.0f;
		height.mapSize = 256;
		return height;
	}
	bool surfaceHeight( float x, float z, float& height ) const override
	{
		if( x < 0.0f || z < 0.0f || x > 256.0f || z > 256.0f )
			return false;
		height = m_height( x, z );
		return true;
	}

private:
	std::function<float( float x, float z )> m_height;
};
