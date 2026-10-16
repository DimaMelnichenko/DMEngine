////////////////////////////////////////////////////////////////////////////////
// Экранные отражения (ScreenSpaceReflections, docs/screen_reflections.md). Пирамида глубины сцены: в ячейке уровня L, 2^L
// пикселей, — ближайшая (при обратной глубине — наибольшая) и дальняя (наименьшая) глубина; уровень 0 — сама глубина сцены
// (за краем кадра — 0: дальняя плоскость), в текстуре — уровни 1… (мип L − 1). Затем луч из каждого пикселя поверхностей,
// которые просят отражения (вода: её глубина и нормаль без ряби — проход reflectionPrepass), по этой пирамиде
// (Shaders/screen_reflection_trace.sh).
//   mainHiZFirst  — уровни 1…4: группа — блок 16 × 16 пикселей глубины сцены, уровни выше 1 сводит у себя в groupshared
//                   из 2 × 2;
//   mainHiZRest   — уровни 5…7 из уровня 4 так же;
//   main          — луч; результат — uv попадания, вес и доля пути луча в глубине точки попадания
// Луч в compute, а не в пиксельном шейдере поверхности: там лучи соседних пикселей квадами 2 × 2 и по краям треугольников
// идут вразнобой — на озере во весь экран 0,25 мс против 0,15 у пары «поверхность + луч в compute»
////////////////////////////////////////////////////////////////////////////////

#include "screen_reflection_trace.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_sceneDepth, 0 );		// глубина сцены (обратная: 1 — у камеры, 0 — небо)
DM_SRV( Texture2D<float2>, g_hizSource, 1 );		// mainHiZRest: уровень 4 (вид одного мипа)
DM_SRV( Texture2D<float>, g_surfaceDepth, 3 );	// main: поверхность с отражением — обратная глубина (0 — её нет)
DM_SRV( Texture2D<float2>, g_surfaceNormal, 4 );	// и её нормаль без ряби: x, z (y — вверх)
DM_UAV( RWTexture2D<float4>, g_output, 0 );		// main: uv попадания, вес, доля пути луча в глубине точки попадания
// Уровни пирамиды, которые пишет проход: mainHiZFirst — 1…4, mainHiZRest — 5…7 в g_hizOut1…3
DM_UAV( RWTexture2D<float2>, g_hizOut1, 1 );
DM_UAV( RWTexture2D<float2>, g_hizOut2, 2 );
DM_UAV( RWTexture2D<float2>, g_hizOut3, 3 );
DM_UAV( RWTexture2D<float2>, g_hizOut4, 4 );

float2 combineDepths( float2 a, float2 b )
{
	return float2( max( a.x, b.x ), min( a.y, b.y ) );
}

// Тексель уровня index прохода — если он в уровне (у mainHiZRest группы выходят за край)
void storeLevel( uint index, uint2 texel, float2 value )
{
	uint width, height;
	switch( index )
	{
	case 1: g_hizOut1.GetDimensions( width, height ); if( all( texel < uint2( width, height ) ) ) g_hizOut1[texel] = value; break;
	case 2: g_hizOut2.GetDimensions( width, height ); if( all( texel < uint2( width, height ) ) ) g_hizOut2[texel] = value; break;
	case 3: g_hizOut3.GetDimensions( width, height ); if( all( texel < uint2( width, height ) ) ) g_hizOut3[texel] = value; break;
	default: g_hizOut4.GetDimensions( width, height ); if( all( texel < uint2( width, height ) ) ) g_hizOut4[texel] = value; break;
	}
}

groupshared float2 g_reduce[8][8];

// Значение потока — тексель уровня g_hizOut1 (группа — 8 × 8 его текселей); выше — levels − 1 уровней в g_hizOut2…
// сведением 2 × 2 в groupshared
void reduceGroup( uint2 group, uint2 local, float2 value, uint levels )
{
	storeLevel( 1, group * 8 + local, value );
	g_reduce[local.y][local.x] = value;
	GroupMemoryBarrierWithGroupSync();
	[unroll] for( uint step = 1; step < 4; ++step )
	{
		if( step >= levels )
			break;
		const uint size = 8u >> step;
		const bool active = all( local < size );
		float2 reduced = 0.0f;
		if( active )
		{
			const uint2 s = local * 2;
			reduced = combineDepths( combineDepths( g_reduce[s.y][s.x], g_reduce[s.y][s.x + 1] ),
									 combineDepths( g_reduce[s.y + 1][s.x], g_reduce[s.y + 1][s.x + 1] ) );
			storeLevel( 1 + step, group * size + local, reduced );
		}
		GroupMemoryBarrierWithGroupSync();
		if( active )
			g_reduce[local.y][local.x] = reduced;
		GroupMemoryBarrierWithGroupSync();
	}
}

// Уровни 1…4: группа — блок 16 × 16 пикселей (стороны уровня 0 кратны 128 — блоки ровно его покрывают)
[numthreads( 8, 8, 1 )]
void mainHiZFirst( uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID )
{
	uint width, height;
	g_sceneDepth.GetDimensions( width, height );
	const uint2 base = group.xy * 16 + local.xy * 2;
	float2 value = float2( 0.0f, 1.0f );
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const uint2 pixel = base + uint2( k & 1, k >> 1 );
		const float depth = all( pixel < uint2( width, height ) ) ? g_sceneDepth[pixel] : 0.0f;
		value = combineDepths( value, float2( depth, depth ) );
	}
	reduceGroup( group.xy, local.xy, value, 4 );
}

// Уровни 5…7 из уровня 4: группа — 8 × 8 текселей уровня 5
[numthreads( 8, 8, 1 )]
void mainHiZRest( uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID )
{
	uint width, height;
	g_hizSource.GetDimensions( width, height );
	const uint2 source = ( group.xy * 8 + local.xy ) * 2;
	float2 value = float2( 0.0f, 1.0f );
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const uint2 texel = source + uint2( k & 1, k >> 1 );
		if( all( texel < uint2( width, height ) ) )
			value = combineDepths( value, g_hizSource[texel] );
	}
	reduceGroup( group.xy, local.xy, value, 3 );
}

// Луч из пикселя поверхности (её глубина сцены — ресурс сцены SLOT_SCENE_DEPTH, пирамида — SLOT_SCENE_DEPTH_PYRAMID)
[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	uint width, height;
	g_reflectionSceneDepth.GetDimensions( width, height );
	if( any( id.xy >= uint2( width, height ) ) )
		return;
	const float surfaceDepth = g_surfaceDepth[id.xy];
	// Поверхности нет или её закрывает сцена (ближе — больше обратная глубина)
	if( surfaceDepth <= 0.0f || g_reflectionSceneDepth[id.xy] > surfaceDepth )
	{
		g_output[id.xy] = 0.0f;
		return;
	}
	// Точка поверхности по глубине (вид → мир) и нормаль без ряби
	const float2 ndc = float2( ( id.x + 0.5f ) / width * 2.0f - 1.0f, 1.0f - ( id.y + 0.5f ) / height * 2.0f );
	const float viewZ = reflectionViewDepth( surfaceDepth );
	const float3 viewPosition = float3( ndc.x * viewZ / cb_projectionMatrix[0][0], ndc.y * viewZ / cb_projectionMatrix[1][1], viewZ );
	const float3 position = mul( float4( viewPosition, 1.0f ), cb_viewInverseMatrix ).xyz;
	const float2 normalXZ = g_surfaceNormal[id.xy];
	const float3 normal = float3( normalXZ.x, sqrt( saturate( 1.0f - dot( normalXZ, normalXZ ) ) ), normalXZ.y );
	g_output[id.xy] = traceScreenReflection( position, normal );
}
