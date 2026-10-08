////////////////////////////////////////////////////////////////////////////////
// Эрозия карты высот — первая ступень конвейера рельефа и воды (TerrainErosion, docs/terrain.md, «Эрозия»):
//   mainSpawn      — пакет капель: старт по карте дождя (fbm-шум, отбраковка), направление и груз — нули;
//   mainDroplet    — шаг капли (H. T. Beyer 2015, как у S. Lague): билинейный градиент, инерция, ёмкость переноса,
//                    размыв кистью и отложение в четыре угла ячейки — целочисленными атомиками в буфер изменений
//                    (фиксированная точка, ERODE_FIXED на метр): сумма целых не зависит от порядка, результат одинаков
//                    от запуска к запуску; внутри шага капли друг друга не видят (как пакеты numpy в прежнем erosion.py);
//   mainApply      — изменения шага в высоту, буфер изменений — в ноль;
//   mainThermalOut — осыпание (Musgrave, Kolb, Mace 1989; Olsen 2004): излишки к восьми соседям сверх угла откоса и
//                    доля ухода клетки;
//   mainThermalIn  — каждая клетка собирает приход от соседей (сбор вместо раздачи — без атомиков), высота и осыпь.
// Карта капель — в метрах, продолжена за край на кисть (pad); осыпание — на карте без продолжения, за краем — крайняя
// клетка. Строки сверху вниз, как у карты высот
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"

#define ERODE_FIXED 1.0e7f		// единиц буфера изменений на метр (0,1 мкм; предел ±214 м за шаг в клетке)
#define DROPLET_GROUP 64
#define CELL_GROUP 8

// Раскладка — TerrainErosion::Parameters
cbuffer TerrainErosionBuffer : register( b4 )
{
	uint  g_size;				// клеток карты по стороне (без продолжения)
	uint  g_padded;				// клеток по стороне с продолжением за край
	uint  g_pad;
	int   g_radius;				// радиус кисти размыва, клетки
	float g_cell;				// шаг сетки, м
	float g_inertia;
	float g_capacity;
	float g_minSlope;
	float g_erodeSpeed;
	float g_depositSpeed;
	float g_evaporation;
	float g_gravity;
	float g_brushSum;			// сумма весов кисти — веса нормируются к 1
	uint  g_dropletCount;		// капель в пакете
	uint  g_batch;				// номер пакета (mainSpawn)
	uint  g_seed;
	float g_erosionPadding0;
	float g_rainScale;			// размер пятен дождя, м
	float g_rainMin;			// доля дождя в сухих местах
	float g_talusTangent;		// тангенс угла откоса
	float g_thermalRate;
	float3 g_erosionPadding;
};

struct Droplet
{
	float2 position;			// клетки карты с продолжением
	float2 direction;
	float speed;
	float water;
	float sediment;
	uint alive;
	uint key;					// пакет и номер капли — случайные числа шага
	uint step;
};

DM_UAV( RWTexture2D<float>, g_height, 0 );			// высота, м (капли — с продолжением, осыпание — без)
DM_UAV( RWTexture2D<int>, g_delta, 1 );				// изменения шага капель, ERODE_FIXED на метр
DM_UAV( RWStructuredBuffer<Droplet>, g_droplets, 2 );
DM_UAV( RWTexture2D<float2>, g_thermal, 3 );		// осыпание: x — уходит из клетки, м; y — доля излишка, уходящая к соседу
DM_UAV( RWTexture2D<float>, g_heightOut, 4 );		// осыпание: высота после шага
DM_UAV( RWTexture2D<float>, g_talus, 5 );			// осыпь: сколько грунта пришло в клетку, м

// --- Случайные числа и шум -------------------------------------------------------------------------------------------

uint pcg( uint v )
{
	const uint state = v * 747796405u + 2891336453u;
	const uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

float random01( uint a, uint b, uint c )
{
	return ( pcg( a ^ pcg( b ^ pcg( c ^ g_seed ) ) ) >> 8 ) * ( 1.0f / 16777216.0f );
}

// Градиентный шум по решётке с хешем (значения ≈ −1…1)
float gradientNoise( float2 p )
{
	const float2 cell = floor( p );
	const float2 f = p - cell;
	const float2 u = f * f * f * ( f * ( f * 6.0f - 15.0f ) + 10.0f );
	float result[4];
	[unroll] for( uint i = 0; i < 4; ++i )
	{
		const float2 corner = float2( i & 1, i >> 1 );
		const int2 c = int2( cell + corner );
		const float angle = random01( asuint( c.x ), asuint( c.y ), 0x9e3779b9u ) * 6.2831853f;
		result[i] = dot( float2( cos( angle ), sin( angle ) ), f - corner );
	}
	return lerp( lerp( result[0], result[1], u.x ), lerp( result[2], result[3], u.x ), u.y ) * 1.41421356f;
}

float fbm3( float2 p )
{
	float total = 0.0f;
	float amplitude = 1.0f;
	float norm = 0.0f;
	[unroll] for( uint i = 0; i < 3; ++i )
	{
		total += gradientNoise( p ) * amplitude;
		norm += amplitude;
		p *= 2.0f;
		amplitude *= 0.5f;
	}
	return total / norm;
}

// Дождь неравномерен: где капель больше, промоины глубже, склоны не ребристые одинаково (0…1, доля — вероятность старта)
float rain( float2 cell )
{
	const float t = saturate( ( fbm3( cell * g_cell / g_rainScale ) + 0.3f ) / 0.6f );
	return g_rainMin + ( 1.0f - g_rainMin ) * t * t * ( 3.0f - 2.0f * t );
}

// --- Капли -----------------------------------------------------------------------------------------------------------

[numthreads( DROPLET_GROUP, 1, 1 )]
void mainSpawn( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_dropletCount )
		return;
	// Клетка старта — равномерно, принимается с вероятностью дождя; восемь попыток, затем последняя
	float2 cell = 0.0f;
	[loop] for( uint attempt = 0; attempt < 8; ++attempt )
	{
		cell = float2( random01( id.x, g_batch, attempt * 4u + 1u ), random01( id.x, g_batch, attempt * 4u + 2u ) ) * g_size;
		if( random01( id.x, g_batch, attempt * 4u + 3u ) < rain( cell ) )
			break;
	}
	Droplet droplet;
	droplet.position = min( g_pad + cell, g_pad + g_size - 1e-3f );
	droplet.direction = 0.0f;
	droplet.speed = 1.0f;
	droplet.water = 1.0f;
	droplet.sediment = 0.0f;
	droplet.alive = 1u;
	droplet.key = pcg( g_batch * 0x10001u + id.x );
	droplet.step = 0u;
	g_droplets[id.x] = droplet;
}

// Высота и градиент (на тексель) билинейно
float sampleHeight( float2 p, out float2 gradient )
{
	const int2 i = int2( p );
	const float2 f = p - i;
	const float h00 = g_height[i];
	const float h10 = g_height[i + int2( 1, 0 )];
	const float h01 = g_height[i + int2( 0, 1 )];
	const float h11 = g_height[i + int2( 1, 1 )];
	gradient = float2( ( h10 - h00 ) * ( 1.0f - f.y ) + ( h11 - h01 ) * f.y, ( h01 - h00 ) * ( 1.0f - f.x ) + ( h11 - h10 ) * f.x );
	return h00 * ( 1.0f - f.x ) * ( 1.0f - f.y ) + h10 * f.x * ( 1.0f - f.y ) + h01 * ( 1.0f - f.x ) * f.y + h11 * f.x * f.y;
}

void addChange( int2 cell, float metres )
{
	InterlockedAdd( g_delta[cell], (int)round( metres * ERODE_FIXED ) );
}

[numthreads( DROPLET_GROUP, 1, 1 )]
void mainDroplet( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_dropletCount )
		return;
	Droplet droplet = g_droplets[id.x];
	if( droplet.alive == 0u )
		return;

	float2 gradient;
	const float h = sampleHeight( droplet.position, gradient );
	gradient /= g_cell;
	float2 direction = droplet.direction * g_inertia - gradient * ( 1.0f - g_inertia );
	float length = sqrt( dot( direction, direction ) );
	// На ровном месте — случайное направление
	if( length < 1e-9f )
	{
		const float angle = random01( droplet.key, droplet.step, 0x51ed27u ) * 6.2831853f;
		direction = float2( cos( angle ), sin( angle ) );
		length = 1.0f;
	}
	direction /= length;
	const float2 next = droplet.position + direction;
	// За продолжением карты капля пропадает вместе с грунтом
	const float limit = (float)g_padded - 1.0f - g_radius;
	if( next.x < g_radius || next.x >= limit || next.y < g_radius || next.y >= limit )
	{
		droplet.alive = 0u;
		g_droplets[id.x] = droplet;
		return;
	}
	float2 unused;
	const float dh = sampleHeight( next, unused ) - h;

	const float capacity = max( -dh / g_cell, g_minSlope ) * droplet.speed * droplet.water * g_capacity;
	float deposit = 0.0f;
	float erode = 0.0f;
	if( dh > 0.0f )
		deposit = min( dh, droplet.sediment );		// в гору — засыпает яму
	else if( droplet.sediment > capacity )
		deposit = ( droplet.sediment - capacity ) * g_depositSpeed;
	else
		erode = min( ( capacity - droplet.sediment ) * g_erodeSpeed, -dh );

	const int2 i = int2( droplet.position );
	const float2 f = droplet.position - i;
	if( deposit > 0.0f )
	{
		addChange( i, deposit * ( 1.0f - f.x ) * ( 1.0f - f.y ) );
		addChange( i + int2( 1, 0 ), deposit * f.x * ( 1.0f - f.y ) );
		addChange( i + int2( 0, 1 ), deposit * ( 1.0f - f.x ) * f.y );
		addChange( i + int2( 1, 1 ), deposit * f.x * f.y );
	}
	if( erode > 0.0f )
	{
		// Кисть: веса убывают к краю, в сумме 1
		[loop] for( int y = -g_radius; y <= g_radius; ++y )
		{
			[loop] for( int x = -g_radius; x <= g_radius; ++x )
			{
				const float distance = sqrt( (float)( x * x + y * y ) );
				if( x * x + y * y > g_radius * g_radius )
					continue;
				const float weight = max( g_radius - distance, 0.0f ) / g_brushSum;
				if( weight > 0.0f )
					addChange( i + int2( x, y ), -erode * weight );
			}
		}
	}
	droplet.sediment += erode - deposit;
	droplet.speed = sqrt( max( droplet.speed * droplet.speed - dh / g_cell * g_gravity, 0.0f ) );
	droplet.water *= 1.0f - g_evaporation;
	droplet.position = next;
	droplet.direction = direction;
	++droplet.step;
	g_droplets[id.x] = droplet;
}

[numthreads( CELL_GROUP, CELL_GROUP, 1 )]
void mainApply( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_padded ) )
		return;
	const int change = g_delta[id.xy];
	if( change != 0 )
	{
		g_height[id.xy] += change * ( 1.0f / ERODE_FIXED );
		g_delta[id.xy] = 0;
	}
}

// --- Осыпание --------------------------------------------------------------------------------------------------------

static const int2 neighbours[8] = { int2( -1, -1 ), int2( 0, -1 ), int2( 1, -1 ), int2( -1, 0 ), int2( 1, 0 ), int2( -1, 1 ), int2( 0, 1 ), int2( 1, 1 ) };

float heightAt( int2 cell )
{
	return g_height[clamp( cell, 0, (int)g_size - 1 )];
}

// Излишек клетки над соседом сверх угла откоса, м
float excess( float height, float neighbour, int2 offset )
{
	return max( height - neighbour - g_talusTangent * length( float2( offset ) ) * g_cell, 0.0f );
}

[numthreads( CELL_GROUP, CELL_GROUP, 1 )]
void mainThermalOut( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float h = g_height[cell];
	float total = 0.0f;
	float largest = 0.0f;
	[unroll] for( uint k = 0; k < 8; ++k )
	{
		const float e = excess( h, heightAt( cell + neighbours[k] ), neighbours[k] );
		total += e;
		largest = max( largest, e );
	}
	// Уходит доля наибольшего излишка: так склон не перескакивает через угол откоса
	const float leaving = g_thermalRate * largest;
	g_thermal[cell] = float2( leaving, total > 0.0f ? leaving / total : 0.0f );
}

[numthreads( CELL_GROUP, CELL_GROUP, 1 )]
void mainThermalIn( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float h = g_height[cell];
	// Клетка n отдаёт соседу n + k; сосед за краем — продолжение крайней клетки, и грунт, ушедший туда, возвращается в
	// ближайшую клетку карты (как в прежнем erosion.py): приход — от тех n рядом, у кого n + k, прижатое к карте, — эта клетка
	float gained = 0.0f;
	[unroll] for( int dy = -1; dy <= 1; ++dy )
	{
		[unroll] for( int dx = -1; dx <= 1; ++dx )
		{
			const int2 source = cell + int2( dx, dy );
			if( any( source < 0 ) || any( source >= (int)g_size ) )
				continue;
			const float sourceHeight = g_height[source];
			const float share = g_thermal[source].y;
			if( share <= 0.0f )
				continue;
			[unroll] for( uint k = 0; k < 8; ++k )
			{
				const int2 target = source + neighbours[k];
				if( all( clamp( target, 0, (int)g_size - 1 ) == cell ) )
					gained += excess( sourceHeight, h, neighbours[k] ) * share;
			}
		}
	}
	g_heightOut[cell] = h + gained - g_thermal[cell].x;
	g_talus[cell] += gained;
}
