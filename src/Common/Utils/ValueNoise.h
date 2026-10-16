#pragma once

#include <cmath>
#include <cstdint>

// Value noise: случайное значение 0…1 в узлах решётки (хэш узла и зерна), сглаженная интерполяция. Общий для правил
// слоёв (TerrainMasks) и зубцов гребней гор (FarTerrain)
inline float latticeValue( int x, int y, uint32_t seed )
{
	uint32_t h = static_cast<uint32_t>( x ) * 0x8da6b343u ^ static_cast<uint32_t>( y ) * 0xd8163841u ^ seed * 0xcb1ab31fu;
	h ^= h >> 13;
	h *= 0x5bd1e995u;
	h ^= h >> 15;
	return ( h & 0xffffff ) / 16777216.0f;
}

inline float valueNoise( float x, float y, uint32_t seed )
{
	const float fx = std::floor( x );
	const float fy = std::floor( y );
	const int ix = static_cast<int>( fx );
	const int iy = static_cast<int>( fy );
	float tx = x - fx;
	float ty = y - fy;
	tx = tx * tx * ( 3.0f - 2.0f * tx );
	ty = ty * ty * ( 3.0f - 2.0f * ty );
	const float top = latticeValue( ix, iy, seed ) + ( latticeValue( ix + 1, iy, seed ) - latticeValue( ix, iy, seed ) ) * tx;
	const float bottom = latticeValue( ix, iy + 1, seed ) + ( latticeValue( ix + 1, iy + 1, seed ) - latticeValue( ix, iy + 1, seed ) ) * tx;
	return top + ( bottom - top ) * ty;
}
