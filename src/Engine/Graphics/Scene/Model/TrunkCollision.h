#pragma once

#include <algorithm>
#include <cfloat>
#include <vector>
#include <DirectXMath.h>

namespace GS
{

// Ствол модели у земли — для капсулы простой коллизии (ModelCollision, Models.collision = capsule): вершины всех секций LOD 0
// не выше band над общим низом модели — середина их следа по X и Z (ось ствола) и полуширина (толщина у земли). Низ — общий
// у модели, а не у каждой секции: у ели своё «основание» у хвои — кончики нижних лап с одного бока, и объединение оснований
// секций (LodBlock::baseBounds) уводит ось в сторону лап и раздувает толщину. false — вершин нет
struct TrunkBase
{
	DirectX::XMFLOAT2 center = DirectX::XMFLOAT2( 0.0f, 0.0f );	// ось по X и Z, координаты модели
	float radius = 0.0f;	// полуширина следа у земли (наибольшая по X и Z)
	float bottom = 0.0f;	// низ модели по Y
	float height = 0.0f;	// высота модели
};

inline bool trunkBase( const std::vector<DirectX::XMFLOAT3>& points, float band, TrunkBase& base )
{
	if( points.empty() )
		return false;
	float bottom = FLT_MAX, top = -FLT_MAX;
	for( const DirectX::XMFLOAT3& p : points )
	{
		bottom = std::min( bottom, p.y );
		top = std::max( top, p.y );
	}
	float minX = FLT_MAX, maxX = -FLT_MAX, minZ = FLT_MAX, maxZ = -FLT_MAX;
	for( const DirectX::XMFLOAT3& p : points )
	{
		if( p.y > bottom + band )
			continue;
		minX = std::min( minX, p.x );
		maxX = std::max( maxX, p.x );
		minZ = std::min( minZ, p.z );
		maxZ = std::max( maxZ, p.z );
	}
	base.center = DirectX::XMFLOAT2( 0.5f * ( minX + maxX ), 0.5f * ( minZ + maxZ ) );
	base.radius = 0.5f * std::max( maxX - minX, maxZ - minZ );
	base.bottom = bottom;
	base.height = top - bottom;
	return true;
}

}
