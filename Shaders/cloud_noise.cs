////////////////////////////////////////////////////////////////////////////////
// Шумы облаков — один раз при загрузке (VolumetricCloud). Все повторяются без шва (решётка по модулю периода):
// - mainShape — 128³: R — шум Перлина — Уорли (Перлин, поднятый к ячейкам Уорли: клубы), GBA — шум Уорли трёх частот;
// - mainDetail — 32³: шум Уорли трёх частот для краёв;
// - mainWeather — 512²: R — покрытие (Перлин), G — тип облака (Перлин крупнее)
// (A. Schneider, SIGGRAPH 2015; так же генератор шумов S. Hillaire для облаков UE)
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"

DM_UAV( RWTexture3D<float4>, g_shapeOut, 0 );
DM_UAV( RWTexture3D<float4>, g_detailOut, 0 );
DM_UAV( RWTexture2D<float2>, g_weatherOut, 0 );

uint noiseHash( uint3 p )
{
	uint n = p.x * 0x8da6b343u ^ p.y * 0xd8163841u ^ p.z * 0xcb1ab31fu;
	n ^= n >> 16;
	n *= 0x7feb352du;
	n ^= n >> 15;
	n *= 0x846ca68bu;
	n ^= n >> 16;
	return n;
}

float3 noiseRandom3( uint3 cell )
{
	const uint h = noiseHash( cell );
	return float3( h & 0x3ffu, ( h >> 10 ) & 0x3ffu, ( h >> 20 ) & 0x3ffu ) / 1023.0f;
}

uint3 wrapCell( int3 cell, int period )
{
	return uint3( ( cell % period + period ) % period );
}

// Шум Перлина с периодом period ячеек, −1…1
float perlin( float3 p, int period )
{
	const int3 cell = int3( floor( p ) );
	const float3 f = p - cell;
	const float3 u = f * f * f * ( f * ( f * 6.0f - 15.0f ) + 10.0f );
	float corners[8];
	[unroll] for( int i = 0; i < 8; ++i )
	{
		const int3 corner = int3( i & 1, ( i >> 1 ) & 1, ( i >> 2 ) & 1 );
		const float3 gradient = normalize( noiseRandom3( wrapCell( cell + corner, period ) ) * 2.0f - 1.0f + 1e-4f );
		corners[i] = dot( gradient, f - corner );
	}
	const float x0 = lerp( corners[0], corners[1], u.x );
	const float x1 = lerp( corners[2], corners[3], u.x );
	const float x2 = lerp( corners[4], corners[5], u.x );
	const float x3 = lerp( corners[6], corners[7], u.x );
	return lerp( lerp( x0, x1, u.y ), lerp( x2, x3, u.y ), u.z ) * 1.15f;
}

// Шум Уорли с периодом period ячеек, перевёрнутый: 1 — у точки ячейки, 0 — далеко
float worley( float3 p, int period )
{
	const int3 cell = int3( floor( p ) );
	const float3 f = p - cell;
	float nearest = 1.0f;
	[unroll] for( int z = -1; z <= 1; ++z )
		[unroll] for( int y = -1; y <= 1; ++y )
			[unroll] for( int x = -1; x <= 1; ++x )
			{
				const int3 offset = int3( x, y, z );
				const float3 feature = offset + noiseRandom3( wrapCell( cell + offset, period ) );
				nearest = min( nearest, length( feature - f ) );
			}
	return 1.0f - saturate( nearest );
}

// Три октавы Уорли с частотой frequency на текстуру
float worleyFbm( float3 uvw, int frequency )
{
	return worley( uvw * frequency, frequency ) * 0.625f + worley( uvw * frequency * 2, frequency * 2 ) * 0.25f +
		   worley( uvw * frequency * 4, frequency * 4 ) * 0.125f;
}

float perlinFbm( float3 uvw, int frequency, int octaves )
{
	float sum = 0.0f;
	float amplitude = 1.0f;
	float total = 0.0f;
	for( int i = 0; i < octaves; ++i )
	{
		sum += perlin( uvw * frequency, frequency ) * amplitude;
		total += amplitude;
		amplitude *= 0.5f;
		frequency *= 2;
	}
	return sum / total;
}

[numthreads( 4, 4, 4 )]
void mainShape( uint3 id : SV_DispatchThreadID )
{
	const float3 uvw = ( id + 0.5f ) / 128.0f;
	const float perlinNoise = saturate( perlinFbm( uvw, 4, 4 ) * 0.5f + 0.5f );
	const float worleyLow = worleyFbm( uvw, 4 );
	// Перлин — Уорли: Перлин, поднятый над ячейками Уорли, — округлые клубы с рваной серединой
	const float perlinWorley = saturate( worleyLow + perlinNoise * ( 1.0f - worleyLow ) );
	g_shapeOut[id] = float4( perlinWorley, worleyLow, worleyFbm( uvw, 8 ), worleyFbm( uvw, 16 ) );
}

[numthreads( 4, 4, 4 )]
void mainDetail( uint3 id : SV_DispatchThreadID )
{
	const float3 uvw = ( id + 0.5f ) / 32.0f;
	g_detailOut[id] = float4( worleyFbm( uvw, 2 ), worleyFbm( uvw, 4 ), worleyFbm( uvw, 8 ), 1.0f );
}

[numthreads( 8, 8, 1 )]
void mainWeather( uint3 id : SV_DispatchThreadID )
{
	const float3 uv = float3( ( id.xy + 0.5f ) / 512.0f, 0.5f );
	const float coverage = saturate( perlinFbm( uv, 4, 5 ) * 0.9f + 0.5f );
	const float type = saturate( perlinFbm( uv + 7.31f, 2, 3 ) * 1.2f + 0.5f );
	g_weatherOut[id.xy] = float2( coverage, type );
}
