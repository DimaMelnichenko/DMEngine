#include "TerrainWind.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Давление (потенциал поправки скорости) на сетке m × m с шагом cell: ∑ h_грани ( p_соседа − p ) = cell · D, D — расхождение
// расхода ровного ветра ∑ ± h_грани u₀; за краем карты p = 0 (открытая граница: ветер входит и выходит свободно)
struct PressureLevel
{
	uint32_t m = 0;
	float cell = 1.0f;
	TerrainField east;		// толщина слоя на восточной грани ячейки (между столбцами col и col + 1)
	TerrainField south;		// на южной (между строками row и row + 1)
	TerrainField source;	// cell · D
	TerrainField pressure;
};

// Толщина слоя сетки m — средняя по блокам factor × factor тонкой
TerrainField downsample( const TerrainField& fine, uint32_t size, uint32_t factor )
{
	if( factor == 1 )
		return fine;
	const uint32_t m = size / factor;
	TerrainField coarse( static_cast<size_t>( m ) * m );
	forRows( m, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < m; ++col )
		{
			float sum = 0.0f;
			for( uint32_t y = 0; y < factor; ++y )
				for( uint32_t x = 0; x < factor; ++x )
					sum += fine[static_cast<size_t>( row * factor + y ) * size + col * factor + x];
			coarse[static_cast<size_t>( row ) * m + col] = sum / ( factor * factor );
		}
	} );
	return coarse;
}

// Давление грубой сетки m / 2 → начальное приближение сетки m (билинейно, центры ячеек)
TerrainField upsample( const TerrainField& coarse, uint32_t coarseSize )
{
	const uint32_t m = coarseSize * 2;
	TerrainField fine( static_cast<size_t>( m ) * m );
	const int n = static_cast<int>( coarseSize );
	forRows( m, [&]( uint32_t row )
	{
		const float y = ( row + 0.5f ) * 0.5f - 0.5f;
		const int y0 = static_cast<int>( std::floor( y ) );
		const float fy = y - y0;
		for( uint32_t col = 0; col < m; ++col )
		{
			const float x = ( col + 0.5f ) * 0.5f - 0.5f;
			const int x0 = static_cast<int>( std::floor( x ) );
			const float fx = x - x0;
			auto at = [&]( int r, int c ) { return coarse[static_cast<size_t>( std::clamp( r, 0, n - 1 ) ) * n + std::clamp( c, 0, n - 1 )]; };
			const float top = at( y0, x0 ) + ( at( y0, x0 + 1 ) - at( y0, x0 ) ) * fx;
			const float bottom = at( y0 + 1, x0 ) + ( at( y0 + 1, x0 + 1 ) - at( y0 + 1, x0 ) ) * fx;
			fine[static_cast<size_t>( row ) * m + col] = top + ( bottom - top ) * fy;
		}
	} );
	return fine;
}

void prepareLevel( PressureLevel& level, const TerrainField& thickness, float u0Col, float u0Row )
{
	const uint32_t m = level.m;
	const size_t cells = static_cast<size_t>( m ) * m;
	level.east.assign( cells, 0.0f );
	level.south.assign( cells, 0.0f );
	level.source.assign( cells, 0.0f );
	// Грань к соседу — средняя толщина двух ячеек; грань к краю карты — толщина своей
	forRows( m, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < m; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * m + col;
			level.east[i] = col + 1 < m ? 0.5f * ( thickness[i] + thickness[i + 1] ) : thickness[i];
			level.south[i] = row + 1 < m ? 0.5f * ( thickness[i] + thickness[i + m] ) : thickness[i];
		}
	} );
	forRows( m, [&]( uint32_t row )
	{
		for( uint32_t col = 0; col < m; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * m + col;
			const float west = col > 0 ? level.east[i - 1] : thickness[i];
			const float north = row > 0 ? level.south[i - m] : thickness[i];
			level.source[i] = level.cell * ( ( level.east[i] - west ) * u0Col + ( level.south[i] - north ) * u0Row );
		}
	} );
}

// Верхняя релаксация (SOR) красно-чёрными полушагами; невязка — наибольшая доля от наибольшего источника
float relax( PressureLevel& level, uint32_t iterations, float omega )
{
	const uint32_t m = level.m;
	const int n = static_cast<int>( m );
	TerrainField& p = level.pressure;
	auto update = [&]( uint32_t parity )
	{
		forRows( m, [&]( uint32_t row )
		{
			const int r = static_cast<int>( row );
			for( int col = ( r + static_cast<int>( parity ) ) & 1; col < n; col += 2 )
			{
				const size_t i = static_cast<size_t>( r ) * n + col;
				const float east = level.east[i];
				const float west = col > 0 ? level.east[i - 1] : level.east[i];
				const float south = level.south[i];
				const float north = r > 0 ? level.south[i - n] : level.south[i];
				const float pe = col + 1 < n ? p[i + 1] : 0.0f;
				const float pw = col > 0 ? p[i - 1] : 0.0f;
				const float ps = r + 1 < n ? p[i + n] : 0.0f;
				const float pn = r > 0 ? p[i - n] : 0.0f;
				const float target = ( east * pe + west * pw + south * ps + north * pn - level.source[i] ) / ( east + west + south + north );
				p[i] += omega * ( target - p[i] );
			}
		} );
	};
	for( uint32_t it = 0; it < iterations; ++it )
	{
		update( 0 );
		update( 1 );
	}
	float worst = 0.0f, largest = 1e-9f;
	for( int r = 0; r < n; ++r )
		for( int col = 0; col < n; ++col )
		{
			const size_t i = static_cast<size_t>( r ) * n + col;
			const float west = col > 0 ? level.east[i - 1] : level.east[i];
			const float north = r > 0 ? level.south[i - n] : level.south[i];
			const float pe = col + 1 < n ? p[i + 1] : 0.0f;
			const float pw = col > 0 ? p[i - 1] : 0.0f;
			const float ps = r + 1 < n ? p[i + n] : 0.0f;
			const float pn = r > 0 ? p[i - n] : 0.0f;
			const float residual = level.east[i] * ( pe - p[i] ) + west * ( pw - p[i] ) + level.south[i] * ( ps - p[i] ) + north * ( pn - p[i] ) -
								   level.source[i];
			worst = std::max( worst, std::fabs( residual ) );
			largest = std::max( largest, std::fabs( level.source[i] ) );
		}
	return worst / largest;
}

}

const char* TerrainWind::codeStamp()
{
	return __DATE__ " " __TIME__;
}

bool TerrainWind::solveTerrain( const TerrainField& heights, uint32_t size, float texel, float windX, float windZ,
								const WindFieldSettings& settings, std::vector<Cell>& result )
{
	const float windLength = std::hypot( windX, windZ );
	if( windLength <= 1e-3f || heights.size() != static_cast<size_t>( size ) * size || size < 64 )
		return false;
	const auto start = std::chrono::steady_clock::now();
	const int n = static_cast<int>( size );
	// Оси сетки: столбец — +x мира, строка — −z мира
	const float u0Col = windX / windLength;
	const float u0Row = -windZ / windLength;

	// 1. Слой воздуха: верх — рельеф, сглаженный примерно на layerSmoothing, плюс layerHeight; не тоньше 5 % слоя
	const int smoothRadius = std::max( static_cast<int>( std::lround( settings.layerSmoothing / texel * 0.5f ) ), 1 );
	const TerrainField top = blurField( heights, size, smoothRadius );
	const float minThickness = std::max( settings.layerHeight * 0.05f, 1.0f );
	TerrainField thickness( heights.size() );
	for( size_t i = 0; i < heights.size(); ++i )
		thickness[i] = std::max( top[i] + settings.layerHeight - heights[i], minThickness );

	// Давление каскадом: до трёх сеток (тонкая, ½, ¼), не мельче 64; итераций — больше на грубой (дальний перенос дешевле)
	std::vector<uint32_t> factors;
	for( uint32_t factor = 4; factor >= 1; factor /= 2 )
		if( size / factor >= 64 )
			factors.push_back( factor );
	PressureLevel level;
	std::string residuals;
	for( size_t l = 0; l < factors.size(); ++l )
	{
		const uint32_t factor = factors[l];
		const uint32_t m = size / factor;
		const TerrainField previous = std::move( level.pressure );
		const uint32_t previousSize = level.m;
		level = PressureLevel();
		level.m = m;
		level.cell = texel * factor;
		prepareLevel( level, downsample( thickness, size, factor ), u0Col, u0Row );
		level.pressure = previousSize ? upsample( previous, previousSize ) : TerrainField( static_cast<size_t>( m ) * m, 0.0f );
		const float omega = std::min( 2.0f / ( 1.0f + std::sin( 3.14159265f / m ) ), 1.97f );
		const uint32_t iterations = l == 0 ? 1200 : ( l + 1 < factors.size() ? 400 : 250 );
		char text[48];
		std::snprintf( text, sizeof( text ), "%s%u: %.4f", residuals.empty() ? "" : ", ", m, relax( level, iterations, omega ) );
		residuals += text;
	}

	// Скорость в центре ячейки — u₀ − ∇p центральными разностями (за краем p = 0)
	const float cell = level.cell;
	const TerrainField& p = level.pressure;
	auto pressureAt = [&]( int row, int col ) { return row < 0 || row >= n || col < 0 || col >= n ? 0.0f : p[static_cast<size_t>( row ) * n + col]; };
	auto heightAt = [&]( int row, int col ) { return heights[static_cast<size_t>( std::clamp( row, 0, n - 1 ) ) * n + std::clamp( col, 0, n - 1 )]; };
	auto flowAt = [&]( int row, int col, float& uCol, float& uRow )
	{
		uCol = u0Col - ( pressureAt( row, col + 1 ) - pressureAt( row, col - 1 ) ) / ( 2.0f * cell );
		uRow = u0Row - ( pressureAt( row + 1, col ) - pressureAt( row - 1, col ) ) / ( 2.0f * cell );
	};
	// Ветер уровня — над ровным открытым местом: поток делится на медиану скорости ровных клеток (уклон до 5°: дно долины,
	// озёра, луга). Иначе толстый слой над дном долины давал там 0,6–0,7, а не ветер уровня
	std::vector<float> flatSpeeds;
	for( int r = 1; r + 1 < n; r += 2 )
		for( int col = 1; col + 1 < n; col += 2 )
		{
			const float slope = std::hypot( heightAt( r, col + 1 ) - heightAt( r, col - 1 ), heightAt( r + 1, col ) - heightAt( r - 1, col ) ) / ( 2.0f * texel );
			if( slope < 0.0875f )
			{
				float uCol, uRow;
				flowAt( r, col, uCol, uRow );
				flatSpeeds.push_back( std::hypot( uCol, uRow ) );
			}
		}
	float reference = 1.0f;
	if( flatSpeeds.size() > 100 )
	{
		std::nth_element( flatSpeeds.begin(), flatSpeeds.begin() + flatSpeeds.size() / 2, flatSpeeds.end() );
		reference = std::max( flatSpeeds[flatSpeeds.size() / 2], 0.05f );
	}
	// 2. Подветренная тень по углу горизонта навстречу ветру
	const TerrainField horizon = upwindHorizon( heights, size, texel, 1.0f, windX, windZ, settings.reach );
	result.assign( heights.size(), Cell( 0.0f, 0.0f, 0.0f, 0.0f ) );
	std::vector<Cell> flowOnly( heights.size() );
	forRows( size, [&]( uint32_t row )
	{
		const int r = static_cast<int>( row );
		for( int col = 0; col < n; ++col )
		{
			const size_t i = static_cast<size_t>( r ) * n + col;
			float uCol, uRow;
			flowAt( r, col, uCol, uRow );
			uCol /= reference;
			uRow /= reference;
			const float speed = std::hypot( uCol, uRow );
			if( speed > settings.maxSpeedup )
			{
				uCol *= settings.maxSpeedup / speed;
				uRow *= settings.maxSpeedup / speed;
			}
			flowOnly[i] = Cell( uCol, 0.0f, -uRow, 0.0f );
			const float shelter = fieldSmoothstep( settings.shelterFrom, settings.shelterTo, horizon[i] );
			const float slow = 1.0f + ( settings.leeSlowdown - 1.0f ) * shelter;
			const float vx = uCol * slow;
			const float vz = -uRow * slow;
			// 3. Вертикаль — вдоль рельефа: скорость · уклон (по X мира — столбцы, по Z — против строк)
			const float slopeX = ( heightAt( r, col + 1 ) - heightAt( r, col - 1 ) ) / ( 2.0f * texel );
			const float slopeZ = -( heightAt( r + 1, col ) - heightAt( r - 1, col ) ) / ( 2.0f * texel );
			result[i] = Cell( vx, vx * slopeX + vz * slopeZ, vz, shelter * settings.turbulence );
		}
	} );
	char text[256];
	std::snprintf( text, sizeof( text ), "Wind field over the terrain %ux%u: residual %s, flat ground speed %.2f (taken as 1), ms: %.0f",
				   size, size, residuals.c_str(), reference, std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
	LOG( text );
	LOG( "Wind field, flow over the terrain only: " + summary( flowOnly ) + "; with the lee shelter: " + summary( result ) );
	return true;
}

TerrainField TerrainWind::canopy( const std::vector<const TerrainField*>& forest, uint32_t size, float worldSize,
								  const std::vector<WindObstacle>& obstacles )
{
	const size_t cells = static_cast<size_t>( size ) * size;
	TerrainField result( cells, 0.0f );
	for( const TerrainField* mask : forest )
		if( mask && mask->size() == cells )
			for( size_t i = 0; i < cells; ++i )
				result[i] += ( *mask )[i];
	// Маска леса — доля ячеек с деревом: редкие деревья (рощи на дне долины, 0,2–0,4) ветер почти не держат, сомкнутый
	// полог — держит; полог — переход от 0,15 до 0,6
	for( float& value : result )
		value = fieldSmoothstep( 0.15f, 0.6f, value );
	// Препятствие — круг по габариту с краем в тексель; строка — от дальнего края по z, как у карты высот
	const float texel = worldSize / size;
	const int n = static_cast<int>( size );
	for( const WindObstacle& obstacle : obstacles )
	{
		const float col = obstacle.x / texel - 0.5f;
		const float row = ( worldSize - obstacle.z ) / texel - 0.5f;
		const float radius = obstacle.radius / texel;
		const int r0 = std::max( static_cast<int>( std::floor( row - radius - 1.0f ) ), 0 );
		const int r1 = std::min( static_cast<int>( std::ceil( row + radius + 1.0f ) ), n - 1 );
		const int c0 = std::max( static_cast<int>( std::floor( col - radius - 1.0f ) ), 0 );
		const int c1 = std::min( static_cast<int>( std::ceil( col + radius + 1.0f ) ), n - 1 );
		for( int r = r0; r <= r1; ++r )
			for( int c = c0; c <= c1; ++c )
			{
				const float weight = std::clamp( radius - std::hypot( c - col, r - row ) + 0.5f, 0.0f, 1.0f );
				float& value = result[static_cast<size_t>( r ) * n + c];
				value = std::max( value, weight );
			}
	}
	return result;
}

void TerrainWind::applyCanopy( const std::vector<Cell>& terrain, uint32_t size, float texel, const TerrainField& canopy,
							   const WindFieldSettings& settings, std::vector<Cell>& result )
{
	const size_t cells = static_cast<size_t>( size ) * size;
	const int n = static_cast<int>( size );
	TerrainField target( cells );
	for( size_t i = 0; i < cells; ++i )
		target[i] = 1.0f - settings.forestDamping * std::clamp( i < canopy.size() ? canopy[i] : 0.0f, 0.0f, 1.0f );

	// Множитель полога тянется к цели вдоль ветра: шаг — несколько метров против течения (полулагранжево), в лес — за
	// forestLength, из леса — за recoveryLength; итераций — пока возмещение пройдёт втрое дальше своей длины
	const float step = std::max( 4.0f, texel );
	const float stepTexels = step / texel;
	const float into = 1.0f - std::exp( -step / std::max( settings.forestLength, 0.1f ) );
	const float out = 1.0f - std::exp( -step / std::max( settings.recoveryLength, 0.1f ) );
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( 3.0f * std::max( settings.recoveryLength, settings.forestLength ) / step, 8.0f, 300.0f ) );
	TerrainField factor = target, next( cells ), drop( cells, 0.0f );
	auto sample = [&]( const TerrainField& field, float row, float col )
	{
		row = std::clamp( row, 0.0f, static_cast<float>( n - 1 ) );
		col = std::clamp( col, 0.0f, static_cast<float>( n - 1 ) );
		const int r0 = std::min( static_cast<int>( row ), n - 2 );
		const int c0 = std::min( static_cast<int>( col ), n - 2 );
		const float fr = row - r0, fc = col - c0;
		const float* line = &field[static_cast<size_t>( r0 ) * n + c0];
		const float topRow = line[0] + ( line[1] - line[0] ) * fc;
		const float bottomRow = line[n] + ( line[n + 1] - line[n] ) * fc;
		return topRow + ( bottomRow - topRow ) * fr;
	};
	for( uint32_t it = 0; it < iterations; ++it )
	{
		const bool last = it + 1 == iterations;
		forRows( size, [&]( uint32_t row )
		{
			for( int col = 0; col < n; ++col )
			{
				const size_t i = static_cast<size_t>( row ) * n + col;
				const Cell& wind = terrain[i];
				const float length = std::hypot( wind.x, wind.z );
				float upstream = factor[i];
				if( length > 1e-4f )
					upstream = sample( factor, row + ( wind.z / length ) * stepTexels, col - ( wind.x / length ) * stepTexels );
				const float rate = target[i] < upstream ? into : out;
				next[i] = upstream + ( target[i] - upstream ) * rate;
				if( last )
					drop[i] = std::max( upstream - target[i], 0.0f );
			}
		} );
		std::swap( factor, next );
	}
	result.resize( cells );
	forRows( size, [&]( uint32_t row )
	{
		for( int col = 0; col < n; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * n + col;
			const Cell& wind = terrain[i];
			const float f = factor[i];
			// Наветренная опушка: ветер с открытого упирается в полог — порывисто
			const float edge = fieldSmoothstep( 0.05f, 0.3f, drop[i] ) * settings.turbulence;
			result[i] = Cell( wind.x * f, wind.y * f, wind.z * f, std::max( wind.w, edge ) );
		}
	} );
}

std::string TerrainWind::summary( const std::vector<Cell>& field )
{
	if( field.empty() )
		return "no field";
	double sum = 0.0, vertical = 0.0, turbulence = 0.0;
	size_t fast = 0, calm = 0;
	float peak = 0.0f;
	for( const Cell& cell : field )
	{
		const float speed = std::hypot( cell.x, cell.z );
		sum += speed;
		vertical += std::fabs( cell.y );
		turbulence += cell.w;
		fast += speed > 1.2f ? 1 : 0;
		calm += speed < 0.5f ? 1 : 0;
		peak = std::max( peak, speed );
	}
	const double count = static_cast<double>( field.size() );
	char text[256];
	std::snprintf( text, sizeof( text ), "speed mean %.2f, max %.2f, faster than 1.2: %.1f%%, slower than 0.5: %.1f%%, |vertical| mean %.3f, "
				   "turbulence mean %.2f", sum / count, peak, fast / count * 100.0, calm / count * 100.0, vertical / count, turbulence / count );
	return text;
}

}
