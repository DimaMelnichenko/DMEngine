#include "StoneTop.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace DirectX;

namespace GS
{

StoneTop rasterizeStoneTop( const std::vector<XMFLOAT3>& points, const std::vector<uint32_t>& indices, FXMMATRIX world )
{
	StoneTop top;
	if( points.empty() || indices.size() < 3 )
		return top;
	std::vector<XMFLOAT3> placed( points.size() );
	XMFLOAT3 low( std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() );
	XMFLOAT3 high( -low.x, -low.y, -low.z );
	for( size_t i = 0; i < points.size(); ++i )
	{
		XMStoreFloat3( &placed[i], XMVector3TransformCoord( XMLoadFloat3( &points[i] ), world ) );
		low = XMFLOAT3( std::min( low.x, placed[i].x ), std::min( low.y, placed[i].y ), std::min( low.z, placed[i].z ) );
		high = XMFLOAT3( std::max( high.x, placed[i].x ), std::max( high.y, placed[i].y ), std::max( high.z, placed[i].z ) );
	}
	// Квадрат по большему размаху, середина — середина меша
	top.side = std::max( high.x - low.x, high.z - low.z );
	if( top.side <= 0.0f )
		return top;
	top.origin = XMFLOAT2( 0.5f * ( low.x + high.x - top.side ), 0.5f * ( low.z + high.z - top.side ) );
	top.base = low.y;
	const int cells = static_cast<int>( StoneTop::cells );
	top.heights.assign( StoneTop::cells * StoneTop::cells, StoneTop::noTop );
	const float scale = cells / top.side;
	for( size_t t = 0; t + 2 < indices.size(); t += 3 )
	{
		// Вершины в текселях квадрата (x, z) и высота над низом
		XMFLOAT3 v[3];
		for( int k = 0; k < 3; ++k )
		{
			const XMFLOAT3& p = placed[indices[t + k]];
			v[k] = XMFLOAT3( ( p.x - top.origin.x ) * scale, p.y - top.base, ( p.z - top.origin.y ) * scale );
		}
		const float area = ( v[1].x - v[0].x ) * ( v[2].z - v[0].z ) - ( v[2].x - v[0].x ) * ( v[1].z - v[0].z );
		if( std::fabs( area ) < 1e-8f )
			continue;	// ребром к взгляду сверху
		const int i0 = std::max( static_cast<int>( std::ceil( std::min( { v[0].x, v[1].x, v[2].x } ) - 0.5f ) ), 0 );
		const int i1 = std::min( static_cast<int>( std::floor( std::max( { v[0].x, v[1].x, v[2].x } ) - 0.5f ) ), cells - 1 );
		const int j0 = std::max( static_cast<int>( std::ceil( std::min( { v[0].z, v[1].z, v[2].z } ) - 0.5f ) ), 0 );
		const int j1 = std::min( static_cast<int>( std::floor( std::max( { v[0].z, v[1].z, v[2].z } ) - 0.5f ) ), cells - 1 );
		for( int j = j0; j <= j1; ++j )
			for( int i = i0; i <= i1; ++i )
			{
				// Барицентрические координаты центра текселя; на ребре — тексель обоих треугольников (берётся наибольший)
				const float x = i + 0.5f;
				const float z = j + 0.5f;
				const float w1 = ( ( x - v[0].x ) * ( v[2].z - v[0].z ) - ( v[2].x - v[0].x ) * ( z - v[0].z ) ) / area;
				const float w2 = ( ( v[1].x - v[0].x ) * ( z - v[0].z ) - ( x - v[0].x ) * ( v[1].z - v[0].z ) ) / area;
				const float w0 = 1.0f - w1 - w2;
				constexpr float edge = -1e-5f;
				if( w0 < edge || w1 < edge || w2 < edge )
					continue;
				float& height = top.heights[static_cast<size_t>( j ) * cells + i];
				height = std::max( height, w0 * v[0].y + w1 * v[1].y + w2 * v[2].y );
			}
	}
	return top;
}

}
