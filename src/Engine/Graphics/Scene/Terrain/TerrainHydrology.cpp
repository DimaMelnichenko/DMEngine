#include "TerrainHydrology.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>

namespace GS
{

namespace
{

constexpr double fillEpsilon = 1e-4;	// уклон заполненных низин на шаг, м: из каждой клетки есть путь вниз к краю
constexpr double pi = 3.14159265358979323846;
// Восемь соседей (строка, столбец) и расстояние в шагах — порядок как в Tools/erosion.py (при равном уклоне берётся первый)
constexpr int neighbourRow[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
constexpr int neighbourCol[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
const double neighbourDistance[8] = { std::sqrt( 2.0 ), 1.0, std::sqrt( 2.0 ), 1.0, 1.0, std::sqrt( 2.0 ), 1.0, std::sqrt( 2.0 ) };

struct Point
{
	double x = 0.0;	// столбец (дробный, центр клетки — +0,5)
	double y = 0.0;	// строка
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

// Priority-Flood с уклоном (Barnes, Lehman, Mulla 2014): обход от края по возрастанию, при равной высоте — по индексу
std::vector<double> fillDepressions( const Grid& height )
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
	while( !heap.empty() )
	{
		const auto [z, i] = heap.top();
		heap.pop();
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
			const double value = height.values[j] > z + fillEpsilon ? height.values[j] : z + fillEpsilon;
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
	return level;
}

}

bool TerrainHydrology::build( const HeightField& field, const WaterSimulationSettings& water, Result& result )
{
	const WaterChannelsSettings& p = water.channels;
	const int n = static_cast<int>( field.size );
	if( n < 3 )
		return false;
	const size_t cells = static_cast<size_t>( n ) * n;
	const double cell = field.texelSize;

	Grid height;
	height.size = n;
	height.values.resize( cells );
	for( int row = 0; row < n; ++row )
		for( int col = 0; col < n; ++col )
			height.values[static_cast<size_t>( row ) * n + col] =
				static_cast<double>( field.heights[row * field.rowPitch + col] ) * field.heightMultiplier + field.heightOffset;

	// 1. Сток по заполненной карте и водосбор
	const std::vector<double> filled = fillDepressions( height );
	const std::vector<int> receiver = receivers( filled, n );
	const std::vector<int> order = downhillOrder( filled );
	const std::vector<double> area = accumulate( std::vector<double>( cells, cell * cell ), receiver, order );

	// 2. Приток: доля «русла» по водосбору и помощники, весь расход помощника — в клетку центра; расход вниз по течению
	std::vector<double> inflow( cells );
	const double logStart = std::log10( std::max( water.flowStart, 1.0f ) );
	const double logFull = std::log10( std::max( water.flowFull, 1.0f ) );
	for( size_t i = 0; i < cells; ++i )
		inflow[i] = smoothstep( logStart, logFull, std::log10( std::max( area[i], 1.0 ) ) ) * ( water.sourceRate / 1000.0 );
	for( const WaterSource& source : water.sources )
	{
		const int col = static_cast<int>( std::clamp( source.position.x / cell, 0.0, n - 1.0 ) );
		const int row = static_cast<int>( std::clamp( ( n * cell - source.position.y ) / cell, 0.0, n - 1.0 ) );
		inflow[static_cast<size_t>( row ) * n + col] += source.rate / 1000.0;
	}
	const std::vector<double> flow = accumulate( inflow, receiver, order );
	result.largestDischarge = static_cast<float>( *std::max_element( flow.begin(), flow.end() ) );

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
	result.nodes = stream.size();
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
	// Узлы от истоков вниз — по убыванию заполненной высоты (при равных — по номеру)
	std::vector<int> nodeOrder( count );
	std::iota( nodeOrder.begin(), nodeOrder.end(), 0 );
	std::stable_sort( nodeOrder.begin(), nodeOrder.end(), [&]( int a, int b ) { return filled[stream[a]] > filled[stream[b]]; } );

	// Извилины: сдвиг поперёк течения волной вдоль пути от истока, амплитуда растёт с расходом и гаснет на уклоне
	// (ось, сдвинутая с ложбины на склон, легла бы бортом под уровень воды) и у истока и устья
	{
		std::vector<double> distance( count, 0.0 );
		for( int k : nodeOrder )
		{
			const int d = down[k];
			if( d >= 0 && up[d] == k )
				distance[d] = distance[k] + std::hypot( position[d].x - position[k].x, position[d].y - position[k].y );
		}
		Grid smooth = height;
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
			const double edge = down[k] < 0 ? 0.0 : std::min( distance[k] / p.meanderLength, 1.0 );
			const double offset = wave * amplitude * edge;
			shifted[k].x = position[k].x - ty * offset;
			shifted[k].y = position[k].y + tx * offset;
		}
		position = std::move( shifted );
	}

	// 4. Ложе: дно монотонно вниз по течению, профиль по отрезкам оси — дно, тальвег, борта
	std::vector<double> half( count ), depth( count ), bed( count ), bank( count ), surface( count );
	for( int k = 0; k < count; ++k )
	{
		half[k] = 0.5 * std::max( p.widthCoef * std::sqrt( q[k] ), static_cast<double>( p.minWidth ) ) / cell;
		depth[k] = std::max( p.depthCoef * std::pow( q[k], 0.4 ), static_cast<double>( p.minIncision ) );
		surface[k] = height.sample( position[k].x - 0.5, position[k].y - 0.5 );
		bed[k] = surface[k] - depth[k];
	}
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d >= 0 )
		{
			const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
			bed[d] = std::min( bed[d], bed[k] - p.minSlope * step );
		}
	}
	for( int k = 0; k < count; ++k )
	{
		depth[k] = surface[k] - bed[k];
		bank[k] = depth[k] * p.bankSlope / cell;
	}
	std::vector<double> carve( cells, 0.0 );
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k];
		if( d < 0 )
			continue;
		const Point a = position[k];
		const Point b = position[d];
		const double reach = std::max( half[k] + bank[k], half[d] + bank[d] );
		const int c0 = static_cast<int>( std::max( std::floor( std::min( a.x, b.x ) - reach ), 0.0 ) );
		const int c1 = static_cast<int>( std::min( std::ceil( std::max( a.x, b.x ) + reach ), n - 1.0 ) );
		const int r0 = static_cast<int>( std::max( std::floor( std::min( a.y, b.y ) - reach ), 0.0 ) );
		const int r1 = static_cast<int>( std::min( std::ceil( std::max( a.y, b.y ) + reach ), n - 1.0 ) );
		const double sx = b.x - a.x;
		const double sy = b.y - a.y;
		const double length2 = std::max( sx * sx + sy * sy, 1e-9 );
		for( int row = r0; row <= r1; ++row )
		{
			for( int col = c0; col <= c1; ++col )
			{
				const double cx = col + 0.5;
				const double cy = row + 0.5;
				const double t = std::clamp( ( ( cx - a.x ) * sx + ( cy - a.y ) * sy ) / length2, 0.0, 1.0 );
				const double distance = std::hypot( cx - ( a.x + t * sx ), cy - ( a.y + t * sy ) );
				const double w = half[k] + ( half[d] - half[k] ) * t;
				const double dep = depth[k] + ( depth[d] - depth[k] ) * t;
				const double bw = std::max( bank[k] + ( bank[d] - bank[k] ) * t, 1e-3 );
				// Ложе U: к середине дно глубже (тальвег) — малый расход собирается в середине, а не плёнкой по ширине
				const double inner = std::clamp( distance / std::max( w, 1e-3 ), 0.0, 1.0 );
				const double thalweg = p.thalweg * dep * ( 1.0 - inner * inner );
				const double profile = dep * ( 1.0 - smoothstep( 0.0, 1.0, ( distance - w ) / bw ) ) + thalweg;
				double& value = carve[static_cast<size_t>( row ) * n + col];
				value = std::max( value, profile );
			}
		}
	}
	// Низины, которые заполнит вода (озёра), не режутся: русло кончается у берега. Низина меньше minLakeArea — не озеро, а
	// ямка на дне ручья (извилина, тальвег, кривая правки рельефа): ручей идёт сквозь неё, иначе лента обрывалась бы у
	// каждой ямки, а в ямке стояла бы своя плоская вода
	const std::vector<double> lakeLevel = lakeLevels( filled, height.values, inflow, n, p.lakeDepth, p.minLakeArea / ( cell * cell ) );
	std::vector<uint8_t> lake( cells );
	for( size_t i = 0; i < cells; ++i )
	{
		lake[i] = std::isnan( lakeLevel[i] ) ? 0 : 1;
		if( lake[i] )
			carve[i] = 0.0;
	}
	result.size = static_cast<uint32_t>( n );
	result.lowering.resize( cells );
	result.carvedCells = 0;
	result.deepestLowering = 0.0f;
	Grid carved = height;
	for( size_t i = 0; i < cells; ++i )
	{
		result.lowering[i] = static_cast<float>( -carve[i] );
		carved.values[i] -= carve[i];
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

	// Водосбор вдоль оси русла — приток симуляции (режим simulated): площадь узла переносится на сдвинутую ось шагами по
	// полметра, иначе приток лился бы вдоль прежних прямых линий D8 рядом с руслом
	result.channelFlow.assign( area.begin(), area.end() );
	for( int k = 0; k < count; ++k )
		result.channelFlow[stream[k]] = 0.0f;
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
			value = std::max( value, static_cast<float>( area[stream[k]] ) );
		}
	}

	// 5. Вода ручьёв: уклон дна, глубина по Маннингу для прямоугольного русла h = (Q·n / (w·√S))^(3/5), дно — по
	// прорезанному рельефу у оси (наименьшее в клетке вокруг точки), уровень вниз по течению не растёт
	std::vector<double> slope( count, p.minWaterSlope ), level( count ), speed( count ), foam( count ), actual( count );
	for( int k = 0; k < count; ++k )
	{
		const int d = down[k];
		if( d >= 0 )
		{
			const double step = std::max( std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell, 1e-3 );
			slope[k] = std::max( ( bed[k] - bed[d] ) / step, static_cast<double>( p.minWaterSlope ) );
		}
		const double waterDepth = std::pow( q[k] * p.manning / ( width[k] * std::sqrt( slope[k] ) ), 0.6 );
		actual[k] = std::numeric_limits<double>::infinity();
		for( double dy : { -0.7, 0.0, 0.7 } )
			for( double dx : { -0.7, 0.0, 0.7 } )
				actual[k] = std::min( actual[k], carved.sample( position[k].x - 0.5 + dx, position[k].y - 0.5 + dy ) );
		level[k] = actual[k] + std::max( waterDepth, static_cast<double>( p.minWaterDepth ) );
	}
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d >= 0 )
			level[d] = std::min( level[d], level[k] );
	}
	for( int k = 0; k < count; ++k )
	{
		speed[k] = std::max( q[k] / ( width[k] * std::max( level[k] - actual[k], 1e-3 ) ), static_cast<double>( p.minSpeed ) );
		foam[k] = std::clamp( ( slope[k] - p.foamSlope ) / ( 2.0 * p.foamSlope ), 0.0, 1.0 ) * 0.6;
	}

	// Ручьи — цепочки узлов от истока (или первого узла ниже озера) до слияния, озера или края; дальше слияния течёт
	// главный ручей
	std::vector<std::vector<int>> chains;
	for( int k : nodeOrder )
	{
		const bool inLake = lake[stream[k]] != 0;
		if( inLake || ( up[k] >= 0 && !lake[stream[up[k]]] ) )
			continue;
		std::vector<int> chain = { k };
		while( true )
		{
			const int d = down[chain.back()];
			if( d < 0 )
				break;
			chain.push_back( d );
			if( up[d] != chain[chain.size() - 2] || lake[stream[d]] )
				break;
		}
		if( chain.size() >= 2 )
			chains.push_back( std::move( chain ) );
	}

	// Растр статичной воды: у клетки — ближайший отрезок ручья в пределах его ленты
	result.staticWater.assign( cells, DirectX::XMFLOAT4( -1e9f, 0.0f, 0.0f, 0.0f ) );
	for( size_t i = 0; i < cells; ++i )
		result.staticWater[i].w = lake[i] ? static_cast<float>( lakeLevel[i] ) : -1e9f;
	std::vector<double> nearest( cells, std::numeric_limits<double>::infinity() );
	const double world = n * cell;
	result.streams.clear();
	result.points = 0;
	for( const std::vector<int>& chain : chains )
	{
		// Точки ручья и узлы, чьи уровень, течение и ширина у точки. Подтекающий в главный кончается на урезе главного (где
		// его вода встречает борт), а не на оси: иначе последний отрезок притока лёг бы поверх ленты главного, и две
		// полупрозрачные воды сложились бы в светлое пятно; и не на внешнем краю ленты главного — там под ней сухой борт, и
		// хвост притока лёг бы плёнкой на сухую гальку. Точки внутри уреза отбрасываются, конец — на пересечении с ним
		std::vector<int> nodes = chain;
		std::vector<Point> points( chain.size() );
		for( size_t i = 0; i < chain.size(); ++i )
			points[i] = position[chain[i]];
		const int last = chain.back();
		const int previous = chain[chain.size() - 2];
		if( up[last] != previous && !lake[stream[last]] )
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
		// Уровень ленты у точек. Ручей, впадающий в озеро, кончается на берегу (последний узел — уже в озере: там вода —
		// поверхность озера) и не ниже уровня озера: иначе хвост ленты спускался бы по ложу круче берега плёнкой поверх
		// сухого борта и уходил под поверхность озера
		std::vector<double> pointLevel( points.size() );
		for( size_t i = 0; i < points.size(); ++i )
			pointLevel[i] = level[nodes[i]];
		if( lake[stream[nodes.back()]] )
		{
			const double lakeSurface = lakeLevel[stream[nodes.back()]];
			points.pop_back();
			nodes.pop_back();
			pointLevel.pop_back();
			for( double& value : pointLevel )
				value = std::max( value, lakeSurface );
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
		}

		WaterStream& water = result.streams.emplace_back();
		for( size_t i = 0; i < points.size(); ++i )
		{
			const int k = nodes[i];
			WaterStreamPoint& point = water.points.emplace_back();
			point.position = DirectX::XMFLOAT3( static_cast<float>( points[i].x * cell ), static_cast<float>( pointLevel[i] ),
												static_cast<float>( world - points[i].y * cell ) );
			point.halfWidth = static_cast<float>( reach[k] + p.ribbonOverlap );
			point.speed = static_cast<float>( speed[k] );
			point.foam = static_cast<float>( foam[k] );
		}
		result.points += points.size();
	}

	// Под лентой ручья озёрной поверхности нет: у берега лента и плоскость озера легли бы друг на друга двумя
	// полупрозрачными слоями. Клетки озера в полосе ленты (по отрезку, без скруглений на концах) — без уровня озера, их вода
	// — вода ленты (mainStatic берёт наибольшую глубину)
	for( const WaterStream& water : result.streams )
	{
		for( size_t i = 0; i + 1 < water.points.size(); ++i )
		{
			const WaterStreamPoint& pa = water.points[i];
			const WaterStreamPoint& pb = water.points[i + 1];
			const Point a = { pa.position.x / cell, ( world - pa.position.z ) / cell };
			const Point b = { pb.position.x / cell, ( world - pb.position.z ) / cell };
			const double radius = std::max( pa.halfWidth, pb.halfWidth ) / cell;
			const int c0 = static_cast<int>( std::max( std::floor( std::min( a.x, b.x ) - radius ), 0.0 ) );
			const int c1 = static_cast<int>( std::min( std::ceil( std::max( a.x, b.x ) + radius ), n - 1.0 ) );
			const int r0 = static_cast<int>( std::max( std::floor( std::min( a.y, b.y ) - radius ), 0.0 ) );
			const int r1 = static_cast<int>( std::min( std::ceil( std::max( a.y, b.y ) + radius ), n - 1.0 ) );
			const double sx = b.x - a.x;
			const double sy = b.y - a.y;
			const double length2 = std::max( sx * sx + sy * sy, 1e-9 );
			for( int row = r0; row <= r1; ++row )
			{
				for( int col = c0; col <= c1; ++col )
				{
					const size_t index = static_cast<size_t>( row ) * n + col;
					if( !lake[index] )
						continue;
					const double cx = col + 0.5;
					const double cy = row + 0.5;
					const double t = ( ( cx - a.x ) * sx + ( cy - a.y ) * sy ) / length2;
					if( t < 0.0 || t > 1.0 )
						continue;
					const double w = ( pa.halfWidth + ( pb.halfWidth - pa.halfWidth ) * t ) / cell;
					if( std::hypot( cx - ( a.x + t * sx ), cy - ( a.y + t * sy ) ) <= w )
						result.staticWater[index].w = -1e9f;
				}
			}
		}
	}
	return true;
}

TerrainEdit TerrainHydrology::loweringEdit( const Result& result, const WaterChannelsSettings& channels )
{
	TerrainEdit edit;
	edit.name = "channels";
	edit.raise = false;
	edit.lower = true;
	edit.relative = false;
	edit.clearFoliage = 1.0f;
	edit.paintLayer = channels.paintLayer;
	edit.raster = "(water channels)";
	edit.rasterValues = result.lowering;
	edit.rasterSize = result.size;
	return edit;
}

}
