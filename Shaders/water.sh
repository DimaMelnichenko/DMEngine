////////////////////////////////////////////////////////////////////////////////
// Вода симуляции для шейдеров (WaterSimulation, docs/water.md): текстура на сетке карты высот террейна — те же UV,
// что у карты высот (terrainUV в terrain_height.sh, input.uv террейна). Без симуляции у уровня слот пуст — нули
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SH
#define WATER_SH

#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float4>, g_waterState, SLOT_WATER );	// глубина, м; скорость по X и Z мира, м/с; глубина с памятью, м
DM_SRV( Texture2D<float>, g_waterSources, SLOT_WATER_SOURCES );	// приток источников, м/с слоя воды
DM_SRV( Texture2D<float>, g_waterLevel, SLOT_WATER_LEVEL );	// уровень поверхности, м; −1e30 — нет воды

struct WaterSample
{
	float depth;
	float2 velocity;
	float inflow;		// приток источников в ячейку, мм/с слоя воды (по водосбору и помощники)
	float remembered;	// наибольшая глубина за последние ~30 с, м: где вода была недавно
};

WaterSample sampleWater( float2 uv )
{
	const float4 state = g_waterState.SampleLevel( g_SamplerLinearClamp, uv, 0.0f );
	WaterSample water;
	water.depth = state.x;
	water.velocity = state.yz;
	water.inflow = g_waterSources.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ) * 1000.0f;
	water.remembered = state.w;
	return water;
}

// Глубина, сглаженная по соседям в полутора ячейках (край без ступенек сетки), × 2: у самого уреза, где вода только
// с одной стороны, — как в воде
float smoothedWaterDepth( float2 uv )
{
	uint width, height;
	g_waterState.GetDimensions( width, height );
	const float2 texel = 1.5f / float2( max( width, 1u ), max( height, 1u ) );
	float depth = 2.0f * g_waterState.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).x;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv + float2( texel.x, 0.0f ), 0.0f ).x;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv - float2( texel.x, 0.0f ), 0.0f ).x;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv + float2( 0.0f, texel.y ), 0.0f ).x;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv - float2( 0.0f, texel.y ), 0.0f ).x;
	return depth / 3.0f;
}

// Глубина с памятью (вода ушла — гаснет за ~30 с), сглаженная, как smoothedWaterDepth
float smoothedRememberedDepth( float2 uv )
{
	uint width, height;
	g_waterState.GetDimensions( width, height );
	const float2 texel = 1.5f / float2( max( width, 1u ), max( height, 1u ) );
	float depth = 2.0f * g_waterState.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).w;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv + float2( texel.x, 0.0f ), 0.0f ).w;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv - float2( texel.x, 0.0f ), 0.0f ).w;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv + float2( 0.0f, texel.y ), 0.0f ).w;
	depth += g_waterState.SampleLevel( g_SamplerLinearClamp, uv - float2( 0.0f, texel.y ), 0.0f ).w;
	return depth / 3.0f;
}

// Глубина воды над точкой высоты height, м: уровень поверхности у точки минус её высота — урез проходит внутри клетки по
// рельефу, а не по границам клеток сетки 1 м. Уровень — билинейно по четырём клеткам вокруг точки (уровень есть у клеток
// с водой и у кольца за урезом, mainSurface); у клетки без уровня — наименьший из соседних (у озера все равны, у ручья
// на склоне — ниже по течению). < 0 — точка выше воды на столько; воды рядом нет (или нет симуляции) — −1e30
float waterDepthAt( float2 uv, float height )
{
	uint width, height2;
	g_waterLevel.GetDimensions( width, height2 );
	if( width == 0 || height2 == 0 )
		return -1e30f;
	const float2 position = uv * float2( width, height2 ) - 0.5f;
	const int2 base = (int2)floor( position );
	const float2 f = position - (float2)base;
	float levels[4];
	float lowest = 1e30f;
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const int2 cell = clamp( base + int2( k & 1, k >> 1 ), 0, int2( width, height2 ) - 1 );
		levels[k] = g_waterLevel.Load( int3( cell, 0 ) );
		if( levels[k] > -1e29f )
			lowest = min( lowest, levels[k] );
	}
	if( lowest > 1e29f )
		return -1e30f;
	[unroll] for( int j = 0; j < 4; ++j )
		levels[j] = levels[j] > -1e29f ? levels[j] : lowest;
	const float level = lerp( lerp( levels[0], levels[1], f.x ), lerp( levels[2], levels[3], f.x ), f.y );
	return level - height;
}

// Вода ушла недавно (simulated: глубина с памятью ~30 с больше нынешней), м; у static — 0
float recededWaterDepth( float2 uv )
{
	return max( smoothedRememberedDepth( uv ) - smoothedWaterDepth( uv ), 0.0f );
}

// Влажность земли в точке высоты height: под водой и узкой каймой выше уреза (до 2 см над уровнем — вода по капиллярам),
// урез — по глубине в точке (waterDepthAt); и где вода недавно ушла — сохнет не сразу
float waterWetness( float2 uv, float height )
{
	return max( smoothstep( -0.02f, 0.0f, waterDepthAt( uv, height ) ), smoothstep( 0.005f, 0.02f, recededWaterDepth( uv ) ) );
}

// Доля растительности, которую убирает вода в точке высоты height (расстановка, scatter.cs, частицы): в воде глубже
// 1–3 см не растёт ничего — ни трава, ни деревья; мокрый берег не трогается. Где вода недавно ушла — тоже (simulated:
// перекатывающийся край мелкой воды не заставляет траву исчезать и вырастать каждые несколько секунд)
float waterFoliageClear( float2 uv, float height )
{
	return max( smoothstep( 0.01f, 0.03f, waterDepthAt( uv, height ) ), smoothstep( 0.01f, 0.03f, recededWaterDepth( uv ) ) );
}

#endif
