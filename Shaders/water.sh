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

// Влажность земли: под водой и полосой в ячейку-две вокруг неё; плёнка тоньше 5 мм — сухо, от 2 см — мокро. По глубине
// с памятью: земля, по которой прошла вода, сохнет не сразу
float waterWetness( float2 uv )
{
	return smoothstep( 0.005f, 0.02f, smoothedRememberedDepth( uv ) );
}

// Доля растительности, которую убирает вода (расстановка, scatter.cs): в воде глубже 1–3 см не растёт ничего —
// ни трава, ни деревья; мокрый берег не трогается. По глубине с памятью: перекатывающийся край мелкой воды не заставляет
// траву исчезать и вырастать каждые несколько секунд
float waterFoliageClear( float2 uv )
{
	return smoothstep( 0.01f, 0.03f, smoothedRememberedDepth( uv ) );
}

#endif
