#include "TerrainHydrology.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <cstdio>
#include <queue>
#include <string>
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
		const double depth = raw * h + limit * ( 1.0 - h ) - k * h * ( 1.0 - h );
		lowering[i] = std::max( height[i] - ( level[i] - depth ), 0.0 );
	}
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

	// 1. Сток по заполненной карте и водосбор
	d.filled = fillDepressions( d.height );
	d.receiver = receivers( d.filled, n );
	d.order = downhillOrder( d.filled );
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

// Отпечаток (FNV-1a 64)
struct Fingerprint
{
	uint64_t value = 1469598103934665603ull;
	void add( const void* data, size_t size )
	{
		const uint8_t* bytes = static_cast<const uint8_t*>( data );
		for( size_t i = 0; i < size; ++i )
			value = ( value ^ bytes[i] ) * 1099511628211ull;
	}
	template<typename T> void add( const T& v ) { add( &v, sizeof( v ) ); }
};

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
	Fingerprint key;
	const char version[] = "streams-v1";
	key.add( version, sizeof( version ) );
	key.add( field.size );
	for( uint32_t row = 0; row < field.size; ++row )
		key.add( field.heights + row * field.rowPitch, field.size * sizeof( float ) );
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

	// 3. Узлы оси: клетки с расходом выше порога, вниз — получатель, вверх — главный приток (наибольший расход)
	std::vector<int> stream;
	std::vector<int> nodeOf( cells, -1 );
	for( size_t i = 0; i < cells; ++i )
	{
		if( dr.flow[i] > p.minDischarge )
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
		down[k] = dr.receiver[i] != i ? nodeOf[dr.receiver[i]] : -1;
		q[k] = dr.flow[i];
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
	std::stable_sort( nodeOrder.begin(), nodeOrder.end(), [&]( int a, int b ) { return dr.filled[stream[a]] > dr.filled[stream[b]]; } );

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
	const std::vector<uint8_t>& lake = dr.lake;
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
	for( size_t a = 0; a < lines.size(); ++a )
	{
		const Point end = lines[a].points.back();
		if( lake[cellIndex( end, n )] )
			continue;
		for( size_t b = 0; b < lines.size() && lines[a].target < 0; ++b )
		{
			if( a == b )
				continue;
			for( size_t i = 0; i < lines[b].points.size(); ++i )
			{
				if( std::hypot( lines[b].points[i].x - end.x, lines[b].points[i].y - end.y ) < 0.25 )
				{
					lines[a].target = static_cast<int>( b );
					lines[a].targetPoint = i;
					break;
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
		double widthCoef, minWidth, depthCoef, minIncision, freeboard, bankSlope, thalweg, manning;
	};
	std::vector<Shape> shape( count );
	for( int k = 0; k < count; ++k )
	{
		const StreamCurve& curve = *lines[curveOf[k]].curve;
		shape[k] = { p.widthCoef * curve.widthScale, p.minWidth * curve.widthScale, p.depthCoef * curve.depthScale,
					 p.minIncision * curve.depthScale, p.freeboard * curve.freeboardScale, p.bankSlope * curve.bankSlopeScale,
					 p.thalweg * curve.thalwegScale, p.manning * curve.roughnessScale };
	}

	// Узлы ниже озера: путь от берега, м, и уровень озера, из которого течёт ручей (NaN — не из озера). Кривая, которая
	// начинается у берега, — из озера
	std::vector<double> outletDistance( count, std::numeric_limits<double>::infinity() );
	std::vector<double> outletLevel( count, std::numeric_limits<double>::quiet_NaN() );
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
					outletLevel[k] = lakeLevel[static_cast<size_t>( r ) * n + c];
					lakeStart[k] = 1;
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
		if( !fromLake && std::isnan( outletLevel[k] ) )
			continue;
		const double step = std::hypot( position[d].x - position[k].x, position[d].y - position[k].y ) * cell;
		const double distance = ( fromLake ? 0.0 : outletDistance[k] ) + step;
		if( distance < outletDistance[d] )
		{
			outletDistance[d] = distance;
			outletLevel[d] = fromLake ? lakeLevel[cellOf[k]] : outletLevel[k];
		}
	}

	// 4. Ложе: дно монотонно вниз по течению, профиль по отрезкам оси — дно, тальвег, борта. В озере ложа нет (дно озера),
	// и оно не тянет дно ниже по течению; ниже озера врез нарастает от порога (outletSlope)
	std::vector<double> half( count ), depth( count ), bed( count ), bank( count ), surface( count );
	for( int k = 0; k < count; ++k )
	{
		const Shape& s = shape[k];
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
											   static_cast<double>( p.minWaterDepth ) );
		depth[k] = std::max( { s.depthCoef * std::pow( q[k], 0.4 ), s.minIncision, ( waterEstimate + s.freeboard ) / ( 1.0 + s.thalweg ) } );
		if( inLake( k ) )
			depth[k] = 0.0;
		else if( !std::isnan( outletLevel[k] ) )
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
				const double thalwegShare = shape[k].thalweg + ( shape[d].thalweg - shape[k].thalweg ) * t;
				// Ложе U: к середине дно глубже (тальвег) — малый расход собирается в середине, а не плёнкой по ширине
				const double inner = std::clamp( distance / std::max( w, 1e-3 ), 0.0, 1.0 );
				const double thalweg = thalwegShare * dep * ( 1.0 - inner * inner );
				const double profile = dep * ( 1.0 - smoothstep( 0.0, 1.0, ( distance - w ) / bw ) ) + thalweg;
				double& value = carve[static_cast<size_t>( row ) * n + col];
				value = std::max( value, profile );
			}
		}
	}
	// В озёрах русло не режется, дно — чаша (lakeBasins)
	const std::vector<double> basin = lakeBasins( lakeLevel, height.values, n, cell, p );
	for( size_t i = 0; i < cells; ++i )
		if( lake[i] )
			carve[i] = basin[i];
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
		const double waterDepth = std::pow( q[k] * shape[k].manning / ( width[k] * std::sqrt( slope[k] ) ), 0.6 );
		actual[k] = std::numeric_limits<double>::infinity();
		for( double dy : { -0.7, 0.0, 0.7 } )
			for( double dx : { -0.7, 0.0, 0.7 } )
				actual[k] = std::min( actual[k], carved.sample( position[k].x - 0.5 + dx, position[k].y - 0.5 + dy ) );
		level[k] = actual[k] + std::max( waterDepth, static_cast<double>( p.minWaterDepth ) );
		// Вода ниже бровки на запас, но не мельче 5 см над дном
		const double freeboard = std::isnan( outletLevel[k] ) ? shape[k].freeboard : std::min( shape[k].freeboard, outletDistance[k] * outletSlope );
		level[k] = std::max( std::min( level[k], surface[k] - freeboard ), actual[k] + 0.05 );
		// Ручей из озера — не выше его уровня, а первый узел ниже берега — на уровне озера: вода выходит из озера ровно
		if( !std::isnan( outletLevel[k] ) )
		{
			const bool first = lakeStart[k] || ( up[k] >= 0 && inLake( up[k] ) );
			level[k] = first ? std::max( outletLevel[k], actual[k] ) : std::min( level[k], outletLevel[k] );
		}
	}
	for( int k : nodeOrder )
	{
		const int d = down[k];
		if( d >= 0 && !inLake( k ) )
			level[d] = std::min( level[d], level[k] );
	}
	for( int k = 0; k < count; ++k )
	{
		speed[k] = std::max( q[k] / ( width[k] * std::max( level[k] - actual[k], 1e-3 ) ), static_cast<double>( p.minSpeed ) );
		foam[k] = std::clamp( ( slope[k] - p.foamSlope ) / ( 2.0 * p.foamSlope ), 0.0, 1.0 ) * 0.6;
	}

	// Правленная или ручная кривая, которая на нынешнем рельефе идёт в гору (земля под ней сменилась) — предупреждение:
	// дно монотонно вниз, и русло там прорежется траншеей
	for( size_t a = 0; a < lines.size(); ++a )
	{
		const StreamCurve& curve = *lines[a].curve;
		if( ( curve.generated && !curve.edited ) || lineNodes[a].empty() )
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
		if( rise > 0.5 )
		{
			char text[256];
			std::snprintf( text, sizeof( text ), "stream \"%s\" runs uphill by %.1f m near %.0f, %.0f: the terrain under the edited curve changed?",
						   curve.name.c_str(), rise, position[at].x * cell, world - position[at].y * cell );
			result.warnings.push_back( text );
		}
	}

	// Ленты — по кривым: от истока (или узла ниже озера) до слияния, озера или края; кривая, проходящая озеро, рвётся в нём
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

	// Растр статичной воды: у клетки — ближайший отрезок ручья в пределах его ленты
	result.staticWater.assign( cells, DirectX::XMFLOAT4( -1e9f, 0.0f, 0.0f, 0.0f ) );
	for( size_t i = 0; i < cells; ++i )
		result.staticWater[i].w = lake[i] ? static_cast<float>( lakeLevel[i] ) : -1e9f;
	std::vector<double> nearest( cells, std::numeric_limits<double>::infinity() );
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
		// Уровень ленты у точек. Ручей, впадающий в озеро, кончается на берегу (последний узел — уже в озере: там вода —
		// поверхность озера) и не ниже уровня озера: иначе хвост ленты спускался бы по ложу круче берега плёнкой поверх
		// сухого борта и уходил под поверхность озера
		std::vector<double> pointLevel( points.size() );
		for( size_t i = 0; i < points.size(); ++i )
			pointLevel[i] = level[nodes[i]];
		if( lake[cellOf[nodes.back()]] )
		{
			const double lakeSurface = lakeLevel[cellOf[nodes.back()]];
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
