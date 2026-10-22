#include "FarForestRelief.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>

namespace GS
{

namespace
{

constexpr int neighbourRow[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
constexpr int neighbourCol[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
const float neighbourDistance[8] = { std::sqrt( 2.0f ), 1.0f, std::sqrt( 2.0f ), 1.0f, 1.0f, std::sqrt( 2.0f ), 1.0f, std::sqrt( 2.0f ) };
// Подъём залитой низины на шаг, м: из каждой клетки есть путь вниз к краю
constexpr float fillEpsilon = 1e-3f;
// За зоной выноса (склон положе runout) лавина гаснет: на клетку дальше доходит эта доля
constexpr float runoutDecay = 0.4f;
// Отрыв набирает силу на столько метров вокруг высоты отрыва
constexpr float releaseHeightBand = 100.0f;

float smoothstep( float edge0, float edge1, float x )
{
	const float t = std::clamp( ( x - edge0 ) / std::max( edge1 - edge0, 1e-6f ), 0.0f, 1.0f );
	return t * t * ( 3.0f - 2.0f * t );
}

}

void FarForestRelief::build( const std::vector<float>& heights, uint32_t size, float cell )
{
	m_size = size;
	m_cell = cell;
	m_heights = heights;
	const int n = static_cast<int>( size );
	const size_t cells = static_cast<size_t>( size ) * size;
	if( size < 3 || heights.size() != cells )
	{
		m_slope.clear();
		m_receiver.clear();
		m_order.clear();
		m_drainage.clear();
		return;
	}
	auto at = [&]( int row, int col ) { return heights[static_cast<size_t>( row ) * n + col]; };

	// Уклон — центральными разностями (у края — односторонними)
	m_slope.assign( cells, 0.0f );
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			const int c0 = std::max( col - 1, 0 ), c1 = std::min( col + 1, n - 1 );
			const int r0 = std::max( row - 1, 0 ), r1 = std::min( row + 1, n - 1 );
			const float dx = ( at( row, c1 ) - at( row, c0 ) ) / ( ( c1 - c0 ) * cell );
			const float dz = ( at( r1, col ) - at( r0, col ) ) / ( ( r1 - r0 ) * cell );
			m_slope[static_cast<size_t>( row ) * n + col] = std::atan( std::hypot( dx, dz ) ) * 57.29578f;
		}
	}

	// Заливка от края: клетка с наименьшей залитой высотой — первой; сосед — не ниже открывшей его клетки (на шаг выше)
	std::vector<float> filled( heights );
	std::vector<uint8_t> closed( cells, 0 );
	using Entry = std::pair<float, int32_t>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			if( row != 0 && col != 0 && row != n - 1 && col != n - 1 )
				continue;
			const int32_t i = row * n + col;
			closed[i] = 1;
			open.push( { filled[i], i } );
		}
	}
	m_order.clear();
	m_order.reserve( cells );
	while( !open.empty() )
	{
		const int32_t i = open.top().second;
		open.pop();
		m_order.push_back( i );
		for( int k = 0; k < 8; ++k )
		{
			const int row = i / n + neighbourRow[k];
			const int col = i % n + neighbourCol[k];
			if( row < 0 || col < 0 || row >= n || col >= n )
				continue;
			const int32_t j = row * n + col;
			if( closed[j] )
				continue;
			closed[j] = 1;
			filled[j] = std::max( heights[j], filled[i] + fillEpsilon );
			open.push( { filled[j], j } );
		}
	}

	// Получатель — сосед с наибольшим уклоном вниз по залитой карте; у края сетки — за край
	m_receiver.assign( cells, -1 );
	for( int row = 1; row < n - 1; ++row )
	{
		for( int col = 1; col < n - 1; ++col )
		{
			const int32_t i = row * n + col;
			float steepest = 0.0f;
			for( int k = 0; k < 8; ++k )
			{
				const int32_t j = ( row + neighbourRow[k] ) * n + col + neighbourCol[k];
				const float drop = ( filled[i] - filled[j] ) / neighbourDistance[k];
				if( drop > steepest )
				{
					steepest = drop;
					m_receiver[i] = j;
				}
			}
		}
	}

	// Водосбор — от верховий: порядок заливки наоборот (залитая высота получателя ниже — он открыт раньше)
	m_drainage.assign( cells, cell * cell );
	for( auto it = m_order.rbegin(); it != m_order.rend(); ++it )
		if( m_receiver[*it] >= 0 )
			m_drainage[m_receiver[*it]] += m_drainage[*it];
}

std::vector<float> FarForestRelief::avalanche( const AvalancheRules& rules ) const
{
	std::vector<float> share( m_heights.size(), 0.0f );
	if( m_order.empty() )
		return share;
	// Площадь отрыва, сошедшая через клетку, га — от верховий вниз по линиям стока
	const float hectares = m_cell * m_cell / 10000.0f;
	std::vector<float> mass( m_heights.size(), 0.0f );
	for( auto it = m_order.rbegin(); it != m_order.rend(); ++it )
	{
		const int32_t i = *it;
		mass[i] += hectares * smoothstep( rules.slopeFrom, rules.slopeTo, m_slope[i] ) *
				   smoothstep( rules.height - releaseHeightBand, rules.height + releaseHeightBand, m_heights[i] );
		share[i] = smoothstep( rules.areaFrom, rules.areaTo, mass[i] );
		if( m_receiver[i] >= 0 )
			mass[m_receiver[i]] += mass[i] * ( m_slope[i] >= rules.runout ? 1.0f : runoutDecay );
	}
	return share;
}

}
