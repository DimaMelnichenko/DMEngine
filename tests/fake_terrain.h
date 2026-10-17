#pragma once

// Подделка рельефа для тестов: квадрат 256 × 256 м, высота — функцией (x, z); вне квадрата земли нет. mapSize — узлов
// сетки на сторону (по ним строит землю физика: 256 — шаг 1 м, 1024 — 0,25 м); regions — детальные квадраты (там физика
// берёт свой шаг); water — уровень воды в точке (меньше −1e29 — воды нет)
#include <functional>
#include <vector>
#include "Terrain\TerrainHeightSource.h"

class FakeTerrain : public GS::TerrainHeightSource
{
public:
	explicit FakeTerrain( std::function<float( float x, float z )> height, uint32_t mapSize = 256 ) :
		m_height( std::move( height ) ), m_mapSize( mapSize )
	{
	}
	GS::TerrainHeight terrainHeight() const override
	{
		GS::TerrainHeight height;
		height.worldSize = 256.0f;
		height.mapSize = m_mapSize;
		return height;
	}
	bool surfaceHeight( float x, float z, float& height ) const override
	{
		if( x < 0.0f || z < 0.0f || x > 256.0f || z > 256.0f )
			return false;
		height = m_height( x, z );
		return true;
	}
	std::vector<FineRegion> fineRegions() const override { return regions; }
	bool waterLevel( float x, float z, float& level ) const override
	{
		if( !water )
			return false;
		level = water( x, z );
		return level > -1e29f;
	}

	std::vector<FineRegion> regions;
	std::function<float( float x, float z )> water;

private:
	std::function<float( float x, float z )> m_height;
	uint32_t m_mapSize;
};
