#include "FluvialErosion.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <queue>
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Соседи D8: столбец, строка
constexpr int neighborCol[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
constexpr int neighborRow[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
// Подъём залитой поверхности на клетку пути, м: у каждой клетки есть строго более низкий сосед — сток определён
constexpr float fillEpsilon = 1e-3f;

// Три прохода прямоугольного фильтра радиуса radius по обеим осям — гаусс с сигмой ≈ radius + 0,5; за краем — край
std::vector<float> blur( const std::vector<float>& values, uint32_t size, int32_t radius )
{
	std::vector<float> current = values;
	std::vector<float> next( values.size() );
	const float scale = 1.0f / static_cast<float>( 2 * radius + 1 );
	for( int pass = 0; pass < 3; ++pass )
	{
		for( int axis = 0; axis < 2; ++axis )
		{
			for( uint32_t line = 0; line < size; ++line )
			{
				auto index = [&]( int64_t i )
				{
					const size_t k = static_cast<size_t>( std::clamp<int64_t>( i, 0, size - 1 ) );
					return axis == 0 ? static_cast<size_t>( line ) * size + k : k * size + line;
				};
				float sum = 0.0f;
				for( int32_t k = -radius; k <= radius; ++k )
					sum += current[index( k )];
				for( uint32_t i = 0; i < size; ++i )
				{
					next[index( i )] = sum * scale;
					sum += current[index( static_cast<int64_t>( i ) + radius + 1 )] - current[index( static_cast<int64_t>( i ) - radius )];
				}
			}
			std::swap( current, next );
		}
	}
	return current;
}

}

void FluvialErosion::run( std::vector<float>& heights, uint32_t size, float cell, const XMFLOAT2& origin, float mapSize,
						  const FluvialErosionSettings& settings )
{
	const auto start = std::chrono::steady_clock::now();
	const size_t count = static_cast<size_t>( size ) * size;
	const int n = static_cast<int>( size );
	const float diagonal = cell * std::sqrt( 2.0f );

	// У карты уровня эрозия гаснет: её край сводится с горами
	std::vector<float> fade( count );
	const float fadeFrom = settings.mapFade.from;
	const float fadeTo = std::max( settings.mapFade.to, fadeFrom + 1.0f );
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			const float x = origin.x + ( col + 0.5f ) * cell;
			const float z = origin.y + ( n - row - 0.5f ) * cell;
			const float dx = std::max( { -x, x - mapSize, 0.0f } );
			const float dz = std::max( { -z, z - mapSize, 0.0f } );
			const float t = std::clamp( ( std::sqrt( dx * dx + dz * dz ) - fadeFrom ) / ( fadeTo - fadeFrom ), 0.0f, 1.0f );
			fade[static_cast<size_t>( row ) * size + col] = t * t * ( 3.0f - 2.0f * t );
		}
	}

	const float area = cell * cell;
	const float years = std::max( settings.years, 0.0f );
	const uint32_t steps = static_cast<uint32_t>( std::max( settings.steps, 1 ) );
	const float dt = years / static_cast<float>( steps );
	const float erodibility = std::max( settings.erodibility, 0.0f );
	const float exponent = std::clamp( settings.areaExponent, 0.1f, 1.0f );
	const float diffusion = std::max( settings.hillslopeDiffusion, 0.0f );
	// Склоны — явной схемой: шаг устойчив при Kd · dt / клетка² ≤ 0,25 — дробится на подшаги
	const uint32_t diffusionSubsteps = static_cast<uint32_t>( std::ceil( diffusion * dt / ( 0.25f * area ) ) );

	// Шум для выбора стока — только для направления, высоты не трогает: на ровном склоне сток D8 иначе бежит параллельными
	// бороздами вдоль осей сетки. Гладкий (узлы через 4 клетки, билинейно) и чуть зёрен, ± routingNoise м
	std::vector<float> routing( count );
	{
		auto hash = []( uint32_t x, uint32_t y )
		{
			uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u;
			h ^= h >> 13;
			h *= 0x85ebca6bu;
			h ^= h >> 16;
			return static_cast<float>( h & 0xffffu ) / 65535.0f * 2.0f - 1.0f;
		};
		const float amplitude = std::max( settings.routingNoise, 0.0f );
		for( int row = 0; row < n; ++row )
		{
			for( int col = 0; col < n; ++col )
			{
				const float u = col / 4.0f;
				const float v = row / 4.0f;
				const uint32_t x0 = static_cast<uint32_t>( u );
				const uint32_t y0 = static_cast<uint32_t>( v );
				const float fx = u - x0;
				const float fy = v - y0;
				const float top = hash( x0, y0 ) + ( hash( x0 + 1, y0 ) - hash( x0, y0 ) ) * fx;
				const float bottom = hash( x0, y0 + 1 ) + ( hash( x0 + 1, y0 + 1 ) - hash( x0, y0 + 1 ) ) * fx;
				const float smooth = top + ( bottom - top ) * fy;
				routing[static_cast<size_t>( row ) * size + col] = amplitude * ( 0.8f * smooth + 0.2f * hash( col + 7919u, row + 104729u ) );
			}
		}
	}

	std::vector<float> filled( count );
	std::vector<uint32_t> receiver( count );
	std::vector<float> receiverDistance( count );
	std::vector<uint32_t> order;
	order.reserve( count );
	std::vector<float> drainage( count );
	std::vector<uint8_t> closed( count );
	std::vector<float> laplacian( diffusionSubsteps > 0 ? count : 0 );
	using Entry = std::pair<float, uint32_t>;
	float deepest = 0.0f;
	const std::vector<float> source = heights;

	// Рельеф заново из подъёма (Cordonnier и др. 2016): огибающая гор — карта, размытая на upliftScale (без хребтов и чаш
	// шума); рельеф начинается с её десятой доли и поднимается со скоростью огибающая / upliftTime, а реки его режут — хребты
	// и долины рождает эрозия. Низины карты (котловина, долины, море: размытая на четверть того карта ниже upliftFloor, граница
	// размыта так же) не поднимаются — остаются, и реки гор стекают в них. У карты уровня в конце — её прежние высоты
	const bool regrow = settings.upliftScale > 0.0f;
	std::vector<float> uplift;
	if( regrow )
	{
		const int32_t radius = std::max( static_cast<int32_t>( settings.upliftScale / cell ), 1 );
		const std::vector<float> envelope = blur( source, size, radius );
		const std::vector<float> lowlands = blur( source, size, std::max( radius / 4, 1 ) );
		// Граница низин — плавно по расстоянию (полоса около upliftScale / 2): по высоте она была бы отвесной стеной
		std::vector<float> highlands( count );
		for( size_t i = 0; i < count; ++i )
			highlands[i] = lowlands[i] > settings.upliftFloor ? 1.0f : 0.0f;
		highlands = blur( highlands, size, std::max( radius / 4, 1 ) );
		const float rate = 1.0f / std::max( settings.upliftTime, 1.0f );
		uplift.resize( count );
		for( size_t i = 0; i < count; ++i )
		{
			const float t = std::clamp( highlands[i], 0.0f, 1.0f );
			const float mountains = t * t * ( 3.0f - 2.0f * t );
			const float top = std::max( envelope[i], 0.0f ) * mountains;
			uplift[i] = top * rate;
			// Низины — как у карты (сглаженной), горы — с десятой доли огибающей
			heights[i] = lowlands[i] + ( 0.1f * top - lowlands[i] ) * mountains;
		}
	}

	for( uint32_t step = 0; step < steps; ++step )
	{
		// 0. Подъём (кроме устьев — края сетки и моря)
		if( regrow )
			for( int row = 1; row < n - 1; ++row )
				for( int col = 1; col < n - 1; ++col )
				{
					const size_t i = static_cast<size_t>( row ) * size + col;
					if( heights[i] >= 0.0f )
						heights[i] += uplift[i] * dt;
				}

		// 1. Залитая поверхность и порядок от устьев вверх (priority-flood): устья — край сетки и море (ниже 0 м)
		std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
		std::fill( closed.begin(), closed.end(), uint8_t( 0 ) );
		order.clear();
		for( int row = 0; row < n; ++row )
		{
			for( int col = 0; col < n; ++col )
			{
				const uint32_t i = static_cast<uint32_t>( row * n + col );
				if( row == 0 || col == 0 || row == n - 1 || col == n - 1 || heights[i] < 0.0f )
				{
					closed[i] = 1;
					filled[i] = heights[i] + routing[i];
					queue.push( { filled[i], i } );
				}
			}
		}
		while( !queue.empty() )
		{
			const uint32_t i = queue.top().second;
			queue.pop();
			order.push_back( i );
			const int row = static_cast<int>( i ) / n;
			const int col = static_cast<int>( i ) % n;
			for( int k = 0; k < 8; ++k )
			{
				const int r = row + neighborRow[k];
				const int c = col + neighborCol[k];
				if( r < 0 || c < 0 || r >= n || c >= n )
					continue;
				const uint32_t j = static_cast<uint32_t>( r * n + c );
				if( closed[j] )
					continue;
				closed[j] = 1;
				filled[j] = std::max( heights[j] + routing[j], filled[i] + fillEpsilon );
				queue.push( { filled[j], j } );
			}
		}

		// 2. Сток — круче всего вниз по залитой поверхности (он раньше в порядке); устье — само себе
		for( const uint32_t i : order )
		{
			receiver[i] = i;
			receiverDistance[i] = cell;
			const int row = static_cast<int>( i ) / n;
			const int col = static_cast<int>( i ) % n;
			if( row == 0 || col == 0 || row == n - 1 || col == n - 1 || heights[i] < 0.0f )
				continue;
			float steepest = 0.0f;
			for( int k = 0; k < 8; ++k )
			{
				const uint32_t j = static_cast<uint32_t>( ( row + neighborRow[k] ) * n + col + neighborCol[k] );
				const float distance = k < 4 ? cell : diagonal;
				const float slope = ( filled[i] - filled[j] ) / distance;
				if( slope > steepest )
				{
					steepest = slope;
					receiver[i] = j;
					receiverDistance[i] = distance;
				}
			}
		}

		// 3. Водосбор — от верховий к устьям
		std::fill( drainage.begin(), drainage.end(), area );
		for( auto it = order.rbegin(); it != order.rend(); ++it )
			if( receiver[*it] != *it )
				drainage[receiver[*it]] += drainage[*it];

		// 4. Врез неявной схемой FastScape (n = 1): от устьев вверх, сток уже на новой высоте. В низине (клетка ниже стока) —
		// без вреза: её спустит врез порога
		for( const uint32_t i : order )
		{
			const uint32_t r = receiver[i];
			if( r == i || heights[i] <= heights[r] )
				continue;
			const float factor = erodibility * dt * std::pow( drainage[i], exponent ) / receiverDistance[i] * ( regrow ? 1.0f : fade[i] );
			heights[i] = ( heights[i] + factor * heights[r] ) / ( 1.0f + factor );
		}

		// 5. Склоны — диффузия
		for( uint32_t sub = 0; sub < diffusionSubsteps; ++sub )
		{
			const float k = diffusion * dt / static_cast<float>( diffusionSubsteps ) / area;
			for( int row = 1; row < n - 1; ++row )
				for( int col = 1; col < n - 1; ++col )
				{
					const size_t i = static_cast<size_t>( row ) * size + col;
					laplacian[i] = heights[i - 1] + heights[i + 1] + heights[i - size] + heights[i + size] - 4.0f * heights[i];
				}
			for( int row = 1; row < n - 1; ++row )
				for( int col = 1; col < n - 1; ++col )
				{
					const size_t i = static_cast<size_t>( row ) * size + col;
					heights[i] += k * laplacian[i] * ( regrow ? 1.0f : fade[i] );
				}
		}
	}
	if( regrow )
		for( size_t i = 0; i < count; ++i )
			heights[i] = source[i] + ( heights[i] - source[i] ) * fade[i];
	for( size_t i = 0; i < count; ++i )
		deepest = std::max( deepest, source[i] - heights[i] );

	char text[200];
	std::snprintf( text, sizeof( text ), "Fluvial erosion %ux%u, cell %.0f m: %u steps of %.0f yr, deepest %.0f m, ms: %.0f", size, size, cell,
				   steps, dt, deepest, std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
	LOG( text );
}

}
