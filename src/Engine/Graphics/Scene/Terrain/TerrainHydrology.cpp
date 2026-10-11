#include "TerrainHydrology.h"
#include "Utils\Fingerprint.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <cstdio>
#include <execution>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>

namespace GS
{

namespace
{

constexpr double fillEpsilon = 1e-4;	// уклон заполненных низин на шаг, м: из каждой клетки есть путь вниз к краю
constexpr double pi = 3.14159265358979323846;
// Врез ручья, вытекающего из озера, нарастает от порога не круче этого уклона: вода уходит из озера через порог, а не
// обрывом — иначе у берега вода ручья стояла бы на полметра ниже озера рядом
constexpr double outletSlope = 0.05;
// Уклон поверхности для скорости ручья — по уровню воды на столько метров вверх и вниз по сети: шаг узлов ~1 м, а плёсы и
// перекаты — через pool_spacing (6 м), окно короче него
constexpr double speedSpan = 2.0;
// Ступени русла: уклон участка — по дну на столько метров вверх и вниз по сети; длина уступа (стенки) вдоль течения, м
constexpr double stepSlopeSpan = 5.0;
constexpr double stepFace = 0.3;
// Валуны русла — только где дно не уже, м: в узком ручье у родника крупный камень загораживает русло
constexpr double boulderMinBedWidth = 1.5;
// Восемь соседей (строка, столбец) и расстояние в шагах — порядок как в Tools/erosion.py (при равном уклоне берётся первый)
constexpr int neighbourRow[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
constexpr int neighbourCol[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
const double neighbourDistance[8] = { std::sqrt( 2.0 ), 1.0, std::sqrt( 2.0 ), 1.0, 1.0, std::sqrt( 2.0 ), 1.0, std::sqrt( 2.0 ) };

struct Point
{
	double x = 0.0;	// столбец (дробный, центр клетки — +0,5)
	double y = 0.0;	// строка
};

// Случайные числа камней русла (splitmix64): последовательность — от положения отрезка, а не от номера узла, поэтому
// камень на своём месте и после правки других кривых
struct StoneRandom
{
	uint64_t state;
	explicit StoneRandom( uint64_t seed ) : state( seed ) {}
	uint32_t next()
	{
		uint64_t z = ( state += 0x9E3779B97F4A7C15ull );
		z = ( z ^ ( z >> 30 ) ) * 0xBF58476D1CE4E5B9ull;
		z = ( z ^ ( z >> 27 ) ) * 0x94D049BB133111EBull;
		return static_cast<uint32_t>( ( z ^ ( z >> 31 ) ) >> 32 );
	}
	double uniform() { return next() / 4294967296.0; }
	// Нормальное (Бокс — Мюллер)
	double gauss()
	{
		const double u = std::max( uniform(), 1e-12 );
		return std::sqrt( -2.0 * std::log( u ) ) * std::cos( 2.0 * 3.14159265358979323846 * uniform() );
	}
};

double smoothstep( double edge0, double edge1, double x )
{
	const double t = std::clamp( ( x - edge0 ) / ( edge1 - edge0 ), 0.0, 1.0 );
	return t * t * ( 3.0 - 2.0 * t );
}

// Сетка size × size, строки подряд
struct Grid
{
	int size = 0;
	std::vector<double> values;

	double at( int row, int col ) const { return values[static_cast<size_t>( row ) * size + col]; }
	// Билинейно в дробных (столбец, строка) — как sample_bilinear в Tools/carve_channels.py
	double sample( double x, double y ) const
	{
		x = std::clamp( x, 0.0, size - 1.001 );
		y = std::clamp( y, 0.0, size - 1.001 );
		const int x0 = static_cast<int>( x );
		const int y0 = static_cast<int>( y );
		const double fx = x - x0;
		const double fy = y - y0;
		const double top = at( y0, x0 ) * ( 1.0 - fx ) + at( y0, x0 + 1 ) * fx;
		const double bottom = at( y0 + 1, x0 ) * ( 1.0 - fx ) + at( y0 + 1, x0 + 1 ) * fx;
		return top * ( 1.0 - fy ) + bottom * fy;
	}
};

// Гладкий шум значений −1…1 по решётке с шагом 1 (хеш узлов, smoothstep между ними): неровность дна и бортов русла —
// одинаковая у клеток карты и точек детальной земли (аргумент — метры мира)
double valueNoise( double x, double y )
{
	auto hash = []( int64_t ix, int64_t iy )
	{
		uint64_t h = static_cast<uint64_t>( ix ) * 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>( iy ) * 0xC2B2AE3D27D4EB4Full;
		h ^= h >> 31;
		h *= 0xBF58476D1CE4E5B9ull;
		h ^= h >> 29;
		return static_cast<double>( h >> 11 ) / static_cast<double>( 1ull << 53 ) * 2.0 - 1.0;
	};
	const double fx = std::floor( x );
	const double fy = std::floor( y );
	const int64_t ix = static_cast<int64_t>( fx );
	const int64_t iy = static_cast<int64_t>( fy );
	const double u = smoothstep( 0.0, 1.0, x - fx );
	const double v = smoothstep( 0.0, 1.0, y - fy );
	const double bottom = hash( ix, iy ) + ( hash( ix + 1, iy ) - hash( ix, iy ) ) * u;
	const double top = hash( ix, iy + 1 ) + ( hash( ix + 1, iy + 1 ) - hash( ix, iy + 1 ) ) * u;
	return bottom + ( top - bottom ) * v;
}

// Бикубическая (Catmull-Rom) выборка сетки в дробных (столбец, строка) центров клеток: гладкий рельеф между клетками
// (детальная земля), проходит через значения клеток
double catmullRom( const Grid& grid, double x, double y )
{
	const int n = grid.size;
	const int x0 = static_cast<int>( std::floor( x ) );
	const int y0 = static_cast<int>( std::floor( y ) );
	const double fx = x - x0;
	const double fy = y - y0;
	auto weights = []( double f, double w[4] )
	{
		const double f2 = f * f;
		const double f3 = f2 * f;
		w[0] = -0.5 * f3 + f2 - 0.5 * f;
		w[1] = 1.5 * f3 - 2.5 * f2 + 1.0;
		w[2] = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
		w[3] = 0.5 * f3 - 0.5 * f2;
	};
	double wx[4];
	double wy[4];
	weights( fx, wx );
	weights( fy, wy );
	double result = 0.0;
	for( int j = 0; j < 4; ++j )
	{
		const int row = std::clamp( y0 - 1 + j, 0, n - 1 );
		double line = 0.0;
		for( int i = 0; i < 4; ++i )
			line += wx[i] * grid.at( row, std::clamp( x0 - 1 + i, 0, n - 1 ) );
		result += wy[j] * line;
	}
	return result;
}

// Priority-Flood с уклоном (Barnes, Lehman, Mulla 2014): обход от края по возрастанию, при равной высоте — по индексу.
// epsilon 0 — низины заполнены точно ровно (для поиска ровных мест, flowDirections). ascending — клетки в порядке обхода:
// по возрастанию заполненной высоты
std::vector<double> fillDepressions( const Grid& height, double epsilon = fillEpsilon, std::vector<int>* ascending = nullptr )
{
	const int n = height.size;
	std::vector<double> filled = height.values;
	std::vector<uint8_t> closed( filled.size(), 0 );
	using Entry = std::pair<double, int>;
	std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> heap;
	auto seed = [&]( int row, int col )
	{
		const int i = row * n + col;
		closed[i] = 1;
		heap.push( { height.values[i], i } );
	};
	for( int row = 0; row < n; ++row )
	{
		seed( row, 0 );
		seed( row, n - 1 );
	}
	for( int col = 1; col < n - 1; ++col )
	{
		seed( 0, col );
		seed( n - 1, col );
	}
	if( ascending )
	{
		ascending->clear();
		ascending->reserve( filled.size() );
	}
	while( !heap.empty() )
	{
		const auto [z, i] = heap.top();
		heap.pop();
		if( ascending )
			ascending->push_back( i );
		const int row = i / n;
		const int col = i % n;
		for( int k = 0; k < 8; ++k )
		{
			const int r = row + neighbourRow[k];
			const int c = col + neighbourCol[k];
			if( r < 0 || c < 0 || r >= n || c >= n )
				continue;
			const int j = r * n + c;
			if( closed[j] )
				continue;
			closed[j] = 1;
			const double value = height.values[j] > z + epsilon ? height.values[j] : z + epsilon;
			filled[j] = value;
			heap.push( { value, j } );
		}
	}
	return filled;
}

// Получатель стока D8 каждой клетки (сама себе — сток за край или в никуда); у края сосед — ближайшая клетка карты
std::vector<int> receivers( const std::vector<double>& filled, int n )
{
	std::vector<int> receiver( filled.size() );
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			const int i = row * n + col;
			double best = 0.0;
			int target = i;
			for( int k = 0; k < 8; ++k )
			{
				const int r = std::clamp( row + neighbourRow[k], 0, n - 1 );
				const int c = std::clamp( col + neighbourCol[k], 0, n - 1 );
				const double slope = ( filled[i] - filled[r * n + c] ) / neighbourDistance[k];
				if( slope > best )
				{
					best = slope;
					target = r * n + c;
				}
			}
			receiver[i] = target;
		}
	}
	return receiver;
}

// Клетки от высоких к низким: устойчивая сортировка по возрастанию, обход с конца (при равных — больший индекс раньше)
std::vector<int> downhillOrder( const std::vector<double>& filled )
{
	std::vector<int> order( filled.size() );
	std::iota( order.begin(), order.end(), 0 );
	std::stable_sort( order.begin(), order.end(), [&filled]( int a, int b ) { return filled[a] < filled[b]; } );
	std::reverse( order.begin(), order.end() );
	return order;
}

// Клетки «каждая раньше своего получателя» (Кан): порядок для накопления вниз по течению при любых получателях — и там,
// где получатель не ниже по заполненной карте (ровные места, flowDirections)
std::vector<int> upstreamOrder( const std::vector<int>& receiver )
{
	const size_t cells = receiver.size();
	std::vector<int> pending( cells, 0 );
	for( size_t i = 0; i < cells; ++i )
		if( receiver[i] != static_cast<int>( i ) )
			++pending[receiver[i]];
	std::vector<int> order;
	order.reserve( cells );
	for( size_t i = 0; i < cells; ++i )
		if( pending[i] == 0 )
			order.push_back( static_cast<int>( i ) );
	for( size_t head = 0; head < order.size(); ++head )
	{
		const int i = order[head];
		const int r = receiver[i];
		if( r != i && --pending[r] == 0 )
			order.push_back( r );
	}
	return order;
}

// Направления стока без прямых линий по сетке — для кривых русел и расхода (drainage).
// - Ровные места (клетки точно ровной заполненной карты level без соседа ниже: заполненные низины, дно долины) — как у
//   Barnes, Lehman, Mulla 2014 («An efficient assignment of drainage direction over flat surfaces in raster digital
//   elevation models»): к краю ровного места, откуда можно стечь (расстояние × 2), и прочь от высокого берега; вода
//   сходится к выходу по середине, а не идёт волной от порога прямыми под 0° и 45°.
// - Склоны — D8-LTD (Orlandini и др. 2003, «Path-based methods for the determination of nondispersive drainage directions
//   in grid-based digital elevation models»): направление уклона по граням (D∞, Tarboton 1997), из двух соседей грани —
//   тот, при котором накопленное поперечное отклонение пути от направления уклона меньше; отклонение несётся вниз от
//   главного притока (больший водосбор plainArea по обычному D8). Сток — в одну клетку, но путь в среднем идёт по склону,
//   а не вдоль строк и диагоналей.
// Получатель — сосед ниже по level (на ровном месте — того же уровня, с меньшей маской); сама клетка — сток за край.
// ascending — клетки по возрастанию level (обход Priority-Flood)
std::vector<int> flowDirections( const std::vector<double>& level, const std::vector<int>& ascending,
								 const std::vector<double>& plainArea, int n )
{
	const size_t cells = level.size();
	auto inside = [n]( int r, int c ) { return r >= 0 && c >= 0 && r < n && c < n; };
	auto onEdge = [n]( int r, int c ) { return r == 0 || c == 0 || r == n - 1 || c == n - 1; };

	// Ровные клетки: нет соседа ниже (у края — сток за карту, не ровная)
	std::vector<uint8_t> flat( cells, 0 );
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			if( onEdge( row, col ) )
				continue;
			const size_t i = static_cast<size_t>( row ) * n + col;
			bool lower = false;
			for( int k = 0; k < 8 && !lower; ++k )
				lower = level[static_cast<size_t>( row + neighbourRow[k] ) * n + col + neighbourCol[k]] < level[i];
			flat[i] = lower ? 0 : 1;
		}
	}

	// Barnes 2014: к выходу — обход от «нижних краёв» (не ровных клеток того же уровня рядом с ровными), прочь от берега —
	// от ровных клеток рядом с выше; у ровного места своё наибольшее расстояние от берега
	constexpr int unreached = -1;
	std::vector<int> towards( cells, unreached ), away( cells, unreached ), label( cells, -1 );
	std::vector<int> queue;
	for( int row = 1; row < n - 1; ++row )
	{
		for( int col = 1; col < n - 1; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * n + col;
			if( !flat[i] )
				continue;
			for( int k = 0; k < 8; ++k )
			{
				const size_t j = static_cast<size_t>( row + neighbourRow[k] ) * n + col + neighbourCol[k];
				if( !flat[j] && level[j] == level[i] && towards[j] == unreached )
				{
					towards[j] = 0;
					queue.push_back( static_cast<int>( j ) );
				}
			}
		}
	}
	for( size_t head = 0; head < queue.size(); ++head )
	{
		const int i = queue[head];
		const int row = i / n, col = i % n;
		for( int k = 0; k < 8; ++k )
		{
			const int r = row + neighbourRow[k], c = col + neighbourCol[k];
			if( !inside( r, c ) )
				continue;
			const size_t j = static_cast<size_t>( r ) * n + c;
			if( flat[j] && towards[j] == unreached && level[j] == level[i] )
			{
				towards[j] = towards[i] + 1;
				queue.push_back( static_cast<int>( j ) );
			}
		}
	}
	// Ровные места — связные области одного уровня; наибольшее расстояние от берега у каждой
	std::vector<int> farthest;
	for( size_t start = 0; start < cells; ++start )
	{
		if( !flat[start] || label[start] >= 0 )
			continue;
		const int id = static_cast<int>( farthest.size() );
		farthest.push_back( 0 );
		queue.assign( 1, static_cast<int>( start ) );
		label[start] = id;
		std::vector<int> high;
		for( size_t head = 0; head < queue.size(); ++head )
		{
			const int i = queue[head];
			const int row = i / n, col = i % n;
			bool nextToHigher = false;
			for( int k = 0; k < 8; ++k )
			{
				const size_t j = static_cast<size_t>( row + neighbourRow[k] ) * n + col + neighbourCol[k];
				if( level[j] > level[i] )
					nextToHigher = true;
				else if( flat[j] && label[j] < 0 && level[j] == level[i] )
				{
					label[j] = id;
					queue.push_back( static_cast<int>( j ) );
				}
			}
			if( nextToHigher )
				high.push_back( i );
		}
		for( int i : high )
			away[i] = 1;
		for( size_t head = 0; head < high.size(); ++head )
		{
			const int i = high[head];
			farthest[id] = std::max( farthest[id], away[i] );
			const int row = i / n, col = i % n;
			for( int k = 0; k < 8; ++k )
			{
				const size_t j = static_cast<size_t>( row + neighbourRow[k] ) * n + col + neighbourCol[k];
				if( flat[j] && label[j] == id && away[j] == unreached )
				{
					away[j] = away[i] + 1;
					high.push_back( static_cast<int>( j ) );
				}
			}
		}
	}
	// Маска ровного места: меньше — ближе к выходу и дальше от берега (у нижних краёв — −1: в них стекают)
	auto mask = [&]( size_t i ) -> double
	{
		if( !flat[i] )
			return -1.0;
		const int toward = towards[i] == unreached ? 1 << 20 : towards[i];
		const int fromHigh = away[i] == unreached ? 0 : farthest[label[i]] - away[i];
		return 2.0 * toward + fromHigh;
	};

	// Направление уклона склона по граням D∞ (Tarboton 1997) — у клеток независимо, параллельно: самая крутая грань, её соседи
	// по стороне e1 и по диагонали e2 и направление v (в клетках: x — столбец, y — строка). Без тригонометрии: внутри грани
	// v = s1 · e1 + s2 · (e2 − e1) (уклоны вдоль стороны и поперёк), у её сторон — e1 или e2
	constexpr int facetCardinal[8] = { 4, 1, 1, 3, 3, 6, 6, 4 };
	constexpr int facetDiagonal[8] = { 2, 2, 0, 0, 5, 5, 7, 7 };
	std::vector<int8_t> facetFirst( cells, -1 ), facetSecond( cells, -1 );
	std::vector<float> facetX( cells, 0.0f ), facetY( cells, 0.0f );
	std::vector<int> rows( n );
	std::iota( rows.begin(), rows.end(), 0 );
	std::for_each( std::execution::par, rows.begin(), rows.end(), [&]( int row )
	{
		for( int col = 0; col < n; ++col )
		{
			const size_t i = static_cast<size_t>( row ) * n + col;
			if( flat[i] )
				continue;
			double steepest = 0.0;
			for( int f = 0; f < 8; ++f )
			{
				const int k1 = facetCardinal[f], k2 = facetDiagonal[f];
				const int r1 = row + neighbourRow[k1], c1 = col + neighbourCol[k1];
				const int r2 = row + neighbourRow[k2], c2 = col + neighbourCol[k2];
				if( !inside( r1, c1 ) || !inside( r2, c2 ) )
					continue;
				const double z0 = level[i], z1 = level[static_cast<size_t>( r1 ) * n + c1], z2 = level[static_cast<size_t>( r2 ) * n + c2];
				const double s1 = z0 - z1, s2 = z1 - z2;
				const double ux = neighbourCol[k1], uy = neighbourRow[k1];
				const double px = neighbourCol[k2] - neighbourCol[k1], py = neighbourRow[k2] - neighbourRow[k1];
				double slope, dx, dy;
				if( s2 <= 0.0 )
				{
					slope = s1;
					dx = ux;
					dy = uy;
				}
				else if( s2 >= s1 )
				{
					slope = ( z0 - z2 ) / std::sqrt( 2.0 );
					dx = ( ux + px ) / std::sqrt( 2.0 );
					dy = ( uy + py ) / std::sqrt( 2.0 );
				}
				else
				{
					slope = std::sqrt( s1 * s1 + s2 * s2 );
					dx = ( s1 * ux + s2 * px ) / slope;
					dy = ( s1 * uy + s2 * py ) / slope;
				}
				if( slope <= steepest )
					continue;
				steepest = slope;
				facetFirst[i] = static_cast<int8_t>( k1 );
				facetSecond[i] = static_cast<int8_t>( k2 );
				facetX[i] = static_cast<float>( dx );
				facetY[i] = static_cast<float>( dy );
			}
		}
	} );

	// Обход сверху вниз по level: у склона — D8-LTD с отклонением, принесённым главным притоком
	std::vector<int> receiver( cells );
	std::vector<double> deviation( cells, 0.0 );		// накопленное отклонение, пришедшее в клетку, клетки
	std::vector<double> donorArea( cells, -1.0 );		// водосбор притока, от которого оно пришло
	for( auto it = ascending.rbegin(); it != ascending.rend(); ++it )
	{
		const int i = *it;
		const int row = i / n, col = i % n;
		receiver[i] = i;
		double carried = 0.0;
		if( flat[i] )
		{
			// Ровное место: сосед того же уровня с наименьшей маской (по шагу)
			double best = 0.0;
			const double here = mask( i );
			for( int k = 0; k < 8; ++k )
			{
				const size_t j = static_cast<size_t>( row + neighbourRow[k] ) * n + col + neighbourCol[k];
				if( level[j] != level[i] )
					continue;
				const double drop = ( here - mask( j ) ) / neighbourDistance[k];
				if( drop > best )
				{
					best = drop;
					receiver[i] = static_cast<int>( j );
				}
			}
		}
		else
		{
			// Склон: самая крутая грань — направление уклона v; соседи грани e1 (сторона), e2 (диагональ)
			const int first = facetFirst[i], second = facetSecond[i];
			const double vx = facetX[i], vy = facetY[i];
			int chosen = -1;
			if( first >= 0 )
			{
				// Поперечное отклонение шага от линии уклона (векторное произведение) — накопленное с пришедшим
				const double d1 = deviation[i] + ( vx * neighbourRow[first] - vy * neighbourCol[first] );
				const double d2 = deviation[i] + ( vx * neighbourRow[second] - vy * neighbourCol[second] );
				const size_t j1 = static_cast<size_t>( row + neighbourRow[first] ) * n + col + neighbourCol[first];
				const size_t j2 = static_cast<size_t>( row + neighbourRow[second] ) * n + col + neighbourCol[second];
				const bool lower1 = level[j1] < level[i], lower2 = level[j2] < level[i];
				if( lower1 && ( !lower2 || std::abs( d1 ) <= std::abs( d2 ) ) )
				{
					chosen = static_cast<int>( j1 );
					carried = d1;
				}
				else if( lower2 )
				{
					chosen = static_cast<int>( j2 );
					carried = d2;
				}
			}
			if( chosen < 0 )
			{
				// Грани не дали соседа ниже (край карты, седловина) — обычный D8
				double best = 0.0;
				for( int k = 0; k < 8; ++k )
				{
					const int r = row + neighbourRow[k], c = col + neighbourCol[k];
					if( !inside( r, c ) )
						continue;
					const size_t j = static_cast<size_t>( r ) * n + c;
					const double drop = ( level[i] - level[j] ) / neighbourDistance[k];
					if( drop > best )
					{
						best = drop;
						chosen = static_cast<int>( j );
					}
				}
				carried = 0.0;
			}
			if( chosen >= 0 )
				receiver[i] = chosen;
		}
		// Отклонение — получателю, если этот приток у него главный (больший водосбор); с ровного места — с нуля
		const int r = receiver[i];
		if( r != i && plainArea[i] > donorArea[r] )
		{
			donorArea[r] = plainArea[i];
			deviation[r] = carried;
		}
	}
	return receiver;
}

// Сумма вниз по D8: каждая клетка отдаёт накопленное получателю
std::vector<double> accumulate( std::vector<double> values, const std::vector<int>& receiver, const std::vector<int>& order )
{
	for( int i : order )
	{
		if( receiver[i] != i )
			values[receiver[i]] += values[i];
	}
	return values;
}

Grid boxBlur3( const Grid& grid )
{
	Grid result = grid;
	const int n = grid.size;
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			double sum = 0.0;
			for( int dy = -1; dy <= 1; ++dy )
				for( int dx = -1; dx <= 1; ++dx )
					sum += grid.at( std::clamp( row + dy, 0, n - 1 ), std::clamp( col + dx, 0, n - 1 ) );
			result.values[static_cast<size_t>( row ) * n + col] = sum / 9.0;
		}
	}
	return result;
}

// Уклон по numpy.gradient: в середине — центральные разности, у края — односторонние
double gradientSlope( const Grid& grid, int row, int col, double cell )
{
	const int n = grid.size;
	auto derivative = [&]( int index, int last, auto value )
	{
		if( index == 0 )
			return ( value( 1 ) - value( 0 ) ) / cell;
		if( index == last )
			return ( value( last ) - value( last - 1 ) ) / cell;
		return ( value( index + 1 ) - value( index - 1 ) ) / ( 2.0 * cell );
	};
	const double gx = derivative( col, n - 1, [&]( int c ) { return grid.at( row, c ); } );
	const double gy = derivative( row, n - 1, [&]( int r ) { return grid.at( r, col ); } );
	return std::hypot( gx, gy );
}

// Озёра: связные (по восьми соседям) области низин глубже depth, не меньше minCells клеток и с притоком внутри (туда
// течёт вода; сухая ямка на склоне озером не станет). Уровень озера — наименьший заполненный в области: поверхность
// ровная (заполнение с уклоном ε поднимает середину на доли миллиметра на клетку). Возвращает уровень по клеткам, м;
// не озеро — NaN
std::vector<double> lakeLevels( const std::vector<double>& filled, const std::vector<double>& height, const std::vector<double>& inflow,
								int n, double depth, double minCells )
{
	const size_t cells = filled.size();
	std::vector<double> level( cells, std::numeric_limits<double>::quiet_NaN() );
	std::vector<uint8_t> visited( cells, 0 );
	std::vector<int> component;
	std::vector<int> queue;
	for( size_t start = 0; start < cells; ++start )
	{
		if( visited[start] || filled[start] - height[start] <= depth )
			continue;
		component.clear();
		queue.assign( 1, static_cast<int>( start ) );
		visited[start] = 1;
		bool fed = false;
		double surface = std::numeric_limits<double>::infinity();
		while( !queue.empty() )
		{
			const int i = queue.back();
			queue.pop_back();
			component.push_back( i );
			fed = fed || inflow[i] > 0.0;
			surface = std::min( surface, filled[i] );
			const int row = i / n;
			const int col = i % n;
			for( int k = 0; k < 8; ++k )
			{
				const int r = row + neighbourRow[k];
				const int c = col + neighbourCol[k];
				if( r < 0 || c < 0 || r >= n || c >= n )
					continue;
				const int j = r * n + c;
				if( !visited[j] && filled[j] - height[j] > depth )
				{
					visited[j] = 1;
					queue.push_back( j );
				}
			}
		}
		if( fed && component.size() >= minCells )
			for( int i : component )
				level[i] = surface;
	}
	// Мелкая кромка (не глубже depth) — тоже озеро: всё, что связано с ним и ниже уровня в той же низине (заполнено почти
	// до уровня: уклон заполнения — доли миллиметра на клетку). Иначе на плоском дне плоскость воды на клетку выходила бы
	// за маску и обрывалась ступенькой по клеткам над рельефом ниже уровня. Сток из низины (заполнен ниже) не заливается
	constexpr double shoreTolerance = 0.01;
	queue.clear();
	for( size_t i = 0; i < cells; ++i )
		if( !std::isnan( level[i] ) )
			queue.push_back( static_cast<int>( i ) );
	while( !queue.empty() )
	{
		const int i = queue.back();
		queue.pop_back();
		const int row = i / n;
		const int col = i % n;
		for( int k = 0; k < 8; ++k )
		{
			const int r = row + neighbourRow[k];
			const int c = col + neighbourCol[k];
			if( r < 0 || c < 0 || r >= n || c >= n )
				continue;
			const int j = r * n + c;
			if( std::isnan( level[j] ) && height[j] < level[i] && filled[j] >= level[i] - shoreTolerance )
			{
				level[j] = level[i];
				queue.push_back( j );
			}
		}
	}
	return level;
}

// Чаша озера: у настоящего озера за узкой отмелью (литоралью) — свал к глубине, которая растёт с площадью, а не
// сантиметры воды над плоским дном. Уровень озера не меняется (его задаёт порог перелива), опускается только дно: по
// расстоянию от берега s — отмель до shelfDepth на ширине shelfWidth, дальше свал dropSlope м на м, не глубже
// clamp(depthRatio · √площади, minDepth, maxDepth) с плавным скруглением. Где дно и так глубже — не трогается.
// Возвращает опускание дна по клеткам, м (≥ 0)
std::vector<double> lakeBasins( const std::vector<double>& level, const std::vector<double>& height, int n, double cell,
								const WaterChannelsSettings& p )
{
	const size_t cells = level.size();
	std::vector<double> lowering( cells, 0.0 );
	// Расстояние до берега (до ближайшей клетки не озера), м: два прохода фаски (1, √2)
	constexpr double unreached = 1e30;
	std::vector<double> shore( cells, 0.0 );
	for( size_t i = 0; i < cells; ++i )
		shore[i] = std::isnan( level[i] ) ? 0.0 : unreached;
	const double diagonal = std::sqrt( 2.0 );
	auto relax = [&]( int row, int col, int dr, int dc, double step )
	{
		const int r = row + dr;
		const int c = col + dc;
		double& value = shore[static_cast<size_t>( row ) * n + col];
		const double other = r < 0 || c < 0 || r >= n || c >= n ? 0.0 : shore[static_cast<size_t>( r ) * n + c];
		value = std::min( value, other + step );
	};
	for( int row = 0; row < n; ++row )
	{
		for( int col = 0; col < n; ++col )
		{
			if( shore[static_cast<size_t>( row ) * n + col] == 0.0 )
				continue;
			relax( row, col, -1, -1, diagonal );
			relax( row, col, -1, 0, 1.0 );
			relax( row, col, -1, 1, diagonal );
			relax( row, col, 0, -1, 1.0 );
		}
	}
	for( int row = n - 1; row >= 0; --row )
	{
		for( int col = n - 1; col >= 0; --col )
		{
			if( shore[static_cast<size_t>( row ) * n + col] == 0.0 )
				continue;
			relax( row, col, 1, 1, diagonal );
			relax( row, col, 1, 0, 1.0 );
			relax( row, col, 1, -1, diagonal );
			relax( row, col, 0, 1, 1.0 );
		}
	}
	// Озёра — связные области одного уровня; площадь — по числу клеток
	std::vector<int> label( cells, -1 );
	std::vector<double> maxDepth;
	std::vector<int> queue, component;
	for( size_t start = 0; start < cells; ++start )
	{
		if( std::isnan( level[start] ) || label[start] >= 0 )
			continue;
		const int id = static_cast<int>( maxDepth.size() );
		component.clear();
		queue.assign( 1, static_cast<int>( start ) );
		label[start] = id;
		while( !queue.empty() )
		{
			const int i = queue.back();
			queue.pop_back();
			component.push_back( i );
			for( int k = 0; k < 8; ++k )
			{
				const int r = i / n + neighbourRow[k];
				const int c = i % n + neighbourCol[k];
				if( r < 0 || c < 0 || r >= n || c >= n )
					continue;
				const int j = r * n + c;
				if( label[j] < 0 && !std::isnan( level[j] ) && level[j] == level[i] )
				{
					label[j] = id;
					queue.push_back( j );
				}
			}
		}
		const double area = component.size() * cell * cell;
		maxDepth.push_back( std::clamp( p.lakeDepthRatio * std::sqrt( area ), static_cast<double>( p.lakeMinDepth ),
										static_cast<double>( std::max( p.lakeMaxDepth, p.lakeMinDepth ) ) ) );
	}
	const double shelfWidth = std::max( static_cast<double>( p.lakeShelfWidth ), 1e-3 );
	std::vector<double> depth( cells, 0.0 );
	for( size_t i = 0; i < cells; ++i )
	{
		if( label[i] < 0 )
			continue;
		const double s = shore[i] * cell;
		const double raw = s < shelfWidth ? p.lakeShelfDepth * s / shelfWidth : p.lakeShelfDepth + ( s - shelfWidth ) * p.lakeDropSlope;
		// Плавный минимум с наибольшей глубиной: дно скругляется, а не обрывается в плоскость
		const double limit = maxDepth[label[i]];
		const double k = 0.25 * limit;
		const double h = std::clamp( 0.5 + 0.5 * ( limit - raw ) / k, 0.0, 1.0 );
		depth[i] = raw * h + limit * ( 1.0 - h ) - k * h * ( 1.0 - h );
	}
	// Глубина чаши — сглаженная (два прохода 3 × 3, вне озера — 0): по клеткам у первой клетки озера сразу отмель на
	// клетку (0,17 м), у соседней вне озера — 0, и узкий рукав по диагонали становился лесенкой ступеней по 16 см — урез
	// и граница отмели шли по краям клеток. Сглаженная — скат от берега, а узкий рукав остаётся почти на своём дне
	for( int pass = 0; pass < 2; ++pass )
	{
		std::vector<double> blurred( cells, 0.0 );
		for( int row = 0; row < n; ++row )
		{
			for( int col = 0; col < n; ++col )
			{
				const size_t i = static_cast<size_t>( row ) * n + col;
				if( label[i] < 0 )
					continue;
				double sum = 0.0;
				for( int dr = -1; dr <= 1; ++dr )
					for( int dc = -1; dc <= 1; ++dc )
					{
						const int r = row + dr;
						const int c = col + dc;
						if( r >= 0 && c >= 0 && r < n && c < n )
							sum += depth[static_cast<size_t>( r ) * n + c];
					}
				blurred[i] = sum / 9.0;
			}
		}
		depth = std::move( blurred );
	}
	for( size_t i = 0; i < cells; ++i )
		if( label[i] >= 0 )
			lowering[i] = std::max( height[i] - ( level[i] - depth[i] ), 0.0 );
	return lowering;
}

}

namespace
{

Grid gridMetres( const HeightField& field )
{
	const int n = static_cast<int>( field.size );
	Grid height;
	height.size = n;
	height.values.resize( static_cast<size_t>( n ) * n );
	for( int row = 0; row < n; ++row )
		for( int col = 0; col < n; ++col )
			height.values[static_cast<size_t>( row ) * n + col] =
				static_cast<double>( field.heights[row * field.rowPitch + col] ) * field.heightMultiplier + field.heightOffset;
	return height;
}

}

namespace
{

// Сток и озёра по итоговому рельефу — общее для генерации кривых и постройки русел по ним
struct Drainage
{
	Grid height;
	std::vector<double> filled;
	std::vector<int> receiver;
	std::vector<int> order;
	std::vector<double> area;		// водосбор, м²
	std::vector<double> inflow;		// приток клетки, м³/с
	std::vector<double> flow;		// расход, м³/с
	std::vector<double> lakeLevel;	// уровень озера, м; NaN — не озеро
	std::vector<uint8_t> lake;
};

Drainage drainage( const HeightField& field, const WaterSimulationSettings& water )
{
	const WaterChannelsSettings& p = water.channels;
	const int n = static_cast<int>( field.size );
	const size_t cells = static_cast<size_t>( n ) * n;
	const double cell = field.texelSize;
	Drainage d;
	d.height = gridMetres( field );

	// 1. Сток по заполненной карте и водосбор: направления без прямых по сетке (flowDirections — по точно ровной заполненной
	// карте; главный приток — по водосбору обычного D8), порядок накопления — по получателям
	std::vector<int> ascending;
	d.filled = fillDepressions( d.height, fillEpsilon, &ascending );
	const std::vector<int> plainReceiver = receivers( d.filled, n );
	// Обход Priority-Flood — по возрастанию заполненной высоты: с конца — сверху вниз, как downhillOrder
	const std::vector<int> plainOrder( ascending.rbegin(), ascending.rend() );
	const std::vector<double> plainArea = accumulate( std::vector<double>( cells, cell * cell ), plainReceiver, plainOrder );
	const std::vector<double> exact = fillDepressions( d.height, 0.0, &ascending );
	d.receiver = flowDirections( exact, ascending, plainArea, n );
	d.order = upstreamOrder( d.receiver );
	d.area = accumulate( std::vector<double>( cells, cell * cell ), d.receiver, d.order );

	// 2. Приток: доля «русла» по водосбору и помощники, весь расход помощника — в клетку центра; расход вниз по течению
	d.inflow.resize( cells );
	const double logStart = std::log10( std::max( water.flowStart, 1.0f ) );
	const double logFull = std::log10( std::max( water.flowFull, 1.0f ) );
	for( size_t i = 0; i < cells; ++i )
		d.inflow[i] = smoothstep( logStart, logFull, std::log10( std::max( d.area[i], 1.0 ) ) ) * ( water.sourceRate / 1000.0 );
	for( const WaterSource& source : water.sources )
	{
		const int col = static_cast<int>( std::clamp( source.position.x / cell, 0.0, n - 1.0 ) );
		const int row = static_cast<int>( std::clamp( ( n * cell - source.position.y ) / cell, 0.0, n - 1.0 ) );
		d.inflow[static_cast<size_t>( row ) * n + col] += source.rate / 1000.0;
	}
	d.flow = accumulate( d.inflow, d.receiver, d.order );

	// Низины, которые заполнит вода (озёра), не режутся: русло кончается у берега. Низина меньше minLakeArea — не озеро, а
	// ямка на дне ручья (извилина, тальвег, кривая правки рельефа): ручей идёт сквозь неё, иначе лента обрывалась бы у
	// каждой ямки, а в ямке стояла бы своя плоская вода
	d.lakeLevel = lakeLevels( d.filled, d.height.values, d.inflow, n, p.lakeDepth, p.minLakeArea / ( cell * cell ) );
	d.lake.resize( cells );
	for( size_t i = 0; i < cells; ++i )
		d.lake[i] = std::isnan( d.lakeLevel[i] ) ? 0 : 1;
	return d;
}

// Клетка карты под точкой (столбец, строка — дробные)
int cellIndex( const Point& point, int n )
{
	const int col = std::clamp( static_cast<int>( point.x ), 0, n - 1 );
	const int row = std::clamp( static_cast<int>( point.y ), 0, n - 1 );
	return row * n + col;
}

// Наибольшее значение в клетках 3 × 3 вокруг точки: расход и водосбор под ручной кривой, которая легла рядом со стоком D8
double sampleMax( const std::vector<double>& values, const Point& point, int n )
{
	const int col = std::clamp( static_cast<int>( point.x ), 0, n - 1 );
	const int row = std::clamp( static_cast<int>( point.y ), 0, n - 1 );
	double result = 0.0;
	for( int dy = -1; dy <= 1; ++dy )
		for( int dx = -1; dx <= 1; ++dx )
			result = std::max( result, values[static_cast<size_t>( std::clamp( row + dy, 0, n - 1 ) ) * n + std::clamp( col + dx, 0, n - 1 )] );
	return result;
}

// Расстояние от точки до ломаной и ближайшая точка на ней (в единицах точек)
double distanceToPolyline( const Point& point, const std::vector<Point>& line, Point* nearest = nullptr )
{
	double best = std::numeric_limits<double>::infinity();
	for( size_t i = 0; i + 1 < line.size(); ++i )
	{
		const double sx = line[i + 1].x - line[i].x;
		const double sy = line[i + 1].y - line[i].y;
		const double length2 = std::max( sx * sx + sy * sy, 1e-12 );
		const double t = std::clamp( ( ( point.x - line[i].x ) * sx + ( point.y - line[i].y ) * sy ) / length2, 0.0, 1.0 );
		const Point on = { line[i].x + t * sx, line[i].y + t * sy };
		const double distance = std::hypot( point.x - on.x, point.y - on.y );
		if( distance < best )
		{
			best = distance;
			if( nearest )
				*nearest = on;
		}
	}
	if( line.size() == 1 )
	{
		best = std::hypot( point.x - line[0].x, point.y - line[0].y );
		if( nearest )
			*nearest = line[0];
	}
	return best;
}

}

std::vector<float> TerrainHydrology::catchment( const HeightField& field )
{
	if( field.size < 3 )
		return {};
	const Grid height = gridMetres( field );
	const std::vector<double> filled = fillDepressions( height );
	const std::vector<int> receiver = receivers( filled, height.size );
	const double cellArea = static_cast<double>( field.texelSize ) * field.texelSize;
	const std::vector<double> area = accumulate( std::vector<double>( filled.size(), cellArea ), receiver, downhillOrder( filled ) );
	return std::vector<float>( area.begin(), area.end() );
}

std::string TerrainHydrology::generationKey( const HeightField& field, const WaterSimulationSettings& water )
{
	// Кривые — данные, как Landscape Splines: отпечаток — только то, что лежит в базе (размеры карты, приток, помощники,
	// параметры генератора), без высот рельефа. Рельеф после эрозии на GPU и исходная карта у разных машин чуть разные — от
	// высот кривые генерировались бы заново на каждой машине и переписывали базу. Сменился рельеф — предупреждение «идёт
	// в гору» (build), генерация заново — Tools/stream_edit.py regenerate
	Fingerprint key;
	// v3 — приток кончается у оси главного, а не в узле слияния по стоку D8 (ось главного сдвинута извилинами); v4 — сток
	// без прямых по сетке (flowDirections: Barnes 2014 на ровных местах, D8-LTD на склонах); v5 — размах извилин по зазору
	// до чужого ручья (русла не пересекаются); v6 — соседние ветви в клетку-две сливаются (русла не ложатся одно на другое)
	const char version[] = "streams-v6";
	key.add( version, sizeof( version ) );
	key.add( field.size );
	key.add( field.texelSize );
	key.add( field.heightMultiplier );
	key.add( field.heightOffset );
	key.add( water.sourceRate );
	key.add( water.flowStart );
	key.add( water.flowFull );
	for( const WaterSource& source : water.sources )
	{
		key.add( source.position );
		key.add( source.rate );
	}
	const WaterChannelsSettings& p = water.channels;
	for( float value : { p.minDischarge, p.meanderLength, p.meanderAmplitude, p.meanderMaxSlope, p.lakeDepth, p.minLakeArea } )
		key.add( value );
	key.add( p.smooth );
	char text[32];
	std::snprintf( text, sizeof( text ), "%016llx", static_cast<unsigned long long>( key.value ) );
	return text;
}

bool TerrainHydrology::generate( const HeightField& field, const WaterSimulationSettings& water, std::vector<StreamCurve>& curves )
{
	const WaterChannelsSettings& p = water.channels;
	const int n = static_cast<int>( field.size );
	if( n < 3 )
		return false;
	const size_t cells = static_cast<size_t>( n ) * n;
	const double cell = field.texelSize;
	const Drainage dr = drainage( field, water );

	// Соседние ручьи сливаются: сток в одну клетку (flowDirections) оставляет параллельные ветви в клетку-две друг от друга,
	// и их русла легли бы одно на другое. Клетка русла ближе mergeReach клеток к клетке другой ветви с не меньшим расходом,
	// куда вода может стечь (не выше), впадает в неё; без петель (та ветвь не течёт обратно в эту) и не в свой же путь.
	// Расход — заново по новой сети: ниже места слияния прежняя ветвь теряет приток и под порогом перестаёт быть руслом
	std::vector<int> receiver = dr.receiver;
	std::vector<double> flow = dr.flow;
	{
		constexpr int mergeReach = 2;
		constexpr int ownPath = 8;		// шагов вниз — свой путь (сливаться не с чем)
		auto reaches = [&]( int from, int target, int limit )
		{
			for( int i = from, steps = 0; steps < limit; ++steps )
			{
				if( i == target )
					return true;
				const int next = receiver[i];
				if( next == i )
					return false;
				i = next;
			}
			return false;
		};
		const int unlimited = static_cast<int>( cells );
		for( int a : dr.order )
		{
			if( dr.flow[a] <= p.minDischarge || dr.lake[a] || receiver[a] == a )
				continue;
			const int row = a / n, col = a % n;
			int best = -1;
			double bestDistance = mergeReach + 0.5;
			for( int dy = -mergeReach; dy <= mergeReach; ++dy )
			{
				for( int dx = -mergeReach; dx <= mergeReach; ++dx )
				{
					const int r = row + dy, c = col + dx;
					if( r < 0 || c < 0 || r >= n || c >= n || ( dx == 0 && dy == 0 ) )
						continue;
					const int b = r * n + c;
					const double distance = std::hypot( dx, dy );
					if( distance >= bestDistance || dr.flow[b] <= p.minDischarge || dr.flow[b] < dr.flow[a] ||
						dr.filled[b] > dr.filled[a] || reaches( a, b, ownPath ) || reaches( b, a, unlimited ) )
						continue;
					best = b;
					bestDistance = distance;
				}
			}
			if( best >= 0 )
				receiver[a] = best;
		}
		flow = accumulate( dr.inflow, receiver, upstreamOrder( receiver ) );
	}
	const std::vector<int> order = upstreamOrder( receiver );

	// 3. Узлы оси: клетки с расходом выше порога, вниз — получатель, вверх — главный приток (наибольший расход)
	std::vector<int> stream;
	std::vector<int> nodeOf( cells, -1 );
	for( size_t i = 0; i < cells; ++i )
	{
		if( flow[i] > p.minDischarge )
		{
			nodeOf[i] = static_cast<int>( stream.size() );
			stream.push_back( static_cast<int>( i ) );
		}
	}
	const int count = static_cast<int>( stream.size() );
	std::vector<Point> position( count );
	std::vector<int> down( count, -1 );
	std::vector<double> q( count );
	for( int k = 0; k < count; ++k )
	{
		const int i = stream[k];
		position[k] = { i % n + 0.5, i / n + 0.5 };
		down[k] = receiver[i] != i ? nodeOf[receiver[i]] : -1;
		q[k] = flow[i];
	}
	std::vector<int> up( count, -1 );
	{
		std::vector<double> upFlow( count, 0.0 );
		for( int k = 0; k < count; ++k )
		{
			const int d = down[k];
			if( d >= 0 && q[k] > upFlow[d] )
			{
				upFlow[d] = q[k];
				up[d] = k;
			}
		}
	}
	// Сглаживание вдоль главного притока: боковые притоки на ось не тянут
	for( int pass = 0; pass < p.smooth; ++pass )
	{
		std::vector<Point> smoothed = position;
		for( int k = 0; k < count; ++k )
		{
			if( down[k] < 0 || up[k] < 0 )
				continue;
			smoothed[k].x = 0.25 * position[up[k]].x + 0.5 * position[k].x + 0.25 * position[down[k]].x;
			smoothed[k].y = 0.25 * position[up[k]].y + 0.5 * position[k].y + 0.25 * position[down[k]].y;
		}
		position = std::move( smoothed );
	}
	// Узлы от истоков вниз — в порядке накопления стока (каждый раньше получателя; на ровном месте получатель не ниже)
	std::vector<int> rank( cells );
	for( size_t position = 0; position < order.size(); ++position )
		rank[order[position]] = static_cast<int>( position );
	std::vector<int> nodeOrder( count );
	std::iota( nodeOrder.begin(), nodeOrder.end(), 0 );
	std::stable_sort( nodeOrder.begin(), nodeOrder.end(), [&]( int a, int b ) { return rank[stream[a]] < rank[stream[b]]; } );

	// Извилины: сдвиг поперёк течения волной вдоль пути от истока, амплитуда растёт с расходом и гаснет на уклоне
	// (ось, сдвинутая с ложбины на склон, легла бы бортом под уровень воды) и у истока и устья. Размах — не больше
	// meanderClearance зазора до ближайшего узла чужого ручья (не своего пути на meanderRelated узлов вверх и вниз): сдвиги
	// у двух близких ручьёв навстречу друг другу оставляют между ними зазор, и русла не переезжают одно через другое
	{
		constexpr double meanderClearance = 0.35;
		constexpr int meanderRelated = 12;
		const double reach = p.meanderAmplitude / cell / meanderClearance + 1.0;	// дальше зазор размах не ограничит, клетки
		const int radius = static_cast<int>( std::ceil( reach ) );
		std::vector<double> clearance( count, reach );
		std::vector<int> related;
		for( int k = 0; k < count; ++k )
		{
			related.clear();
			for( int j = down[k], steps = 0; j >= 0 && steps < meanderRelated; j = down[j], ++steps )
				related.push_back( j );
			for( int j = up[k], steps = 0; j >= 0 && steps < meanderRelated; j = up[j], ++steps )
				related.push_back( j );
			const int row = stream[k] / n, col = stream[k] % n;
			for( int dy = -radius; dy <= radius; ++dy )
			{
				for( int dx = -radius; dx <= radius; ++dx )
				{
					const int r = row + dy, c = col + dx;
					if( r < 0 || c < 0 || r >= n || c >= n )
						continue;
					const int j = nodeOf[static_cast<size_t>( r ) * n + c];
					if( j < 0 || j == k || std::find( related.begin(), related.end(), j ) != related.end() )
						continue;
					clearance[k] = std::min( clearance[k], std::hypot( position[j].x - position[k].x, position[j].y - position[k].y ) );
				}
			}
		}
		std::vector<double> distance( count, 0.0 );
		for( int k : nodeOrder )
		{
			const int d = down[k];
			if( d >= 0 && up[d] == k )
				distance[d] = distance[k] + std::hypot( position[d].x - position[k].x, position[d].y - position[k].y );
		}
		Grid smooth = dr.height;
		for( int pass = 0; pass < 3; ++pass )
			smooth = boxBlur3( smooth );
		std::vector<Point> shifted = position;
		for( int k = 0; k < count; ++k )
		{
			const Point& previous = position[up[k] >= 0 ? up[k] : k];
			const Point& following = position[down[k] >= 0 ? down[k] : k];
			double tx = following.x - previous.x;
			double ty = following.y - previous.y;
			const double length = std::max( std::hypot( tx, ty ), 1e-6 );
			tx /= length;
			ty /= length;
			const double phase = 2.0 * pi * distance[k] / p.meanderLength;
			const double wave = std::sin( phase ) + 0.5 * std::sin( 2.3 * phase + 1.7 );
			double amplitude = std::min( p.meanderAmplitude * std::sqrt( q[k] / 0.1 ), static_cast<double>( p.meanderAmplitude ) ) / cell;
			const int i = stream[k];
			amplitude *= std::clamp( 1.0 - gradientSlope( smooth, i / n, i % n, cell ) / p.meanderMaxSlope, 0.0, 1.0 );
			amplitude = std::min( amplitude, meanderClearance * clearance[k] );
			const double edge = down[k] < 0 ? 0.0 : std::min( distance[k] / p.meanderLength, 1.0 );
			const double offset = wave * amplitude * edge;
			shifted[k].x = position[k].x - ty * offset;
			shifted[k].y = position[k].y + tx * offset;
		}
		position = std::move( shifted );
	}

	// Кривые — цепочки узлов от истока (или первого узла ниже озера) до слияния (узел главного — последняя точка), озера
	// (узел в озере — последняя) или края; дальше слияния течёт главный ручей
	curves.clear();
	const double world = n * cell;
	std::vector<uint8_t> confluenceEnd;	// у кривой — конец на слиянии (не в озере и не у края)
	for( int k : nodeOrder )
	{
		const bool inLake = dr.lake[stream[k]] != 0;
		if( inLake || ( up[k] >= 0 && !dr.lake[stream[up[k]]] ) )
			continue;
		std::vector<int> chain = { k };
		while( true )
		{
			const int d = down[chain.back()];
			if( d < 0 )
				break;
			chain.push_back( d );
			if( up[d] != chain[chain.size() - 2] || dr.lake[stream[d]] )
				break;
		}
		// Приток: узлы — на клетках стока D8, а ось главного сглажена и сдвинута извилинами (притоки на неё не тянут), поэтому
		// приток мог пересечь ось главного раньше узла слияния и уйти за неё — его врез выедал противоположный берег. Приток
		// кончается у первой своей точки ближе confluenceReach к оси главного — в ближайшем к ней узле главного
		const int last = chain.back();
		const bool atConfluence = chain.size() >= 2 && up[last] >= 0 && up[last] != chain[chain.size() - 2] && !dr.lake[stream[last]];
		if( atConfluence )
		{
			constexpr double confluenceReach = 1.5;	// клетки
			constexpr int mainReach = 12;				// узлов главного вверх и вниз от узла слияния
			std::vector<int> mainNodes = { last };
			// Узлы в озере — не точки кривых (кривая кончается первым из них): только вне озера
			for( int k2 = up[last], steps = 0; k2 >= 0 && steps < mainReach && !dr.lake[stream[k2]]; k2 = up[k2], ++steps )
				mainNodes.insert( mainNodes.begin(), k2 );
			for( int k2 = down[last], steps = 0; k2 >= 0 && steps < mainReach && !dr.lake[stream[k2]]; k2 = down[k2], ++steps )
				mainNodes.push_back( k2 );
			std::vector<Point> mainLine;
			for( int node : mainNodes )
				mainLine.push_back( position[node] );
			for( size_t j = 1; j + 1 < chain.size(); ++j )
			{
				if( distanceToPolyline( position[chain[j]], mainLine ) >= confluenceReach )
					continue;
				int nearest = last;
				double best = std::numeric_limits<double>::infinity();
				for( int node : mainNodes )
				{
					const double distance = std::hypot( position[node].x - position[chain[j]].x, position[node].y - position[chain[j]].y );
					if( distance < best )
					{
						best = distance;
						nearest = node;
					}
				}
				chain.resize( j );
				chain.push_back( nearest );
				break;
			}
		}
		if( chain.size() < 2 )
			continue;
		StreamCurve& curve = curves.emplace_back();
		curve.name = "Stream " + std::to_string( curves.size() );
		for( int node : chain )
		{
			StreamCurvePoint& point = curve.points.emplace_back();
			point.position = DirectX::XMFLOAT2( static_cast<float>( position[node].x * cell ), static_cast<float>( world - position[node].y * cell ) );
			point.discharge = static_cast<float>( q[node] );
		}
		confluenceEnd.push_back( atConfluence ? 1 : 0 );
	}
	// Обрезка каскадом: приток мог кончаться узлом другого притока, который сам обрезан у своего главного, — такой конец
	// уже не точка ни одной кривой (build оставил бы его обрывом). Он переносится в ближайшую точку другой кривой в трёх
	// клетках
	const float snapReach = static_cast<float>( 3.0 * cell );
	for( size_t a = 0; a < curves.size(); ++a )
	{
		if( !confluenceEnd[a] )
			continue;
		DirectX::XMFLOAT2& end = curves[a].points.back().position;
		bool joined = false;
		float best = snapReach;
		DirectX::XMFLOAT2 nearest = end;
		for( size_t b = 0; b < curves.size() && !joined; ++b )
		{
			if( a == b )
				continue;
			for( const StreamCurvePoint& point : curves[b].points )
			{
				const float distance = std::hypot( point.position.x - end.x, point.position.y - end.y );
				if( distance < 1e-3f )
				{
					joined = true;
					break;
				}
				if( distance < best )
				{
					best = distance;
					nearest = point.position;
				}
			}
		}
		if( !joined )
			end = nearest;
	}
	return true;
}

std::vector<StreamCurve> TerrainHydrology::merge( const std::vector<StreamCurve>& generated, const std::vector<StreamCurve>& existing,
												  const WaterChannelsSettings& channels )
{
	// Правленные и ручные — как есть
	std::vector<StreamCurve> result;
	for( const StreamCurve& curve : existing )
		if( !curve.generated || curve.edited )
			result.push_back( curve );
	struct Corridor
	{
		std::vector<Point> line;
		double radius = 0.0;
	};
	std::vector<Corridor> corridors;
	for( const StreamCurve& curve : result )
	{
		if( !curve.enabled )
			continue;
		Corridor& corridor = corridors.emplace_back();
		for( const StreamCurvePoint& point : curve.points )
			corridor.line.push_back( { point.position.x, point.position.y } );
		corridor.radius = 0.5 * channels.minWidth * curve.widthScale + 3.0;
	}
	// Сгенерированные — без участков в полосе правленных: остаток кончается на правленной (примыкает к ней)
	int index = 0;
	for( const StreamCurve& curve : generated )
	{
		// В полосе правленной; nearest — ближайшая точка её кривой (вершина: конец обрезанной совпадёт с узлом правленной)
		auto covered = [&]( const StreamCurvePoint& point, Point* nearest )
		{
			const Point at = { point.position.x, point.position.y };
			for( const Corridor& corridor : corridors )
			{
				if( distanceToPolyline( at, corridor.line ) >= corridor.radius )
					continue;
				double best = std::numeric_limits<double>::infinity();
				for( const Point& vertex : corridor.line )
				{
					const double distance = std::hypot( vertex.x - at.x, vertex.y - at.y );
					if( distance < best )
					{
						best = distance;
						*nearest = vertex;
					}
				}
				return true;
			}
			return false;
		};
		StreamCurve piece = curve;
		piece.points.clear();
		auto flush = [&]()
		{
			if( piece.points.size() >= 2 )
			{
				piece.name = "Stream " + std::to_string( ++index );
				result.push_back( piece );
			}
			piece.points.clear();
		};
		for( const StreamCurvePoint& point : curve.points )
		{
			Point nearest;
			if( !covered( point, &nearest ) )
			{
				piece.points.push_back( point );
				continue;
			}
			// Вошла в полосу правленной: конец — в ближайшей точке её кривой
			if( !piece.points.empty() )
			{
				StreamCurvePoint end = point;
				end.position = DirectX::XMFLOAT2( static_cast<float>( nearest.x ), static_cast<float>( nearest.y ) );
				piece.points.push_back( end );
			}
			flush();
		}
		flush();
	}
	return result;
}

bool TerrainHydrology::build( const HeightField& field, const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves,
							  Result& result )
{
	const WaterChannelsSettings& p = water.channels;
	const int n = static_cast<int>( field.size );
	if( n < 3 )
		return false;
	const size_t cells = static_cast<size_t>( n ) * n;
	const double cell = field.texelSize;
	const double world = n * cell;
	const Drainage dr = drainage( field, water );
	const Grid& height = dr.height;
	const std::vector<double>& lakeLevel = dr.lakeLevel;
	std::vector<uint8_t> lake = dr.lake;	// до заливки по итоговому уровню — низины заполнения
	result.largestDischarge = static_cast<float>( *std::max_element( dr.flow.begin(), dr.flow.end() ) );
	result.warnings.clear();

	// 3. Узлы оси — по кривым: точки в клетках, длинные отрезки (ручная кривая) — через клетку. Последняя точка, совпавшая
	// с точкой другой кривой (сгенерированный приток кончается узлом главного), — это узел той кривой
	struct CurvePoints
	{
		const StreamCurve* curve = nullptr;
		std::vector<Point> points;
		std::vector<double> discharge;
		int target = -1;			// последняя точка — точка этой кривой …
		size_t targetPoint = 0;		// … с этим номером
	};
	std::vector<CurvePoints> lines;
	for( const StreamCurve& curve : curves )
	{
		if( !curve.enabled || curve.points.size() < 2 )
			continue;
		CurvePoints& line = lines.emplace_back();
		line.curve = &curve;
		for( size_t i = 0; i < curve.points.size(); ++i )
		{
			const Point point = { curve.points[i].position.x / cell, ( world - curve.points[i].position.y ) / cell };
			if( !line.points.empty() )
			{
				const Point previous = line.points.back();
				const double previousDischarge = line.discharge.back();
				const int steps = static_cast<int>( std::ceil( std::hypot( point.x - previous.x, point.y - previous.y ) / 1.5 ) );
				for( int s = 1; s < steps; ++s )
				{
					const double f = static_cast<double>( s ) / steps;
					line.points.push_back( { previous.x + ( point.x - previous.x ) * f, previous.y + ( point.y - previous.y ) * f } );
					line.discharge.push_back( previousDischarge + ( curve.points[i].discharge - previousDischarge ) * f );
				}
			}
			line.points.push_back( point );
			line.discharge.push_back( curve.points[i].discharge );
		}
	}
	// Сначала — внутренняя точка другой кривой: два притока, впадающих в один узел главного, иначе нашли бы концы друг друга и
	// оба остались бы без продолжения; конец другой кривой — только если внутренней нет
	for( size_t a = 0; a < lines.size(); ++a )
	{
		const Point end = lines[a].points.back();
		if( lake[cellIndex( end, n )] )
			continue;
		for( int pass = 0; pass < 2 && lines[a].target < 0; ++pass )
		{
			for( size_t b = 0; b < lines.size() && lines[a].target < 0; ++b )
			{
				if( a == b )
					continue;
				for( size_t i = 0; i < lines[b].points.size(); ++i )
				{
					if( pass == 0 && i + 1 == lines[b].points.size() )
						continue;
					if( std::hypot( lines[b].points[i].x - end.x, lines[b].points[i].y - end.y ) < 0.25 )
					{
						lines[a].target = static_cast<int>( b );
						lines[a].targetPoint = i;
						break;
					}
				}
			}
		}
	}
	std::vector<Point> position;
	std::vector<double> q, nodeArea;
	std::vector<int> down, curveOf;
	std::vector<std::vector<int>> lineNodes( lines.size() );
	for( size_t a = 0; a < lines.size(); ++a )
	{
		const CurvePoints& line = lines[a];
		const size_t own = line.points.size() - ( line.target >= 0 ? 1 : 0 );
		for( size_t i = 0; i < own; ++i )
		{
			const int k = static_cast<int>( position.size() );
			position.push_back( line.points[i] );
			const double discharge = line.discharge[i] > 0.0 ? line.discharge[i] : sampleMax( dr.flow, line.points[i], n );
			q.push_back( discharge * line.curve->dischargeScale );
			nodeArea.push_back( sampleMax( dr.area, line.points[i], n ) );
			down.push_back( -1 );
			curveOf.push_back( static_cast<int>( a ) );
			if( i > 0 )
				down[k - 1] = k;
			lineNodes[a].push_back( k );
		}
	}
	const int count = static_cast<int>( position.size() );
	result.nodes = position.size();
	// Концы: слившийся — узлом той кривой; правленная и ручная — в ближайший узел другой кривой в пределах трёх клеток
	// (её конец могли сдвинуть, сгенерированная обрезана у правленной); в озере и у края — никуда
	for( size_t a = 0; a < lines.size(); ++a )
	{
		if( lineNodes[a].empty() )
			continue;
		const int last = lineNodes[a].back();
		if( lines[a].target >= 0 )
		{
			const std::vector<int>& target = lineNodes[lines[a].target];
			if( lines[a].targetPoint < target.size() )
				down[last] = target[lines[a].targetPoint];
			continue;
		}
		// Конец сгенерированной кривой известен: слияние (совпавшая точка), озеро или край карты
		const StreamCurve& curve = *lines[a].curve;
		if( ( curve.generated && !curve.edited ) || lake[cellIndex( position[last], n )] )
			continue;
		double best = 3.0;
		for( int k = 0; k < count; ++k )
		{
			if( curveOf[k] == static_cast<int>( a ) )
				continue;
			const double distance = std::hypot( position[k].x - position[last].x, position[k].y - position[last].y );
			if( distance < best )
			{
				best = distance;
				down[last] = k;
			}
		}
	}
	// От истоков вниз — топологический порядок (Kahn); связи, замкнувшие круг, рвутся
	std::vector<int> nodeOrder;
	{
		std::vector<int> incoming( count, 0 );
		for( int k = 0; k < count; ++k )
			if( down[k] >= 0 )
				++incoming[down[k]];
		std::vector<int> ready;
		for( int k = count - 1; k >= 0; --k )
			if( incoming[k] == 0 )
				ready.push_back( k );
		while( !ready.empty() )
		{
			const int k = ready.back();
			ready.pop_back();
			nodeOrder.push_back( k );
			const int d = down[k];
			if( d >= 0 && --incoming[d] == 0 )
				ready.push_back( d );
		}
		if( static_cast<int>( nodeOrder.size() ) < count )
		{
			for( int k = 0; k < count; ++k )
			{
				if( incoming[k] > 0 )
				{
					down[k] = -1;
					nodeOrder.push_back( k );
				}
			}
			result.warnings.push_back( "stream curves link into a loop: the loop is cut" );
		}
	}
	// Расход вниз по течению не убывает (ручная кривая с расходом по водосбору)
	for( int k : nodeOrder )
		if( down[k] >= 0 )
			q[down[k]] = std::max( q[down[k]], q[k] );
	std::vector<int> up( count, -1 );
	{
		std::vector<double> upFlow( count, 0.0 );
		for( int k = 0; k < count; ++k )
		{
			const int d = down[k];
			if( d >= 0 && q[k] > upFlow[d] )
			{
				upFlow[d] = q[k];
				up[d] = k;
			}
		}
	}
	std::vector<int> cellOf( count );
	for( int k = 0; k < count; ++k )
		cellOf[k] = cellIndex( position[k], n );
	auto inLake = [&]( int k ) { return lake[cellOf[k]] != 0; };

	// Параметры русла узла — общие, умноженные на множители его кривой
	struct Shape
	{
		double widthCoef, minWidth, depthCoef, minIncision, freeboard, bankSlope, thalweg, manning, stones;
	};
	std::vector<Shape> shape( count );
	for( int k = 0; k < count; ++k )
	{
		const StreamCurve& curve = *lines[curveOf[k]].curve;
		shape[k] = { p.widthCoef * curve.widthScale, p.minWidth * curve.widthScale, p.depthCoef * curve.depthScale,
					 p.minIncision * curve.depthScale, p.freeboard * curve.freeboardScale, p.bankSlope * curve.bankSlopeScale,
					 p.thalweg * curve.thalwegScale, p.manning * curve.roughnessScale, curve.stoneScale };
	}

	// Озёра — связные области одного уровня заполнения: номер озера у клетки (−1 — не озеро)
	std::vector<int> lakeLabel( cells, -1 );
	int lakeCount = 0;
	{
		std::vector<int> stack;
		for( size_t start = 0; start < cells; ++start )
		{
			if( !lake[start] || lakeLabel[start] >= 0 )
				continue;
			lakeLabel[start] = lakeCount;
			stack.assign( 1, static_cast<int>( start ) );
			while( !stack.empty() )
			{
				const int i = stack.back();
				stack.pop_back();
				for( int k = 0; k < 8; ++k )
				{
					const int r = i / n + neighbourRow[k];
					const int c = i % n + neighbourCol[k];
					if( r < 0 || c < 0 || r >= n || c >= n )
						continue;
					const int j = r * n + c;
					if( lake[j] && lakeLabel[j] < 0 && lakeLevel[j] == lakeLevel[i] )
					{
						lakeLabel[j] = lakeCount;
						stack.push_back( j );
					}
				}
			}
			++lakeCount;
		}
	}
	// Уровень заполнения низины озера (перелив) — по номеру
	std::vector<double> fillLevel( lakeCount, 0.0 );
	for( size_t i = 0; i < cells; ++i )
		if( lakeLabel[i] >= 0 )
			fillLevel[lakeLabel[i]] = lakeLevel[i];
	// Узлы ниже озера: путь от берега, м (бесконечность — не из озера). Кривая, которая начинается у берега (озеро в трёх
	// клетках), — исток этого озера; у озера с несколькими — ниже всех по земле (порог перелива)
	std::vector<double> outletDistance( count, std::numeric_limits<double>::infinity() );
	std::vector<int> lakeOutlet( lakeCount, -1 );
	std::vector<uint8_t> lakeStart( count, 0 );
	for( size_t a = 0; a < lines.size(); ++a )
	{
		if( lineNodes[a].empty() )
			continue;
		const int k = lineNodes[a].front();
		if( up[k] >= 0 || inLake( k ) )
			continue;
		const int col = cellOf[k] % n;
		const int row = cellOf[k] / n;
		double best = 3.0;
		int outletLake = -1;
		for( int dy = -3; dy <= 3; ++dy )
		{
			for( int dx = -3; dx <= 3; ++dx )
			{
				const int r = row + dy;
				const int c = col + dx;
				if( r < 0 || c < 0 || r >= n || c >= n || !lake[static_cast<size_t>( r ) * n + c] )
					continue;
				const double distance = std::hypot( c + 0.5 - position[k].x, r + 0.5 - position[k].y );
				if( distance < best )
				{
					best = distance;
					outletDistance[k] = distance * cell;
					lakeStart[k] = 1;
					outletLake = lakeLabel[static_cast<size_t>( r ) * n + c];
				}
			}
		}
		// Исток — ниже перелива (не ручей, который лишь начинается выше по склону у берега)
		if( outletLake >= 0 && height.sample( position[k].x - 0.5, position[k].y - 0.5 ) <= fillLevel[outletLake] + 0.25 )
		{
			int& outlet = lakeOutlet[outletLake];
			if( outlet < 0 || height.sample( position[k].x - 0.5, position[k].y - 0.5 ) <
								  height.sample( position[outlet].x - 0.5, position[outlet].y - 0.5 ) )
				outlet = k;
		}
	}
	// Ручей, который кончается в озере или у его берега (озеро в трёх клетках), впадает в него: уровень озера — его вода ниже
	// по течению (подпор)
	std::vector<int> endLake( count, -1 );
	for( size_t a = 0; a < lines.size(); ++a )
	{
		if( lineNodes[a].empty() )
			continue;
		const int last = lineNodes[a].back();
		if( down[last] >= 0 )
			continue;
		const int col = cellOf[last] % n;
		const int row = cellOf[last] / n;
		double best = 3.5;
		for( int dy = -3; dy <= 3; ++dy )
		{
			for( int dx = -3; dx <= 3; ++dx )
			{
				const int r = row + dy;
				const int c = col + dx;
				if( r < 0 || c < 0 || r >= n || c >= n || lakeLabel[static_cast<size_t>( r ) * n + c] < 0 )
					continue;
				const double distance = std::hypot( dx, dy );
				if( distance < best )
				{
					best = distance;
					endLake[last] = lakeLabel[static_cast<size_t>( r ) * n + c];
				}
			}
		}
	}
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d < 0 || inLake( d ) )
			continue;
		const bool fromLake = inLake( k );
		if( !fromLake && !std::isfinite( outletDistance[k] ) )
			continue;
		const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
		outletDistance[d] = std::min( outletDistance[d], ( fromLake ? 0.0 : outletDistance[k] ) + step );
	}

	// Родник: путь от истока ручья (узел без притока, не из озера) вниз по главному течению, м — наименьшие ширина, врез,
	// вода и запас бровки нарастают на source_taper метрах: ручей начинается мокрой канавкой, а не готовым руслом
	std::vector<double> fromSource( count, std::numeric_limits<double>::infinity() );
	for( int k : nodeOrder )
	{
		if( up[k] < 0 )
			fromSource[k] = std::isfinite( outletDistance[k] ) || inLake( k ) ? std::numeric_limits<double>::infinity() : 0.0;
		const int d = down[k];
		if( d >= 0 && up[d] == k )
			fromSource[d] = fromSource[k] + std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
	}
	std::vector<double> sourceShare( count, 1.0 );
	for( int k = 0; k < count; ++k )
		if( p.sourceTaper > 0.0f && std::isfinite( fromSource[k] ) )
			sourceShare[k] = smoothstep( 0.0, p.sourceTaper, fromSource[k] );

	// 4. Ложе: дно монотонно вниз по течению, профиль по отрезкам оси — дно, тальвег, борта. В озере ложа нет (дно озера),
	// и оно не тянет дно ниже по течению; ниже озера врез нарастает от порога (outletSlope)
	std::vector<double> half( count ), depth( count ), bed( count ), bank( count ), surface( count );
	for( int k = 0; k < count; ++k )
	{
		Shape s = shape[k];
		// У родника — от трети наименьших ширины, вреза, воды и запаса бровки (вода и у родника ниже бровки)
		const double grow = 0.33 + 0.67 * sourceShare[k];
		s.minWidth *= grow;
		s.minIncision *= grow;
		s.freeboard *= grow;
		shape[k].freeboard = s.freeboard;
		half[k] = 0.5 * std::max( s.widthCoef * std::sqrt( q[k] ), s.minWidth ) / cell;
		surface[k] = height.sample( position[k].x - 0.5, position[k].y - 0.5 );
		// Русло прорезано под паводок: вода в межень (по Маннингу, уклон — по поверхности) стоит ниже бровки на freeboard.
		// У оси дно глубже на тальвег, поэтому врез · (1 + thalweg) ≥ вода + запас
		double surfaceSlope = p.minWaterSlope;
		if( down[k] >= 0 )
		{
			const int d = down[k];
			const double step = std::max( std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell, 1e-3 );
			surfaceSlope = std::max( ( surface[k] - height.sample( position[d].x - 0.5, position[d].y - 0.5 ) ) / step, surfaceSlope );
		}
		const double waterEstimate = std::max( std::pow( q[k] * s.manning / ( 2.0 * half[k] * cell * std::sqrt( surfaceSlope ) ), 0.6 ),
											   p.minWaterDepth * grow );
		depth[k] = std::max( { s.depthCoef * std::pow( q[k], 0.4 ), s.minIncision, ( waterEstimate + s.freeboard ) / ( 1.0 + s.thalweg ) } );
		if( inLake( k ) )
			depth[k] = 0.0;
		else if( std::isfinite( outletDistance[k] ) )
			depth[k] = std::min( depth[k], outletDistance[k] * outletSlope );
		bed[k] = surface[k] - depth[k];
	}
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d >= 0 && !inLake( k ) )
		{
			const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
			bed[d] = std::min( bed[d], bed[k] - p.minSlope * step );
		}
	}
	for( int k = 0; k < count; ++k )
	{
		depth[k] = surface[k] - bed[k];
		bank[k] = depth[k] * shape[k].bankSlope / cell;
	}
	// Изгиб русла в узле — кривизна оси × ширина (−1…1, > 0 — поворот влево в клетках карты), сглаженная вдоль течения:
	// у внешнего берега излучины глубже и круче, у внутреннего — пологая отмель
	std::vector<double> bend( count, 0.0 );
	for( int k = 0; k < count; ++k )
	{
		if( up[k] < 0 || down[k] < 0 )
			continue;
		const Point in = { position[k].x - position[up[k]].x, position[k].y - position[up[k]].y };
		const Point out = { position[down[k]].x - position[k].x, position[down[k]].y - position[k].y };
		const double turn = std::atan2( in.x * out.y - in.y * out.x, in.x * out.x + in.y * out.y );
		const double length = 0.5 * ( std::hypot( in.x, in.y ) + std::hypot( out.x, out.y ) ) * cell;
		bend[k] = turn / std::max( length, 1e-3 ) * 2.0 * half[k] * cell * 2.0;
	}
	for( int pass = 0; pass < 6; ++pass )
	{
		std::vector<double> smoothed = bend;
		for( int k = 0; k < count; ++k )
			if( up[k] >= 0 && down[k] >= 0 )
				smoothed[k] = 0.25 * bend[up[k]] + 0.5 * bend[k] + 0.25 * bend[down[k]];
		bend = std::move( smoothed );
	}
	for( double& value : bend )
		value = std::clamp( value, -1.0, 1.0 );
	// Плёсы и перекаты: фаза вдоль главного течения, период — pool_spacing ширин русла (Leopold, Wolman 1957: 5–7 ширин)
	std::vector<double> poolPhase( count, 0.0 );
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d >= 0 && up[d] == k )
		{
			const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
			poolPhase[d] = poolPhase[k] + step / std::max( p.poolSpacing * 2.0 * half[k] * cell, 1.0 );
		}
	}
	// Путь до берега озера ниже по течению, м: у впадения, как у истока, наименьшая глубина и запас нарастают от берега —
	// в устье вода ручья переходит в озеро, а не стоит над ним ступенькой
	std::vector<double> inflowDistance( count, std::numeric_limits<double>::infinity() );
	for( auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it )
	{
		const int k = *it;
		const int d = down[k];
		if( inLake( k ) )
			continue;
		if( endLake[k] >= 0 )
			inflowDistance[k] = 0.0;
		else if( d >= 0 )
		{
			const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
			inflowDistance[k] = inLake( d ) ? step : inflowDistance[d] + step;
		}
	}
	// Ступени и плёсы на крутом (step-pool, каскад; Montgomery, Buffington 1997: ступени — с ~3 % уклона, каскад — круче
	// 6,5 %): уступ step_height, шаг L — чтобы крутизна ступеней H / L была step_steepness уклона участка (Abrahams, Atkinson
	// 1995), но 0,5…4 ширины дна (Chin 1999: 0,4–2,4 ширины; положе — уступ ниже и сходит на нет). Уклон участка — по дну
	// на ±stepSlopeSpan вдоль сети: у узла дно идёт по рельефу, и уклон одного отрезка шумит. У истока из озера и в устье
	// ступеней нет (там уровень озера), положе step_min_slope — тоже (плёсы и перекаты)
	std::vector<double> stepLength( count, 1.0 ), stepHeight( count, 0.0 ), stepPhase( count, 0.0 );
	std::vector<uint32_t> stepSeed( count, 0 );
	if( p.stepHeight > 0.0f )
	{
		auto walkBed = [&]( int from, const std::vector<int>& next, double& distance )
		{
			distance = 0.0;
			while( distance < stepSlopeSpan && next[from] >= 0 && !inLake( next[from] ) )
			{
				const int to = next[from];
				distance += std::hypot( position[to].x - position[from].x, position[to].y - position[from].y ) * cell;
				from = to;
			}
			return from;
		};
		for( int k = 0; k < count; ++k )
		{
			if( inLake( k ) )
				continue;
			double below = 0.0, above = 0.0;
			const int lower = walkBed( k, down, below );
			const int upper = walkBed( k, up, above );
			const double reachSlope = below + above > 1e-3 ? std::max( ( bed[upper] - bed[lower] ) / ( below + above ), 0.0 ) : 0.0;
			const double width = 2.0 * half[k] * cell;
			const double steepness = std::max( static_cast<double>( p.stepSteepness ), 0.1 );
			stepLength[k] = std::clamp( p.stepHeight / ( steepness * std::max( reachSlope, 1e-4 ) ), std::max( 0.5 * width, 0.5 ), 4.0 * width );
			const double share = smoothstep( 0.67 * p.stepMinSlope, p.stepMinSlope, reachSlope ) *
								 std::clamp( std::min( outletDistance[k], inflowDistance[k] ) / ( 2.0 * stepLength[k] ), 0.0, 1.0 );
			// Не выше вреза у оси: у родника и на мелком русле уступ поднимал бы воду плёса выше берега
			stepHeight[k] = std::min( steepness * reachSlope * stepLength[k], depth[k] * ( 1.0 + shape[k].thalweg ) ) * share;
		}
		for( int k : nodeOrder )
		{
			if( up[k] < 0 )
				stepSeed[k] = static_cast<uint32_t>( std::llround( position[k].x * 16.0 ) * 73856093ll ^ std::llround( position[k].y * 16.0 ) * 19349663ll );
			const int d = down[k];
			if( d >= 0 && up[d] == k )
			{
				const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
				stepPhase[d] = stepPhase[k] + step / stepLength[k];
				stepSeed[d] = stepSeed[k];
			}
		}
	}

	// Врез отрезка k → down[k] в точке (cx, cy) клеток (центр клетки — +0,5). Его же считают и клетки карты, и точки
	// детальной земли. Врез по отрезку оси a → b, параметры — от узла k к down[k] в долях t0…t1 (часть отрезка — у гладкой оси
	// детальной земли). Профиль поперёк: дно шириной w с тальвегом (форма — bed_shape: 2 — V, больше — плоский U), борта
	// шириной bw (bank_roundness: 1 — скруглённые бровка и подошва, 0 — прямой откос с резкой бровкой); на изгибе тальвег
	// сдвигается к внешнему берегу, внешний борт круче и глубже, внутренний — пологая отмель (bend_asymmetry); плёсы — глубже
	// (pool_depth); шум дна и бортов (bed_noise). base — тот же врез без плёсов и шума: по нему уровень воды (в плёсе вода
	// глубже, а не ниже)
	// Сечение русла в узле: полуширина дна и ширина борта (клетки), врез (м), доля тальвега, изгиб, фаза плёсов
	struct Section
	{
		double half, depth, bank, thalweg, bend, phase;
		double stepPhase, stepLength, stepHeight;	// ступени: фаза (целое — котёл под уступом), шаг, м, высота уступа, м
		uint32_t stepSeed;							// разброс высоты уступов — свой у каждого ручья
	};
	auto section = [&]( int k )
	{
		return Section{ half[k], depth[k], bank[k], shape[k].thalweg, bend[k], poolPhase[k], stepPhase[k], stepLength[k], stepHeight[k], stepSeed[k] };
	};
	// Подъём дна ступенями над гладким дном в доле t отрезка, м (≤ 0 почти везде): от котла под уступом (фаза — целое) дно
	// поднимается к гребню следующего уступа, затем уступ длиной stepFace вниз — к котлу. Высота уступа — step_height с
	// разбросом ±30 % у каждого (свой у ручья); подъём круче уклона (step_steepness > 1), поэтому котёл за уступом — глубже
	// гладкого дна. Ступени врезаны вниз: гребень — на гладком дне (± разброс), котлы — ниже. С гребнем выше гладкого дна
	// (вниз и вверх поровну) уровень плёса за ним поднимался над прежним, и у мелкого русла вода выходила на берег тонким
	// слоем — разлив по траве (2026-10-11)
	auto stepOffset = [&]( const Section& s0, const Section& s1, double t )
	{
		const double height = s0.stepHeight + ( s1.stepHeight - s0.stepHeight ) * t;
		if( height <= 0.0 )
			return 0.0;
		const double phase = s0.stepPhase + ( s1.stepPhase - s0.stepPhase ) * t;
		const double length = s0.stepLength + ( s1.stepLength - s0.stepLength ) * t;
		const double index = std::floor( phase );
		const double f = phase - index;
		auto jitter = [&]( double i )
		{
			uint32_t h = static_cast<uint32_t>( static_cast<int64_t>( i ) ) * 747796405u + s0.stepSeed;
			h = ( ( h >> ( ( h >> 28u ) + 4u ) ) ^ h ) * 277803737u;
			return 0.7 + 0.6 * static_cast<double>( ( h >> 22u ) ^ h ) / 4294967295.0;
		};
		const double here = 0.5 * height * jitter( index );
		const double next = 0.5 * height * jitter( index + 1.0 );
		const double face = std::clamp( stepFace / std::max( length, 1e-3 ), 0.05, 0.4 );
		const double down = 0.5 * height;
		if( f < 1.0 - face )
			return -here + 2.0 * here * f / ( 1.0 - face ) - down;
		return here - ( here + next ) * smoothstep( 0.0, 1.0, ( f - ( 1.0 - face ) ) / face ) - down;
	};
	auto pieceProfile = [&]( const Section& s0, const Section& s1, Point a, Point b, double t0, double t1, double cx, double cy,
							 double& base )
	{
		const double sx = b.x - a.x;
		const double sy = b.y - a.y;
		const double length2 = std::max( sx * sx + sy * sy, 1e-9 );
		const double local = std::clamp( ( ( cx - a.x ) * sx + ( cy - a.y ) * sy ) / length2, 0.0, 1.0 );
		const double offsetX = cx - ( a.x + local * sx );
		const double offsetY = cy - ( a.y + local * sy );
		const double distance = std::hypot( offsetX, offsetY );
		// Сторона: > 0 — слева по течению (в клетках карты)
		const double side = sx * offsetY - sy * offsetX;
		const double t = t0 + ( t1 - t0 ) * local;
		const double w = s0.half + ( s1.half - s0.half ) * t;
		const double dep = s0.depth + ( s1.depth - s0.depth ) * t;
		const double bw = std::max( s0.bank + ( s1.bank - s0.bank ) * t, 1e-3 );
		const double thalwegShare = s0.thalweg + ( s1.thalweg - s0.thalweg ) * t;
		const double bendHere = s0.bend + ( s1.bend - s0.bend ) * t;
		// Асимметрия: внутренний берег — со стороны центра поворота (bend > 0 — слева)
		const double asym = std::clamp( p.bendAsymmetry * std::abs( bendHere ), 0.0, 1.0 );
		const bool innerSide = side * bendHere > 0.0;
		const double sideWidth = innerSide ? w * ( 1.0 - 0.3 * asym ) : w;
		const double sideBank = innerSide ? bw * ( 1.0 + 2.0 * asym ) : bw * std::max( 1.0 - 0.6 * asym, 0.3 );
		const double sideDepth = innerSide ? dep : dep * ( 1.0 + 0.3 * asym );
		// Тальвег — к внешнему берегу на долю ширины
		const double lateral = ( side >= 0.0 ? distance : -distance );
		const double thalwegCenter = ( bendHere > 0.0 ? -1.0 : 1.0 ) * asym * 0.5 * w;
		const double inner = std::clamp( std::abs( lateral - thalwegCenter ) / std::max( w, 1e-3 ), 0.0, 1.0 );
		const double thalweg = thalwegShare * sideDepth * ( 1.0 - std::pow( inner, std::max( static_cast<double>( p.bedShape ), 1.0 ) ) );
		// Борт: от подошвы (0) к бровке (1) — скруглённый (smoothstep) или прямой
		const double slope = std::clamp( ( distance - sideWidth ) / sideBank, 0.0, 1.0 );
		const double roundness = std::clamp( static_cast<double>( p.bankRoundness ), 0.0, 1.0 );
		const double rise = slope + ( smoothstep( 0.0, 1.0, slope ) - slope ) * roundness;
		// Ступени — поперёк всего дна, к бровке сходят на нет; по ним и уровень воды (base): в плёсе вода ровная, на уступе
		// — перелив
		const double step = stepOffset( s0, s1, t );
		const double profile = std::max( sideDepth * ( 1.0 - rise ) + thalweg - step * ( 1.0 - rise ), 0.0 );
		base = profile;
		// Плёс — глубже на pool_depth вреза, пол-периода; перекат — как есть. На ступенчатом участке плёсы — котлы ступеней
		const double phase = s0.phase + ( s1.phase - s0.phase ) * t;
		const double stepped = std::clamp( std::max( s0.stepHeight, s1.stepHeight ) / std::max( static_cast<double>( p.stepHeight ), 1e-3 ), 0.0, 1.0 );
		const double pool = 1.0 + p.poolDepth * ( 1.0 - stepped ) * 0.5 * ( 1.0 + std::sin( 2.0 * pi * phase ) );
		const double fill = profile / std::max( sideDepth * ( 1.0 + thalwegShare ), 1e-3 );	// 0 у бровки, 1 у тальвега
		const double noise = p.bedNoise * fill *
							 ( valueNoise( cx * cell / p.bedNoiseScale, cy * cell / p.bedNoiseScale ) +
							   0.5 * valueNoise( cx * cell * 2.1 / p.bedNoiseScale + 17.3, cy * cell * 2.1 / p.bedNoiseScale - 5.1 ) );
		return std::max( profile * ( 1.0 + ( pool - 1.0 ) * fill ) + noise, 0.0 );
	};
	// Ось — сплайн Catmull-Rom через узлы (до — главный приток, после — получатель получателя), четыре части на отрезок: по
	// ломаной на каждом изломе внутренний борт сходился бы складкой (фестоны вдоль бровки через метр)
	constexpr int pieces = 4;
	std::vector<std::array<Point, pieces + 1>> spline( count );
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k];
		if( d < 0 )
			continue;
		const Point p0 = position[up[k] >= 0 ? up[k] : k];
		const Point p1 = position[k];
		const Point p2 = position[d];
		const Point p3 = position[down[d] >= 0 ? down[d] : d];
		for( int s = 0; s <= pieces; ++s )
		{
			const double t = static_cast<double>( s ) / pieces;
			const double t2 = t * t;
			const double t3 = t2 * t;
			auto blend = [&]( double v0, double v1, double v2, double v3 )
			{
				return 0.5 * ( 2.0 * v1 + ( v2 - v0 ) * t + ( 2.0 * v0 - 5.0 * v1 + 4.0 * v2 - v3 ) * t2 + ( 3.0 * v1 - v0 - 3.0 * v2 + v3 ) * t3 );
			};
			spline[k][s] = { blend( p0.x, p1.x, p2.x, p3.x ), blend( p0.y, p1.y, p2.y, p3.y ) };
		}
	}
	// Врез отрезка в точке — наибольший по частям сплайна; base — без плёсов и шума
	auto segmentProfile = [&]( int k, double cx, double cy, double& base )
	{
		double value = 0.0;
		base = 0.0;
		const Section s0 = section( k );
		const Section s1 = section( down[k] );
		for( int s = 0; s < pieces; ++s )
		{
			double pieceBase = 0.0;
			value = std::max( value, pieceProfile( s0, s1, spline[k][s], spline[k][s + 1], static_cast<double>( s ) / pieces,
												   static_cast<double>( s + 1 ) / pieces, cx, cy, pieceBase ) );
			base = std::max( base, pieceBase );
		}
		return value;
	};
	auto segmentReach = [&]( int k ) { return std::max( half[k] + bank[k], half[down[k]] + bank[down[k]] ); };
	// Рамка отрезка по сплайну, клетки
	auto segmentBox = [&]( int k, double& xMin, double& xMax, double& yMin, double& yMax )
	{
		const double reach = segmentReach( k );
		xMin = yMin = std::numeric_limits<double>::infinity();
		xMax = yMax = -std::numeric_limits<double>::infinity();
		for( const Point& point : spline[k] )
		{
			xMin = std::min( xMin, point.x - reach );
			xMax = std::max( xMax, point.x + reach );
			yMin = std::min( yMin, point.y - reach );
			yMax = std::max( yMax, point.y + reach );
		}
	};
	std::vector<double> carve( cells, 0.0 );
	std::vector<double> carveBase( cells, 0.0 );	// без плёсов и шума: по нему уровень воды
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k];
		if( d < 0 )
			continue;
		double xMin, xMax, yMin, yMax;
		segmentBox( k, xMin, xMax, yMin, yMax );
		const int c0 = static_cast<int>( std::max( std::floor( xMin ), 0.0 ) );
		const int c1 = static_cast<int>( std::min( std::ceil( xMax ), n - 1.0 ) );
		const int r0 = static_cast<int>( std::max( std::floor( yMin ), 0.0 ) );
		const int r1 = static_cast<int>( std::min( std::ceil( yMax ), n - 1.0 ) );
		for( int row = r0; row <= r1; ++row )
		{
			for( int col = c0; col <= c1; ++col )
			{
				const size_t i = static_cast<size_t>( row ) * n + col;
				double base = 0.0;
				carve[i] = std::max( carve[i], segmentProfile( k, col + 0.5, row + 0.5, base ) );
				carveBase[i] = std::max( carveBase[i], base );
			}
		}
	}
	// В озёрах русло не режется, дно — чаша (lakeBasins)
	const std::vector<double> basin = lakeBasins( lakeLevel, height.values, n, cell, p );

	// Подводное русло: ручей, который впадает в озеро (последний узел вне озера — следующий в озере или конец кривой у
	// берега), продолжается от этого узла с тем же сечением — по узлам кривой в озере, за концом кривой — прямо, дно — вниз
	// не положе min_slope, пока чаша не станет глубже (и ещё на ширину русла — без стенки на конце). Иначе врез обрывался у
	// озера: в клетках озера дно — чаша, а у берега она мелкая (отмель от нуля) — порог на дне между руслом и свалом
	struct MouthExtension
	{
		Point a, b;
		Section s0, s1;
	};
	std::vector<MouthExtension> mouths;
	constexpr double mouthMaxLength = 40.0;	// м
	for( int k = 0; k < count; ++k )
	{
		if( inLake( k ) || up[k] < 0 )
			continue;
		const bool intoLake = down[k] >= 0 ? inLake( down[k] ) : endLake[k] >= 0;
		if( !intoLake )
			continue;
		// Путь: узлы кривой в озере; кривая кончилась (в озере или у берега) — дальше прямо по направлению последних узлов
		// (у сгенерированной кривой точки через метр, ось извилиста). Ручей, который выходит из озера, — до последнего узла в нём
		std::vector<Point> path = { position[k] };
		int last = k;
		while( down[last] >= 0 && inLake( down[last] ) && path.size() * cell < mouthMaxLength )
		{
			last = down[last];
			path.push_back( position[last] );
		}
		const size_t curveCount = path.size();
		if( down[last] < 0 )
		{
			int from = last;
			for( int step = 0; step < 4 && up[from] >= 0; ++step )
				from = up[from];
			const double dx = position[last].x - position[from].x;
			const double dy = position[last].y - position[from].y;
			const double length = std::hypot( dx, dy );
			if( length > 1e-3 )
				for( double s = 1.0; ( curveCount - 1 + s ) * cell <= mouthMaxLength; s += 1.0 )
					path.push_back( { position[last].x + dx / length * s, position[last].y + dy / length * s } );
		}
		// Вершины пути до места, где чаша глубже продолжения дна, и ещё на ширину русла; расстояние — по пути, клетки
		const double thalwegFactor = 1.0 + shape[k].thalweg;
		const double tail = half[k] + bank[k];
		std::vector<Point> kept = { path[0] };
		std::vector<double> along = { 0.0 };
		bool entered = false;
		double deepFrom = -1.0;
		for( size_t v = 1; v < path.size(); ++v )
		{
			const Point q = path[v];
			const int col = static_cast<int>( std::floor( q.x ) );
			const int row = static_cast<int>( std::floor( q.y ) );
			if( col < 0 || row < 0 || col >= n || row >= n )
				break;
			const size_t i = static_cast<size_t>( row ) * n + col;
			if( !lake[i] && entered && v >= curveCount )
				break;	// прямое продолжение вышло из озера (узкий залив) — дальше не режем
			const double s = along.back() + std::hypot( q.x - kept.back().x, q.y - kept.back().y );
			kept.push_back( q );
			along.push_back( s );
			if( !lake[i] )
				continue;
			entered = true;
			if( deepFrom < 0.0 )
			{
				const double need = ( height.sample( q.x - 0.5, q.y - 0.5 ) - ( bed[k] - p.minSlope * s * cell ) ) * thalwegFactor;
				if( basin[i] >= need )
					deepFrom = s;
			}
			else if( s >= deepFrom + tail )
				break;
		}
		if( !entered )
			continue;
		auto sectionAt = [&]( size_t v )
		{
			if( v == 0 )
				return section( k );
			Section result = section( k );
			result.bend = 0.0;
			result.stepHeight = 0.0;
			result.depth = std::max( height.sample( kept[v].x - 0.5, kept[v].y - 0.5 ) - ( bed[k] - p.minSlope * along[v] * cell ), 0.0 );
			result.phase += along[v] * cell / std::max( p.poolSpacing * 2.0 * half[k] * cell, 1.0 );
			return result;
		};
		for( size_t v = 0; v + 1 < kept.size(); ++v )
			mouths.push_back( { kept[v], kept[v + 1], sectionAt( v ), sectionAt( v + 1 ) } );	}
	auto mouthBox = [&]( const MouthExtension& mouth, double& xMin, double& xMax, double& yMin, double& yMax )
	{
		const double reach = std::max( mouth.s0.half + mouth.s0.bank, mouth.s1.half + mouth.s1.bank );
		xMin = std::min( mouth.a.x, mouth.b.x ) - reach;
		xMax = std::max( mouth.a.x, mouth.b.x ) + reach;
		yMin = std::min( mouth.a.y, mouth.b.y ) - reach;
		yMax = std::max( mouth.a.y, mouth.b.y ) + reach;
	};
	auto mouthProfile = [&]( const MouthExtension& mouth, double cx, double cy, double& base )
	{
		return pieceProfile( mouth.s0, mouth.s1, mouth.a, mouth.b, 0.0, 1.0, cx, cy, base );
	};
	// Врез подводных русел — отдельно: в клетках озера он спорит с чашей, а не с руслами
	std::vector<double> mouthCarve( cells, 0.0 );
	std::vector<double> mouthBase( cells, 0.0 );
	for( const MouthExtension& mouth : mouths )
	{
		double xMin, xMax, yMin, yMax;
		mouthBox( mouth, xMin, xMax, yMin, yMax );
		const int c0 = static_cast<int>( std::max( std::floor( xMin ), 0.0 ) );
		const int c1 = static_cast<int>( std::min( std::ceil( xMax ), n - 1.0 ) );
		const int r0 = static_cast<int>( std::max( std::floor( yMin ), 0.0 ) );
		const int r1 = static_cast<int>( std::min( std::ceil( yMax ), n - 1.0 ) );
		for( int row = r0; row <= r1; ++row )
		{
			for( int col = c0; col <= c1; ++col )
			{
				const size_t i = static_cast<size_t>( row ) * n + col;
				double base = 0.0;
				mouthCarve[i] = std::max( mouthCarve[i], mouthProfile( mouth, col + 0.5, row + 0.5, base ) );
				mouthBase[i] = std::max( mouthBase[i], base );
			}
		}
	}
	for( size_t i = 0; i < cells; ++i )
	{
		if( lake[i] )
		{
			carve[i] = std::max( basin[i], mouthCarve[i] );
			carveBase[i] = std::max( basin[i], mouthBase[i] );
		}
		else
		{
			carve[i] = std::max( carve[i], mouthCarve[i] );
			carveBase[i] = std::max( carveBase[i], mouthBase[i] );
		}
	}

	// Детальная земля у русел: плитки по detailTileCells клеток, где русло режет рельеф (озёра — нет: дно под водой);
	// точка — рельеф до вреза Catmull-Rom (через центры клеток) минус тот же профиль русла, что у клеток, в самой точке;
	// в клетке озера — чаша (билинейно). Тексели — по x и z мира, с полем в тексель с каждой стороны
	result.detailTiles.clear();
	{
		const int tileCells = static_cast<int>( detailTileCells );
		const int samples = static_cast<int>( detailSamples );
		const int side = tileCells * samples + 2;
		const int tiles = ( n + tileCells - 1 ) / tileCells;
		const double step = 1.0 / samples;	// клеток на тексель
		// Плитки — где проходит русло (рамка отрезка оси): узкое русло может не задеть центры клеток карты
		std::vector<int> tileIndex( static_cast<size_t>( tiles ) * tiles, -1 );
		auto addTiles = [&]( double xMin, double xMax, double yMin, double yMax )
		{
			const int tx0 = std::max( static_cast<int>( std::floor( xMin / tileCells ) ), 0 );
			const int tx1 = std::min( static_cast<int>( std::floor( xMax / tileCells ) ), tiles - 1 );
			const int tz0 = std::max( static_cast<int>( std::floor( ( n - yMax ) / tileCells ) ), 0 );
			const int tz1 = std::min( static_cast<int>( std::floor( ( n - yMin ) / tileCells ) ), tiles - 1 );
			for( int tz = tz0; tz <= tz1; ++tz )
			{
				for( int tx = tx0; tx <= tx1; ++tx )
				{
					int& index = tileIndex[static_cast<size_t>( tz ) * tiles + tx];
					if( index >= 0 )
						continue;
					index = static_cast<int>( result.detailTiles.size() );
					DetailTile& detail = result.detailTiles.emplace_back();
					detail.x = static_cast<uint32_t>( tx );
					detail.z = static_cast<uint32_t>( tz );
				}
			}
		};
		for( int k = 0; k < count; ++k )
		{
			if( down[k] < 0 || ( inLake( k ) && inLake( down[k] ) ) )
				continue;
			double xMin, xMax, yMin, yMax;
			segmentBox( k, xMin, xMax, yMin, yMax );
			addTiles( xMin, xMax, yMin, yMax );
		}
		for( const MouthExtension& mouth : mouths )
		{
			double xMin, xMax, yMin, yMax;
			mouthBox( mouth, xMin, xMax, yMin, yMax );
			addTiles( xMin, xMax, yMin, yMax );
		}
		// Тексель (i, j) плитки (x, z): клетки — ( x · tileCells + ( i − 0,5 ) · step, n − z · tileCells − ( j − 0,5 ) · step )
		auto texelCell = [&]( const DetailTile& tile, int i, int j )
		{
			return Point{ tile.x * tileCells + ( i - 0.5 ) * step, n - ( tile.z * tileCells + ( j - 0.5 ) * step ) };
		};
		// Отрезки оси и подводные русла, рамка которых задевает плитку
		std::vector<std::vector<int>> tileSegments( result.detailTiles.size() );
		std::vector<std::vector<int>> tileMouths( result.detailTiles.size() );
		auto collectTiles = [&]( double xMin, double xMax, double yMin, double yMax, std::vector<std::vector<int>>& lists, int item )
		{
			// Плитки по z мира: строки клеток y → z = n − y
			const int tx0 = std::max( static_cast<int>( std::floor( ( xMin - step ) / tileCells ) ), 0 );
			const int tx1 = std::min( static_cast<int>( std::floor( ( xMax + step ) / tileCells ) ), tiles - 1 );
			const int tz0 = std::max( static_cast<int>( std::floor( ( n - yMax - step ) / tileCells ) ), 0 );
			const int tz1 = std::min( static_cast<int>( std::floor( ( n - yMin + step ) / tileCells ) ), tiles - 1 );
			for( int tz = tz0; tz <= tz1; ++tz )
				for( int tx = tx0; tx <= tx1; ++tx )
					if( const int t = tileIndex[static_cast<size_t>( tz ) * tiles + tx]; t >= 0 )
						lists[t].push_back( item );
		};
		for( int k = 0; k < count; ++k )
		{
			if( down[k] < 0 )
				continue;
			double xMin, xMax, yMin, yMax;
			segmentBox( k, xMin, xMax, yMin, yMax );
			collectTiles( xMin, xMax, yMin, yMax, tileSegments, k );
		}
		for( size_t m = 0; m < mouths.size(); ++m )
		{
			double xMin, xMax, yMin, yMax;
			mouthBox( mouths[m], xMin, xMax, yMin, yMax );
			collectTiles( xMin, xMax, yMin, yMax, tileMouths, static_cast<int>( m ) );
		}
		Grid basinGrid;
		basinGrid.size = n;
		basinGrid.values = basin;
		// Плитки независимы — параллельно: рельеф до вреза Catmull-Rom, врез отрезков (тот же сплайн оси, что у клеток), в
		// озере — чаша
		std::vector<size_t> order( result.detailTiles.size() );
		std::iota( order.begin(), order.end(), size_t( 0 ) );
		std::vector<std::vector<float>> tileBase( result.detailTiles.size() );
		std::for_each( std::execution::par, order.begin(), order.end(), [&]( size_t t )
		{
			DetailTile& tile = result.detailTiles[t];
			tile.heights.resize( static_cast<size_t>( side ) * side );
			tile.carve.assign( tile.heights.size(), 0.0f );
			tileBase[t].assign( tile.heights.size(), 0.0f );
			std::vector<double> carved( tile.heights.size(), 0.0 );
			std::vector<double> mouthCarved( tile.heights.size(), 0.0 );
			for( int m : tileMouths[t] )
			{
				const MouthExtension& mouth = mouths[m];
				double xMin, xMax, yMin, yMax;
				mouthBox( mouth, xMin, xMax, yMin, yMax );
				const int i0 = std::max( static_cast<int>( std::floor( ( xMin - tile.x * tileCells ) / step + 0.5 ) ), 0 );
				const int i1 = std::min( static_cast<int>( std::ceil( ( xMax - tile.x * tileCells ) / step + 0.5 ) ), side - 1 );
				const int j0 = std::max( static_cast<int>( std::floor( ( n - yMax - tile.z * tileCells ) / step + 0.5 ) ), 0 );
				const int j1 = std::min( static_cast<int>( std::ceil( ( n - yMin - tile.z * tileCells ) / step + 0.5 ) ), side - 1 );
				for( int j = j0; j <= j1; ++j )
				{
					for( int i = i0; i <= i1; ++i )
					{
						const Point at = texelCell( tile, i, j );
						const size_t index = static_cast<size_t>( j ) * side + i;
						double base = 0.0;
						mouthCarved[index] = std::max( mouthCarved[index], mouthProfile( mouth, at.x, at.y, base ) );
						tileBase[t][index] = std::max( tileBase[t][index], static_cast<float>( base ) );
					}
				}
			}
			for( int k : tileSegments[t] )
			{
				double xMin, xMax, yMin, yMax;
				segmentBox( k, xMin, xMax, yMin, yMax );
				// Тексели плитки в рамке отрезка
				const int i0 = std::max( static_cast<int>( std::floor( ( xMin - tile.x * tileCells ) / step + 0.5 ) ), 0 );
				const int i1 = std::min( static_cast<int>( std::ceil( ( xMax - tile.x * tileCells ) / step + 0.5 ) ), side - 1 );
				const int j0 = std::max( static_cast<int>( std::floor( ( n - yMax - tile.z * tileCells ) / step + 0.5 ) ), 0 );
				const int j1 = std::min( static_cast<int>( std::ceil( ( n - yMin - tile.z * tileCells ) / step + 0.5 ) ), side - 1 );
				for( int j = j0; j <= j1; ++j )
				{
					for( int i = i0; i <= i1; ++i )
					{
						const Point at = texelCell( tile, i, j );
						const size_t index = static_cast<size_t>( j ) * side + i;
						double base = 0.0;
						carved[index] = std::max( carved[index], segmentProfile( k, at.x, at.y, base ) );
						tileBase[t][index] = std::max( tileBase[t][index], static_cast<float>( base ) );
					}
				}
			}
			for( int j = 0; j < side; ++j )
			{
				for( int i = 0; i < side; ++i )
				{
					const Point at = texelCell( tile, i, j );
					const int col = std::clamp( static_cast<int>( at.x ), 0, n - 1 );
					const int row = std::clamp( static_cast<int>( at.y ), 0, n - 1 );
					const size_t index = static_cast<size_t>( j ) * side + i;
					// В озере — чаша или подводное русло, что глубже; вне озера — русла и подводные русла через берег
					const double lowering = std::max( lake[static_cast<size_t>( row ) * n + col] ? basinGrid.sample( at.x - 0.5, at.y - 0.5 )
																								 : carved[index], mouthCarved[index] );
					tile.heights[index] = static_cast<float>( catmullRom( height, at.x - 0.5, at.y - 0.5 ) - lowering );
					tile.carve[index] = static_cast<float>( lowering );
				}
			}
		} );
		// Клетка карты — не выше самой низкой детальной точки в ней: узкое русло, которое не задело центр клетки, всё равно
		// опускает её — уровень воды (дно у оси) и глубина воды на сетке 1 м — по детальному ложу
		for( size_t t = 0; t < result.detailTiles.size(); ++t )
		{
			const DetailTile& tile = result.detailTiles[t];
			for( int j = 1; j < side - 1; ++j )
			{
				for( int i = 1; i < side - 1; ++i )
				{
					const Point at = texelCell( tile, i, j );
					const int col = static_cast<int>( at.x );
					const int row = static_cast<int>( at.y );
					if( col < 0 || row < 0 || col >= n || row >= n )
						continue;
					const size_t cellIndex = static_cast<size_t>( row ) * n + col;
					if( lake[cellIndex] )
						continue;
					const size_t index = static_cast<size_t>( j ) * side + i;
					carve[cellIndex] = std::max( carve[cellIndex], static_cast<double>( tile.carve[index] ) );
					carveBase[cellIndex] = std::max( carveBase[cellIndex], static_cast<double>( tileBase[t][index] ) );
				}
			}
		}
	}
	result.size = static_cast<uint32_t>( n );
	result.lowering.resize( cells );
	result.carvedCells = 0;
	result.deepestLowering = 0.0f;
	Grid carved = height;
	for( size_t i = 0; i < cells; ++i )
	{
		result.lowering[i] = static_cast<float>( -carve[i] );
		carved.values[i] -= carveBase[i];
		if( carve[i] > 0.01 )
			++result.carvedCells;
		result.deepestLowering = std::min( result.deepestLowering, result.lowering[i] );
	}
	std::vector<double> width( count ), reach( count );
	for( int k = 0; k < count; ++k )
	{
		width[k] = 2.0 * half[k] * cell;
		reach[k] = ( half[k] + bank[k] ) * cell;
	}

	// Водосбор вдоль оси русла — приток симуляции (режим simulated): площадь узла переносится на ось шагами по полметра,
	// иначе приток лился бы вдоль прежних прямых линий D8 рядом с руслом
	result.channelFlow.assign( dr.area.begin(), dr.area.end() );
	for( int k = 0; k < count; ++k )
		result.channelFlow[cellOf[k]] = 0.0f;
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k] >= 0 ? down[k] : k;
		const Point a = position[k];
		const Point b = position[d];
		const int steps = std::max( static_cast<int>( std::ceil( std::hypot( b.x - a.x, b.y - a.y ) * 2.0 ) ), 1 );
		for( int s = 0; s <= steps; ++s )
		{
			const double f = static_cast<double>( s ) / steps;
			const int col = static_cast<int>( std::clamp( a.x + ( b.x - a.x ) * f, 0.0, n - 1.0 ) );
			const int row = static_cast<int>( std::clamp( a.y + ( b.y - a.y ) * f, 0.0, n - 1.0 ) );
			float& value = result.channelFlow[static_cast<size_t>( row ) * n + col];
			value = std::max( value, static_cast<float>( nodeArea[k] ) );
		}
	}

	// 5. Вода — одно поле уровня по сети: в узле — своя вода (дно у оси — тальвег детальной земли в узле — плюс глубина по
	// Маннингу для прямоугольного русла h = (Q·n / (w·√S))^(3/5), не мельче min_water_depth и ниже бровки на запас), но не
	// ниже воды ниже по течению — подпор. Озеро — вода ниже по течению у впадающих в него ручьёв и уровень его истока:
	// ручьи в нём и у берега — на том же уровне подпором, а не правилами на стыках
	std::vector<double> slope( count, p.minWaterSlope ), level( count ), speed( count ), foam( count ), actual( count );
	std::vector<double> hydraulicRadius( count, 0.0 );	// R русла у узла, м — течение детальной воды (water_flow.cs)
	// Дно, от которого стоит вода, — видимое, детальное (билинейно по текселям плитки; вне плиток — сетка 1 м), а не клетки
	// карты: клетка опущена до самой низкой детальной точки в ней, на крутом это её нижний край — вода выходила на
	// 10–20 см мельче, и гребень переката оставался почти сухим (2026-10-10). Тальвег — наименьшее поперёк направления
	// (tx, ty) на ±0,75 клетки (тальвег мог уйти от оси), но не вдоль: на крутом точка ниже по склону опустила бы воду под
	// землю
	const int detailTiles = static_cast<int>( ( n + detailTileCells - 1 ) / detailTileCells );
	std::vector<int> detailIndex( static_cast<size_t>( detailTiles ) * detailTiles, -1 );
	for( size_t t = 0; t < result.detailTiles.size(); ++t )
		detailIndex[static_cast<size_t>( result.detailTiles[t].z ) * detailTiles + result.detailTiles[t].x] = static_cast<int>( t );
	auto bedAt = [&]( double x, double y )
	{
		const int tx = static_cast<int>( std::floor( x / detailTileCells ) );
		const int tz = static_cast<int>( std::floor( ( n - y ) / detailTileCells ) );
		if( tx < 0 || tz < 0 || tx >= detailTiles || tz >= detailTiles || detailIndex[static_cast<size_t>( tz ) * detailTiles + tx] < 0 )
			return carved.sample( x - 0.5, y - 0.5 );
		const DetailTile& tile = result.detailTiles[detailIndex[static_cast<size_t>( tz ) * detailTiles + tx]];
		// Тексель (i, j) — в клетках ( x · tileCells + ( i − 0,5 ) / samples, n − z · tileCells − ( j − 0,5 ) / samples )
		const int side = static_cast<int>( detailTileCells * detailSamples + 2 );
		const double fi = ( x - static_cast<double>( tile.x * detailTileCells ) ) * detailSamples + 0.5;
		const double fj = ( n - y - static_cast<double>( tile.z * detailTileCells ) ) * detailSamples + 0.5;
		const int i = std::clamp( static_cast<int>( std::floor( fi ) ), 0, side - 2 );
		const int j = std::clamp( static_cast<int>( std::floor( fj ) ), 0, side - 2 );
		const double u = std::clamp( fi - i, 0.0, 1.0 );
		const double v = std::clamp( fj - j, 0.0, 1.0 );
		auto texel = [&]( int a, int b ) { return static_cast<double>( tile.heights[static_cast<size_t>( b ) * side + a] ); };
		return ( texel( i, j ) * ( 1.0 - u ) + texel( i + 1, j ) * u ) * ( 1.0 - v ) +
			   ( texel( i, j + 1 ) * ( 1.0 - u ) + texel( i + 1, j + 1 ) * u ) * v;
	};
	auto thalweg = [&]( const Point& at, double tx, double ty )
	{
		double lowest = std::numeric_limits<double>::infinity();
		for( double offset : { -0.75, -0.5, -0.25, 0.0, 0.25, 0.5, 0.75 } )
			lowest = std::min( lowest, bedAt( at.x - ty * offset, at.y + tx * offset ) );
		return lowest;
	};
	std::vector<double> needDepth( count, 0.0 );
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k];
		if( d >= 0 )
		{
			const double step = std::max( std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell, 1e-3 );
			slope[k] = std::max( ( bed[k] - bed[d] ) / step, static_cast<double>( p.minWaterSlope ) );
		}
		const double waterDepth = std::pow( q[k] * shape[k].manning / ( width[k] * std::sqrt( slope[k] ) ), 0.6 );
		{
			const Point& from = position[up[k] >= 0 ? up[k] : k];
			const Point& to = position[d >= 0 ? d : k];
			double tx = to.x - from.x;
			double ty = to.y - from.y;
			const double length = std::hypot( tx, ty );
			if( length < 1e-6 )
			{
				tx = 1.0;
				ty = 0.0;
			}
			else
			{
				tx /= length;
				ty /= length;
			}
			actual[k] = thalweg( position[k], tx, ty );
		}
		// У истока и устья наименьшая глубина и запас бровки нарастают от берега озера (как врез у истока): на пороге вода —
		// тонкий слив, в устье — подпор озера
		const double ramp = std::min( outletDistance[k], inflowDistance[k] ) * outletSlope;
		const double minDepth = std::min( p.minWaterDepth * ( 0.33 + 0.67 * sourceShare[k] ), ramp );
		needDepth[k] = minDepth;
		const double freeboard = std::min( shape[k].freeboard, ramp );
		level[k] = actual[k] + std::max( waterDepth, minDepth );
		level[k] = std::max( std::min( level[k], surface[k] - freeboard ), actual[k] + std::min( 0.05, minDepth ) );
	}
	// Озеро стоит до перелива своей низины (fillLevel): это вода ниже по течению у узлов в озере и у ручьёв, которые в него
	// впадают; исток на пороге — тоже на уровне перелива (дальше вода сходит по истоку вниз)
	const std::vector<double>& lakeSurface = fillLevel;
	for( int k = 0; k < count; ++k )
	{
		const int label = inLake( k ) ? lakeLabel[cellOf[k]] : endLake[k];
		if( label >= 0 )
			level[k] = std::max( level[k], lakeSurface[label] );
	}
	for( int label = 0; label < lakeCount; ++label )
		if( lakeOutlet[label] >= 0 )
			level[lakeOutlet[label]] = lakeSurface[label];
	// Подпор: снизу вверх по сети уровень — не ниже воды ниже по течению. Между узлами уровень растра — прямая, а дно под ней
	// — детальное: гребень переката посреди отрезка выходил выше воды, и вода «проходила сквозь» подъём русла (2026-10-10).
	// Поэтому тальвег отрезка проверяется через тексель детальной земли: под прямой уровня он не мельче наименьшей глубины
	// концов; нехватку добирает ближний конец (верхний — с запасом 1 / (1 − f), нижний — 1 / f). Подъём нижнего снова
	// поднимает подпором верхние — проходы до устойчивого. Уровень узлов в озере и истока на пороге — перелив, не поднимается
	std::vector<uint8_t> pinned( count, 0 );
	for( int k = 0; k < count; ++k )
		pinned[k] = inLake( k ) ? 1 : 0;
	for( int label = 0; label < lakeCount; ++label )
		if( lakeOutlet[label] >= 0 )
			pinned[lakeOutlet[label]] = 1;
	for( int pass = 0; pass < 8; ++pass )
	{
		bool raisedBelow = false;
		for( auto it = nodeOrder.rbegin(); it != nodeOrder.rend(); ++it )
		{
			const int k = *it;
			const int d = down[k];
			if( d < 0 )
				continue;
			level[k] = std::max( level[k], level[d] );
			if( inLake( k ) || inLake( d ) )
				continue;
			const Point a = position[k];
			const Point b = position[d];
			const double length = std::hypot( b.x - a.x, b.y - a.y );
			if( length < 1e-6 )
				continue;
			const double tx = ( b.x - a.x ) / length;
			const double ty = ( b.y - a.y ) / length;
			const int steps = std::max( static_cast<int>( std::ceil( length * detailSamples ) ), 1 );
			for( int s = 1; s < steps; ++s )
			{
				const double f = static_cast<double>( s ) / steps;
				const Point at{ a.x + ( b.x - a.x ) * f, a.y + ( b.y - a.y ) * f };
				const double need = thalweg( at, tx, ty ) + needDepth[k] + ( needDepth[d] - needDepth[k] ) * f;
				const double shortfall = need - ( level[k] + ( level[d] - level[k] ) * f );
				if( shortfall <= 1e-4 )
					continue;
				// Нижний — если гребень ближе к нему и подъём не выше верхнего (вода в гору не течёт)
				const bool raiseLower = f > 0.5 && !pinned[d] && level[d] + shortfall / f <= level[k];
				if( !raiseLower && !pinned[k] )
					level[k] += shortfall / ( 1.0 - f );
				else if( raiseLower )
				{
					level[d] += shortfall / f;
					raisedBelow = true;
				}
			}
		}
		if( !raisedBelow )
			break;
	}
	// Скорость — по Маннингу от видимой глубины и уклона поверхности: v = R^(2/3)·√S / n, R = w·h / (w + 2h). Не Q / (w·h):
	// глубина не мельче min_water_depth (воду видно), а расход водосбора карты 1 км на такой глубине — сантиметры в секунду,
	// и вся сеть упиралась в min_speed — одна скорость на крутом и на ровном (2026-10-10). Где глубина — по расходу (крупный
	// ручей), это то же Q / (w·h). Уклон — по уровню воды на ±speedSpan вдоль сети: перекат быстрее, плёс и подпор у озера
	// медленнее
	auto walk = [&]( int from, const std::vector<int>& next, double span, double& distance )
	{
		distance = 0.0;
		while( distance < span && next[from] >= 0 )
		{
			const int to = next[from];
			distance += std::hypot( position[to].x - position[from].x, position[to].y - position[from].y ) * cell;
			from = to;
		}
		return from;
	};
	for( int k = 0; k < count; ++k )
	{
		double below = 0.0, above = 0.0;
		// На ступенчатом участке — не короче шага ступеней: на уступе уровень обрывается, и уклон по нему — не уклон участка
		const double span = stepHeight[k] > 0.0 ? std::max( speedSpan, stepLength[k] ) : speedSpan;
		const int lower = walk( k, down, span, below );
		const int upper = walk( k, up, span, above );
		const double surfaceSlope = below + above > 1e-3 ? ( level[upper] - level[lower] ) / ( below + above ) : slope[k];
		// По Маннингу — для опорной глубины участка (в том же окне), у узла — по неразрывности: расход участка (скорость ×
		// опорная глубина) на его глубину. Опорная — средняя, на ступенчатом участке — над гребнем (наименьшая): расход
		// ступеней задают гребни уступов (там поток проходит критическую глубину), плёсы его не ускоряют. В плёсе вода
		// глубже — медленнее, на гребне — быстрее (не больше чем вдвое); на ровном участке глубина узла — средняя, и скорость
		// — по Маннингу, как была. По глубине узла Маннинг давал обратное: в плёсе за уступом вода текла быстрее, и средняя
		// по ручьям со ступенями выросла с 1,08 до 1,33 м/с (по средней глубине — до 1,43)
		const double depth = std::max( level[k] - actual[k], 1e-3 );
		double depthSum = 0.0, lengthSum = 0.0, shallowest = depth;
		for( int at = upper; at != lower && down[at] >= 0; at = down[at] )
		{
			const int next = down[at];
			const double step = std::hypot( position[next].x - position[at].x, position[next].y - position[at].y ) * cell;
			depthSum += 0.5 * ( std::max( level[at] - actual[at], 0.0 ) + std::max( level[next] - actual[next], 0.0 ) ) * step;
			lengthSum += step;
			shallowest = std::min( shallowest, std::max( level[next] - actual[next], 1e-3 ) );
		}
		const double stepped = std::clamp( stepHeight[k] / std::max( static_cast<double>( p.stepHeight ), 1e-3 ), 0.0, 1.0 );
		const double average = lengthSum > 1e-3 ? std::max( depthSum / lengthSum, 1e-3 ) : depth;
		const double mean = average + ( shallowest - average ) * stepped;
		const double meanRadius = width[k] * mean / ( width[k] + 2.0 * mean );
		hydraulicRadius[k] = width[k] * depth / ( width[k] + 2.0 * depth );
		const double meanSpeed = std::pow( meanRadius, 2.0 / 3.0 ) *
								 std::sqrt( std::max( surfaceSlope, static_cast<double>( p.minWaterSlope ) ) ) / shape[k].manning;
		speed[k] = std::max( meanSpeed * mean / std::max( depth, 0.5 * mean ), static_cast<double>( p.minSpeed ) );
		foam[k] = std::clamp( ( slope[k] - p.foamSlope ) / ( 2.0 * p.foamSlope ), 0.0, 1.0 ) * 0.6;
	}

	// 6. Камни русла — часть кривой: уступы ступеней, на дне (гуще на перекатах и на крутом), у бортов — у уреза, и редкие
	// валуны. Место случайное, но от положения отрезка: тот же камень на том же месте при каждой загрузке и после правки
	// других кривых; внахлёст не ставятся. Высота — самое низкое детальное дно под камнем: на борту камень не висит над
	// склоном
	result.stones.clear();
	{
		std::vector<Point> stoneCells;		// центры камней в клетках карты и радиусы, клеток — проверка нахлёста
		std::vector<double> stoneRadii;
		std::unordered_map<int64_t, std::vector<size_t>> buckets;
		auto bucket = [n]( int col, int row ) { return static_cast<int64_t>( row + 1 ) * ( n + 2 ) + col + 1; };
		auto overlaps = [&]( const Point& at, double radius )
		{
			const int col = static_cast<int>( std::floor( at.x ) );
			const int row = static_cast<int>( std::floor( at.y ) );
			for( int dr = -2; dr <= 2; ++dr )
				for( int dc = -2; dc <= 2; ++dc )
					if( const auto it = buckets.find( bucket( col + dc, row + dr ) ); it != buckets.end() )
						for( size_t i : it->second )
							if( std::hypot( stoneCells[i].x - at.x, stoneCells[i].y - at.y ) < 0.9 * ( stoneRadii[i] + radius ) )
								return true;
			return false;
		};
		auto addStone = [&]( const Point& at, double size, uint32_t seed, double bottom )
		{
			result.stones.push_back( { DirectX::XMFLOAT3( static_cast<float>( at.x * cell ), static_cast<float>( bottom ),
														  static_cast<float>( ( n - at.y ) * cell ) ),
									   static_cast<float>( size ), seed } );
			buckets[bucket( static_cast<int>( std::floor( at.x ) ), static_cast<int>( std::floor( at.y ) ) )].push_back( stoneCells.size() );
			stoneCells.push_back( at );
			stoneRadii.push_back( 0.5 * size / cell );
		};
		// Камни-ступени: уступ ступени сложен камнями (keystones) — ряд поперёк дна на стенке уступа, каждый — около 1,4
		// высоты уступа (не меньше stone_size, не больше boulder_size и половины дна), с пропусками: ступень не ровная
		// стенка. Камень шире половины дна в узком русле у родника стоял один посередине и загораживал ручей (2026-10-11).
		// Раньше остальных камней — они не займут места на уступе. Высота — дно под центром и по бокам поперёк течения
		// (не вдоль: камень на стенке опустился бы в котёл)
		for( int k = 0; k < count; ++k )
		{
			const int d = down[k];
			if( d < 0 || inLake( k ) || inLake( d ) || up[d] != k || std::max( stepHeight[k], stepHeight[d] ) <= 0.0 )
				continue;
			const Point a = position[k];
			const Point b = position[d];
			const double length = std::hypot( b.x - a.x, b.y - a.y );
			if( length < 1e-6 || stepPhase[d] <= stepPhase[k] )
				continue;
			const double tx = ( b.x - a.x ) / length;
			const double ty = ( b.y - a.y ) / length;
			// Середина стенки уступа — фаза m − face / 2 (уступ — перед целой фазой)
			const double face = std::clamp( stepFace / std::max( stepLength[k], 1e-3 ), 0.05, 0.4 );
			for( double m = std::floor( stepPhase[k] + 0.5 * face ) + 1.0; m - 0.5 * face < stepPhase[d]; m += 1.0 )
			{
				const double t = ( m - 0.5 * face - stepPhase[k] ) / ( stepPhase[d] - stepPhase[k] );
				if( t < 0.0 || t >= 1.0 )
					continue;
				const double height = stepHeight[k] + ( stepHeight[d] - stepHeight[k] ) * t;
				const double halfWidth = half[k] + ( half[d] - half[k] ) * t;
				if( height < 0.05 )
					continue;
				StoneRandom random( static_cast<uint64_t>( std::llround( a.x * 16.0 ) ) * 73856093ull ^
									static_cast<uint64_t>( std::llround( a.y * 16.0 ) ) * 19349663ull ^ static_cast<uint64_t>( m ) * 83492791ull );
				const double width = 2.0 * halfWidth * cell;
				const double size = std::min( std::clamp( 1.4 * height * ( 0.85 + 0.3 * random.uniform() ), static_cast<double>( p.stoneSize ),
														  std::max( static_cast<double>( p.boulderSize ), static_cast<double>( p.stoneSize ) ) ),
											  0.5 * width );
				const int stones = std::max( static_cast<int>( std::lround( width / ( 0.85 * size ) ) ), 1 );
				const Point centre{ a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t };
				for( int i = 0; i < stones; ++i )
				{
					if( random.uniform() < 0.15 )
						continue;
					const double stoneSize = std::min( size * ( 0.8 + 0.4 * random.uniform() ), 0.5 * width );
					const double across = stones > 1 ? ( ( i + 0.5 ) / stones * 2.0 - 1.0 ) * ( halfWidth - 0.5 * stoneSize / cell ) : 0.0;
					const double along = ( random.uniform() - 0.5 ) * 0.2 * stoneSize / cell;
					const Point at{ centre.x - ty * across + tx * along, centre.y + tx * across + ty * along };
					if( at.x < 1.0 || at.y < 1.0 || at.x > n - 1.0 || at.y > n - 1.0 || lake[cellIndex( at, n )] )
						continue;
					const double radius = 0.5 * stoneSize / cell;
					if( overlaps( at, radius ) )
						continue;
					double bottom = bedAt( at.x, at.y );
					for( const double side : { -0.5, 0.5 } )
						bottom = std::min( bottom, bedAt( at.x - ty * radius * side, at.y + tx * radius * side ) );
					addStone( at, stoneSize, random.next(), bottom );
				}
			}
		}
		for( int k = 0; k < count; ++k )
		{
			const int d = down[k];
			if( d < 0 || inLake( k ) || inLake( d ) )
				continue;
			const Point a = position[k];
			const Point b = position[d];
			const double length = std::hypot( b.x - a.x, b.y - a.y );
			if( length < 1e-6 )
				continue;
			const double tx = ( b.x - a.x ) / length;
			const double ty = ( b.y - a.y ) / length;
			StoneRandom random( static_cast<uint64_t>( std::llround( a.x * 16.0 ) ) * 73856093ull ^
								static_cast<uint64_t>( std::llround( a.y * 16.0 ) ) * 19349663ull );
			// Перекат — где фаза плёсов у дна выше (Leopold, Wolman 1957: на перекатах крупнее и гуще); крутое русло —
			// ступенчатое, камней больше; у родника — реже и мельче; у истока из озера и в устье — почти нет: там слив и подпор
			const double riffle = 0.5 * ( 1.0 - std::sin( 2.0 * pi * poolPhase[k] ) );
			const double steep = std::clamp( slope[k] / 0.1, 0.0, 2.0 );
			const double ramp = std::clamp( std::min( outletDistance[k], inflowDistance[k] ) / 3.0, 0.0, 1.0 );
			const double share = ( 0.3 + 0.7 * sourceShare[k] ) * shape[k].stones * ramp;
			const double metres = length * cell;
			const double expected[3] = {
				metres * p.stoneDensity * share * ( 1.0 - p.bankStones ) * ( 0.3 + 0.7 * riffle ) * ( 0.6 + 0.7 * steep ),
				metres * p.stoneDensity * share * p.bankStones,
				metres * p.boulderDensity * share * ( 0.5 + 0.75 * steep ) * ( 2.0 * half[k] * cell >= boulderMinBedWidth ? 1.0 : 0.0 ) };
			for( int kind = 0; kind < 3; ++kind )
			{
				const int stones = static_cast<int>( std::floor( expected[kind] + random.uniform() ) );
				for( int i = 0; i < stones; ++i )
				{
					const double t = random.uniform();
					// Поперёк оси (клетки, > 0 — слева по течению): дно — по ширине дна; борт — у подошвы и чуть выше, к урезу;
					// валун — у борта: край — у подошвы. Размер — около stone_size с разбросом, на перекате и на крутом крупнее;
					// камень дна и валун — не шире половины дна, камень борта — не шире дна (он наполовину на борту): русло
					// камень не загораживает
					double offset = 0.0;
					double size = 0.0;
					if( kind == 0 )
					{
						offset = ( 2.0 * random.uniform() - 1.0 ) * 0.85 * half[k];
						size = p.stoneSize * std::exp( 0.35 * random.gauss() ) * ( 0.8 + 0.4 * riffle ) * ( 0.85 + 0.25 * steep );
						size = std::min( size, half[k] * cell );
					}
					else if( kind == 1 )
					{
						const double side = random.uniform() < 0.5 ? -1.0 : 1.0;
						// Случайные числа — отдельными операторами: порядок вычисления слагаемых не задан, и Debug и Release
						// брали их в разном порядке — камни и ключ кэша течения расходились
						const double toe = 0.8 + 0.25 * random.uniform();
						const double up = 0.3 * random.uniform();
						offset = side * ( half[k] * toe + bank[k] * up );
						size = std::min( p.stoneSize * 1.3 * std::exp( 0.35 * random.gauss() ), 2.0 * half[k] * cell );
					}
					else
					{
						size = std::min( p.boulderSize * ( 0.75 + 0.5 * random.uniform() ), half[k] * cell );
						const double side = random.uniform() < 0.5 ? -1.0 : 1.0;
						offset = side * std::max( half[k] - 0.5 * size / cell * ( 0.8 + 0.4 * random.uniform() ), 0.0 );
					}
					size = std::clamp( size, 0.06, 3.0 );
					const uint32_t seed = random.next();
					const Point at{ a.x + ( b.x - a.x ) * t - ty * offset, a.y + ( b.y - a.y ) * t + tx * offset };
					if( at.x < 1.0 || at.y < 1.0 || at.x > n - 1.0 || at.y > n - 1.0 || lake[cellIndex( at, n )] )
						continue;
					const double radius = 0.5 * size / cell;
					if( overlaps( at, radius ) )
						continue;
					double bottom = bedAt( at.x, at.y );
					for( const Point& around : { Point{ radius * 0.5, 0.0 }, Point{ -radius * 0.5, 0.0 }, Point{ 0.0, radius * 0.5 },
												 Point{ 0.0, -radius * 0.5 } } )
						bottom = std::min( bottom, bedAt( at.x + around.x, at.y + around.y ) );
					addStone( at, size, seed, bottom );
				}
			}
		}
	}

	// Кривая, которая на нынешнем рельефе идёт в гору (земля под ней сменилась: новая эрозия, другая карта, другая машина) —
	// предупреждение: дно монотонно вниз, и русло там прорежется траншеей. Правленные и ручные — по одной, сгенерированные —
	// одной строкой с подсказкой: их генерация от рельефа не зависит (generationKey)
	size_t generatedUphill = 0;
	double generatedRise = 0.0;
	for( size_t a = 0; a < lines.size(); ++a )
	{
		const StreamCurve& curve = *lines[a].curve;
		if( lineNodes[a].empty() )
			continue;
		double lowest = std::numeric_limits<double>::infinity();
		double rise = 0.0;
		int at = lineNodes[a].front();
		for( int k : lineNodes[a] )
		{
			if( surface[k] - lowest > rise )
			{
				rise = surface[k] - lowest;
				at = k;
			}
			lowest = std::min( lowest, surface[k] );
		}
		if( rise <= 0.5 )
			continue;
		if( curve.generated && !curve.edited )
		{
			++generatedUphill;
			generatedRise = std::max( generatedRise, rise );
			continue;
		}
		char text[256];
		std::snprintf( text, sizeof( text ), "stream \"%s\" runs uphill by %.1f m near %.0f, %.0f: the terrain under the edited curve changed?",
					   curve.name.c_str(), rise, position[at].x * cell, world - position[at].y * cell );
		result.warnings.push_back( text );
	}
	if( generatedUphill > 0 )
	{
		char text[320];
		std::snprintf( text, sizeof( text ), "%zu generated stream curves run uphill (up to %.1f m): the terrain changed since they were generated? "
					   "Regenerate: python Tools/stream_edit.py regenerate", generatedUphill, generatedRise );
		result.warnings.push_back( text );
	}

	// Кривая, которая обрывается: последний узел не переходит в другое русло, не в озере и не у его берега (endLake — в трёх
	// клетках) и не у края карты. Так бывает, когда рельеф сменился после генерации (там было озеро или слияние): вода ниже
	// идёт без русла. Правленные и ручные — по одной, сгенерированные — одной строкой с подсказкой, как «в гору»
	constexpr int edgeCells = 3;
	size_t generatedDangling = 0;
	std::string danglingExamples;
	for( size_t a = 0; a < lines.size(); ++a )
	{
		if( lineNodes[a].empty() )
			continue;
		const int last = lineNodes[a].back();
		if( down[last] >= 0 || inLake( last ) || endLake[last] >= 0 )
			continue;
		const int col = cellOf[last] % n;
		const int row = cellOf[last] / n;
		if( col < edgeCells || row < edgeCells || col >= n - edgeCells || row >= n - edgeCells )
			continue;
		const StreamCurve& curve = *lines[a].curve;
		char text[256];
		if( curve.generated && !curve.edited )
		{
			if( ++generatedDangling <= 5 )
			{
				std::snprintf( text, sizeof( text ), "%s\"%s\" at %.0f, %.0f", danglingExamples.empty() ? "" : "; ", curve.name.c_str(),
							   position[last].x * cell, world - position[last].y * cell );
				danglingExamples += text;
			}
			continue;
		}
		std::snprintf( text, sizeof( text ), "stream \"%s\" ends near %.0f, %.0f not at a confluence, lake or map edge: the water "
					   "below runs without a channel", curve.name.c_str(), position[last].x * cell, world - position[last].y * cell );
		result.warnings.push_back( text );
	}
	if( generatedDangling > 0 )
	{
		std::string text = std::to_string( generatedDangling ) + " generated stream curves end not at a confluence, lake or map edge (" +
						   danglingExamples + ( generatedDangling > 5 ? "; ..." : "" ) + "): the terrain changed since they were "
						   "generated? Regenerate: python Tools/stream_edit.py regenerate";
		result.warnings.push_back( text );
	}

	// Ручьи растра — по кривым: от истока (или узла ниже озера) до слияния, озера или края; кривая, проходящая озеро, рвётся в нём
	std::vector<std::vector<int>> chains;
	for( size_t a = 0; a < lineNodes.size(); ++a )
	{
		std::vector<int> chain;
		for( int k : lineNodes[a] )
		{
			if( inLake( k ) )
			{
				if( !chain.empty() )
				{
					chain.push_back( k );
					chains.push_back( std::move( chain ) );
				}
				chain.clear();
				continue;
			}
			chain.push_back( k );
		}
		if( !chain.empty() && down[chain.back()] >= 0 )
			chain.push_back( down[chain.back()] );
		if( chain.size() >= 2 )
			chains.push_back( std::move( chain ) );
	}

	// Растр статичной воды — одно поле: у клетки — уровень и течение ближайшего отрезка ручья в пределах его русла, уровень
	// озера (ниже); поверхность воды по нему рисует WaterSimulation (тайлы)
	result.staticWater.assign( cells, DirectX::XMFLOAT4( -1e9f, 0.0f, 0.0f, -1e9f ) );
	std::vector<double> nearest( cells, std::numeric_limits<double>::infinity() );
	// Детальная вода — тем же обходом отрезков по текселям детальных плиток (0,25 м, с полем в тексель): уровень, течение
	// и гидравлический радиус ближайшего отрезка; уровень озера — после заливки озёр
	const int detailSide = static_cast<int>( detailTileCells * detailSamples + 2 );
	const double detailStep = 1.0 / detailSamples;
	std::vector<std::vector<double>> detailNearest( result.detailTiles.size() );
	for( size_t t = 0; t < result.detailTiles.size(); ++t )
	{
		result.detailTiles[t].water.assign( static_cast<size_t>( detailSide ) * detailSide, DirectX::XMFLOAT4( -1e9f, 0.0f, 0.0f, 0.0f ) );
		detailNearest[t].assign( static_cast<size_t>( detailSide ) * detailSide, std::numeric_limits<double>::infinity() );
	}
	// Доля перепада уровня отрезка k → d в доле t: на ступенчатом участке перепад — на стенке уступа (там же, где уступ дна,
	// stepOffset: от гребня до котла), а не по прямой на весь отрезок: в плёсе вода ровная, на уступе — слив каскадом.
	// Где ступени сходят на нет, — к прямой. Узлы разных ручьёв (приток у слияния) — прямая: фазы ступеней у них свои
	auto levelShare = [&]( int k, int d, double t )
	{
		const double stepped = std::clamp( std::max( stepHeight[k], stepHeight[d] ) / std::max( static_cast<double>( p.stepHeight ), 1e-3 ), 0.0, 1.0 );
		if( stepped <= 0.0 || up[d] != k )
			return t;
		const double face = std::clamp( stepFace / std::max( stepLength[k], 1e-3 ), 0.05, 0.4 );
		auto passed = [&]( double phase )
		{
			const double index = std::floor( phase );
			return index + smoothstep( 1.0 - face, 1.0, phase - index );
		};
		const double from = passed( stepPhase[k] );
		const double to = passed( stepPhase[d] );
		if( to - from < 1e-3 )
			return t;
		const double share = ( passed( stepPhase[k] + ( stepPhase[d] - stepPhase[k] ) * t ) - from ) / ( to - from );
		return t + ( share - t ) * stepped;
	};
	auto detailTexel = [&]( const DetailTile& tile, int i, int j )
	{
		return Point{ tile.x * static_cast<double>( detailTileCells ) + ( i - 0.5 ) * detailStep,
					  n - ( tile.z * static_cast<double>( detailTileCells ) + ( j - 0.5 ) * detailStep ) };
	};
	result.streamCount = 0;
	result.points = 0;
	for( const std::vector<int>& chain : chains )
	{
		// Точки ручья и узлы, чьи уровень, течение и ширина у точки. Подтекающий в главный кончается на урезе главного (где
		// его вода встречает борт), а не на оси: клетки главного — с течением главного. Точки внутри уреза отбрасываются,
		// конец — на пересечении с ним
		std::vector<int> nodes = chain;
		std::vector<Point> points( chain.size() );
		for( size_t i = 0; i < chain.size(); ++i )
			points[i] = position[chain[i]];
		const int last = chain.back();
		const int previous = chain[chain.size() - 2];
		if( up[last] != previous && !lake[cellOf[last]] )
		{
			const Point centre = position[last];
			// Урез главного: дно шириной half, выше — борт; вода стоит на глубине level − actual над дном
			const double waterShare = std::clamp( ( level[last] - actual[last] ) / std::max( depth[last], 1e-3 ), 0.0, 1.0 );
			const double radius = half[last] + bank[last] * waterShare;
			auto inside = [&]( const Point& point ) { return std::hypot( point.x - centre.x, point.y - centre.y ) < radius; };
			size_t keep = points.size() - 1;	// последняя точка снаружи ленты главного
			while( keep > 0 && inside( points[keep] ) )
				--keep;
			if( keep == 0 && inside( points[0] ) )
				continue;	// весь приток — в ленте главного
			// Конец — на отрезке от последней наружной точки к следующей, на краю ленты главного
			const Point a = points[keep];
			const Point b = points[keep + 1];
			const double dx = b.x - a.x;
			const double dy = b.y - a.y;
			const double fx = a.x - centre.x;
			const double fy = a.y - centre.y;
			const double qa = dx * dx + dy * dy;
			const double qb = 2.0 * ( fx * dx + fy * dy );
			const double qc = fx * fx + fy * fy - radius * radius;
			const double root = std::sqrt( std::max( qb * qb - 4.0 * qa * qc, 0.0 ) );
			const double t = qa > 0.0 ? std::clamp( ( -qb - root ) / ( 2.0 * qa ), 0.0, 1.0 ) : 0.0;
			points.resize( keep + 2 );
			nodes.resize( keep + 2 );
			points.back() = { a.x + dx * t, a.y + dy * t };
			if( t <= 0.0 )
			{
				points.pop_back();
				nodes.pop_back();
			}
			if( points.size() < 2 )
				continue;
		}
		// Уровень у точек — поле уровня сети. Ручей, впадающий в озеро, кончается на берегу (последний узел — уже в озере:
		// там вода — поверхность озера; уровень у берега и так озёрный — подпор)
		std::vector<double> pointLevel( points.size() );
		for( size_t i = 0; i < points.size(); ++i )
			pointLevel[i] = level[nodes[i]];
		if( lake[cellOf[nodes.back()]] )
		{
			points.pop_back();
			nodes.pop_back();
			pointLevel.pop_back();
			if( points.size() < 2 )
				continue;
		}

		for( size_t i = 0; i + 1 < points.size(); ++i )
		{
			const int k = nodes[i];
			const int d = nodes[i + 1];
			const Point a = points[i];
			const Point b = points[i + 1];
			const double radius = std::max( reach[k], reach[d] ) / cell + 0.5;
			const int c0 = static_cast<int>( std::max( std::floor( std::min( a.x, b.x ) - radius ), 0.0 ) );
			const int c1 = static_cast<int>( std::min( std::ceil( std::max( a.x, b.x ) + radius ), n - 1.0 ) );
			const int r0 = static_cast<int>( std::max( std::floor( std::min( a.y, b.y ) - radius ), 0.0 ) );
			const int r1 = static_cast<int>( std::min( std::ceil( std::max( a.y, b.y ) + radius ), n - 1.0 ) );
			const double sx = b.x - a.x;
			const double sy = b.y - a.y;
			const double length2 = std::max( sx * sx + sy * sy, 1e-9 );
			const double length = std::max( std::sqrt( length2 ), 1e-6 );
			for( int row = r0; row <= r1; ++row )
			{
				for( int col = c0; col <= c1; ++col )
				{
					const double cx = col + 0.5;
					const double cy = row + 0.5;
					const double t = std::clamp( ( ( cx - a.x ) * sx + ( cy - a.y ) * sy ) / length2, 0.0, 1.0 );
					const double distance = std::hypot( cx - ( a.x + t * sx ), cy - ( a.y + t * sy ) );
					const size_t index = static_cast<size_t>( row ) * n + col;
					if( distance >= radius || distance >= nearest[index] )
						continue;
					nearest[index] = distance;
					const double v = speed[k] + ( speed[d] - speed[k] ) * t;
					DirectX::XMFLOAT4& texel = result.staticWater[index];
					texel.x = static_cast<float>( pointLevel[i] + ( pointLevel[i + 1] - pointLevel[i] ) * t );
					// Строки сетки идут против Z мира
					texel.y = static_cast<float>( sx / length * v );
					texel.z = static_cast<float>( -sy / length * v );
				}
			}
			// Тексели детальных плиток в рамке отрезка
			const double xMin = std::min( a.x, b.x ) - radius;
			const double xMax = std::max( a.x, b.x ) + radius;
			const double yMin = std::min( a.y, b.y ) - radius;
			const double yMax = std::max( a.y, b.y ) + radius;
			const int tileCells = static_cast<int>( detailTileCells );
			const int tx0 = std::max( static_cast<int>( std::floor( ( xMin - detailStep ) / tileCells ) ), 0 );
			const int tx1 = std::min( static_cast<int>( std::floor( ( xMax + detailStep ) / tileCells ) ), detailTiles - 1 );
			const int tz0 = std::max( static_cast<int>( std::floor( ( n - yMax - detailStep ) / tileCells ) ), 0 );
			const int tz1 = std::min( static_cast<int>( std::floor( ( n - yMin + detailStep ) / tileCells ) ), detailTiles - 1 );
			for( int tz = tz0; tz <= tz1; ++tz )
			{
				for( int tx = tx0; tx <= tx1; ++tx )
				{
					const int slice = detailIndex[static_cast<size_t>( tz ) * detailTiles + tx];
					if( slice < 0 )
						continue;
					DetailTile& tile = result.detailTiles[slice];
					const int i0 = std::max( static_cast<int>( std::floor( ( xMin - tx * tileCells ) / detailStep + 0.5 ) ), 0 );
					const int i1 = std::min( static_cast<int>( std::ceil( ( xMax - tx * tileCells ) / detailStep + 0.5 ) ), detailSide - 1 );
					const int j0 = std::max( static_cast<int>( std::floor( ( n - yMax - tz * tileCells ) / detailStep + 0.5 ) ), 0 );
					const int j1 = std::min( static_cast<int>( std::ceil( ( n - yMin - tz * tileCells ) / detailStep + 0.5 ) ), detailSide - 1 );
					for( int j = j0; j <= j1; ++j )
					{
						for( int ii = i0; ii <= i1; ++ii )
						{
							const Point at = detailTexel( tile, ii, j );
							const double t = std::clamp( ( ( at.x - a.x ) * sx + ( at.y - a.y ) * sy ) / length2, 0.0, 1.0 );
							const double distance = std::hypot( at.x - ( a.x + t * sx ), at.y - ( a.y + t * sy ) );
							const size_t index = static_cast<size_t>( j ) * detailSide + ii;
							if( distance >= radius || distance >= detailNearest[slice][index] )
								continue;
							detailNearest[slice][index] = distance;
							const double v = speed[k] + ( speed[d] - speed[k] ) * t;
							DirectX::XMFLOAT4& texel = tile.water[index];
							texel.x = static_cast<float>( pointLevel[i] + ( pointLevel[i + 1] - pointLevel[i] ) * levelShare( k, d, t ) );
							texel.y = static_cast<float>( sx / length * v );
							texel.z = static_cast<float>( -sy / length * v );
							texel.w = static_cast<float>(
								std::max( hydraulicRadius[k] + ( hydraulicRadius[d] - hydraulicRadius[k] ) * t, 1e-3 ) );
						}
					}
				}
			}
		}

		++result.streamCount;
		result.points += points.size();
	}

	// Озеро — по своему уровню над прорезанной землёй: из клеток низины ниже уровня по соседям ниже уровня в той же низине
	// (заполнение до вреза — до перелива: за край низины вода не растекается), через русла — только где вода ручья на уровне
	// озера (подпор впадающего), не вниз по истоку
	{
		std::vector<int> stack, region;
		std::vector<uint8_t> tried( cells, 0 );
		for( size_t start = 0; start < cells; ++start )
		{
			const int label = lakeLabel[start];
			if( label < 0 || result.staticWater[start].w > -1e8f || tried[start] )
				continue;
			const double surfaceLevel = lakeSurface[label];
			tried[start] = 1;
			if( height.values[start] - carve[start] >= surfaceLevel )
				continue;
			result.staticWater[start].w = static_cast<float>( surfaceLevel );
			stack.assign( 1, static_cast<int>( start ) );
			region.assign( 1, static_cast<int>( start ) );
			while( !stack.empty() )
			{
				const int i = stack.back();
				stack.pop_back();
				for( int k = 0; k < 8; ++k )
				{
					const int r = i / n + neighbourRow[k];
					const int c = i % n + neighbourCol[k];
					if( r < 0 || c < 0 || r >= n || c >= n )
						continue;
					const size_t j = static_cast<size_t>( r ) * n + c;
					DirectX::XMFLOAT4& texel = result.staticWater[j];
					if( texel.w > -1e8f || ( lakeLabel[j] >= 0 && lakeLabel[j] != label ) || height.values[j] - carve[j] >= surfaceLevel - 0.005 ||
						dr.filled[j] < fillLevel[label] - 0.01 )
						continue;
					if( texel.x > -1e8f && texel.x < surfaceLevel - 0.01 )
						continue;
					texel.w = static_cast<float>( surfaceLevel );
					tried[j] = 1;
					stack.push_back( static_cast<int>( j ) );
					region.push_back( static_cast<int>( j ) );
				}
			}
			// Клочок меньше min_lake_area (ямка низины в русле, которую ручей перекрыл) — не озеро: там вода ручья
			if( region.size() < p.minLakeArea / ( cell * cell ) )
				for( int i : region )
					result.staticWater[i].w = -1e9f;
		}
		for( size_t i = 0; i < cells; ++i )
			lake[i] = result.staticWater[i].w > -1e8f ? 1 : 0;
	}

	// Уровень озера у текселей детальных плиток: своей клетки или соседней (урез — по детальной земле внутри клетки, а не
	// по клеткам 1 м), но не там, где вода ручья ниже озера (исток уходит вниз от порога). Где озеро не ниже ручья, тексель —
	// озеро: уровень озера, без течения и R
	for( DetailTile& tile : result.detailTiles )
	{
		for( int j = 0; j < detailSide; ++j )
		{
			for( int i = 0; i < detailSide; ++i )
			{
				const Point at = detailTexel( tile, i, j );
				const int col = std::clamp( static_cast<int>( std::floor( at.x ) ), 0, n - 1 );
				const int row = std::clamp( static_cast<int>( std::floor( at.y ) ), 0, n - 1 );
				const DirectX::XMFLOAT4& own = result.staticWater[static_cast<size_t>( row ) * n + col];
				float lakeLevel = own.w;
				if( lakeLevel <= -1e8f )
					for( int k = 0; k < 8; ++k )
					{
						const int r = row + neighbourRow[k];
						const int c = col + neighbourCol[k];
						if( r < 0 || c < 0 || r >= n || c >= n )
							continue;
						const float w = result.staticWater[static_cast<size_t>( r ) * n + c].w;
						if( w > -1e8f && !( own.x > -1e8f && own.x < w - 0.01f ) )
							lakeLevel = std::max( lakeLevel, w );
					}
				DirectX::XMFLOAT4& texel = tile.water[static_cast<size_t>( j ) * detailSide + i];
				if( lakeLevel > -1e8f && ( texel.x <= -1e8f || lakeLevel >= texel.x ) )
					texel = DirectX::XMFLOAT4( lakeLevel, 0.0f, 0.0f, 0.0f );
			}
		}
	}

	return true;
}

std::vector<TerrainEdit> TerrainHydrology::loweringEdits( const Result& result, const WaterChannelsSettings& channels, bool shaderLakeBeds )
{
	TerrainEdit lower;
	lower.name = "channels";
	lower.raise = false;
	lower.lower = true;
	lower.relative = false;
	lower.raster = "(water channels)";
	lower.rasterValues = result.lowering;
	lower.rasterSize = result.size;
	// Покраска галькой и очистка растительности — по опусканию, но только вне детальных плиток: в плитках их делают шейдеры
	// по детальному врезу (terrain.ps, scatter.cs — terrain_detail.sh), точнее клетки 1 м
	TerrainEdit paint = lower;
	paint.name = "channels paint";
	paint.lower = false;
	paint.clearFoliage = 1.0f;
	paint.paintLayer = channels.paintLayer;
	const uint32_t n = result.size;
	// Клетки озёр (static): галька по дну и трава в воде — в шейдерах по глубине в точке (урез внутри клетки, water.sh),
	// по клеткам граница озера по диагонали шла лесенкой
	if( shaderLakeBeds && result.staticWater.size() == paint.rasterValues.size() )
		for( size_t i = 0; i < paint.rasterValues.size(); ++i )
			if( result.staticWater[i].w > -1e8f )
				paint.rasterValues[i] = 0.0f;
	for( const DetailTile& tile : result.detailTiles )
	{
		for( uint32_t j = 0; j < detailTileCells; ++j )
		{
			for( uint32_t i = 0; i < detailTileCells; ++i )
			{
				const uint32_t col = tile.x * detailTileCells + i;
				const uint32_t z = tile.z * detailTileCells + j;
				if( col < n && z < n )
					paint.rasterValues[static_cast<size_t>( n - 1 - z ) * n + col] = 0.0f;
			}
		}
	}
	return { lower, paint };
}

}
