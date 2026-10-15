////////////////////////////////////////////////////////////////////////////////
// Тень рельефа от источника теней (солнце, ночью — луна) дальше каскадов (TerrainSunShadow): для точки карты — высота
// края тени над ней и расстояние до заслона. Луч из точки к солнцу идёт над рельефом; точка карты на расстоянии s по
// азимуту солнца высотой h заслоняет всё ниже h − s · tg(высоты солнца) над исходной точкой, край тени — наибольшая из
// этих высот. Точка мира ниже края — в тени (terrainSunShadow в Shaders/shadows.sh), выше — на свету: так тень получают и
// земля, и кроны деревьев над ней, и туман в воздухе. Хранится запас земли над краем (отрицательный — земля в тени) и
// сама земля: точка мира сравнивается с краем по своей высоте над землёй, и неровности мельче текселя не затеняют
// ровную землю при низком солнце. Шаг растёт с расстоянием, и вдали высоты берутся из мипа по шагу: края тени вдали
// мягкие и так. Луч кончается у края карты или когда выше края уже ничто не поднимется
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"
#include "samplers.sh"

#include "terrain_sun_shadow_constants.h"

DM_SRV( Texture2D<float>, g_heightMap, 0 );
DM_UAV( RWTexture2D<float4>, g_result, 0 );

static const uint maxSteps = 384;
// Шаг — доля пройденного расстояния (не меньше текселя результата): ~220 шагов на 1,4 км
static const float stepFraction = 0.02f;
// Край тени без заслона — ниже земли: билинейная выборка у границы тени не уводит его в бесконечность
static const float noShadowDepth = 64.0f;

float heightAt( float2 world, float mip )
{
	const float2 uv = float2( world.x, g_worldSize - world.y ) / g_worldSize;
	return g_heightMap.SampleLevel( g_SamplerLinearClamp, uv, mip ) * g_heightMultiplier + g_heightOffset;
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	// Тексель результата: строки сверху вниз, как у карты высот (v = 1 − z / сторона)
	const float2 world = float2( id.x + 0.5f, g_size - id.y - 0.5f ) * g_texelSize;
	const float ground = heightAt( world, 0.0f );
	float edge = ground - noShadowDepth;
	float occluder = 0.0f;
	float distance = g_startDistance;
	[loop] for( uint i = 0; i < maxSteps; ++i )
	{
		const float2 probe = world + g_sunAzimuth * distance;
		if( any( probe < 0.0f ) || any( probe > g_worldSize ) )
			break;
		const float step = max( g_texelSize, distance * stepFraction );
		const float height = heightAt( probe, max( log2( step / g_mapTexel ) - 1.0f, 0.0f ) ) - distance * g_sunSlope;
		if( height > edge )
		{
			edge = height;
			occluder = distance;
		}
		// Дальше луч к солнцу выше любой высоты рельефа над точкой — заслонять нечему
		if( g_maxHeight - distance * g_sunSlope < max( edge, ground ) )
			break;
		distance += step;
	}
	// x — запас земли над краем тени, м; y — расстояние до заслона, м (ширина полутени); z — высота земли, м
	g_result[id.xy] = float4( ground - edge, occluder, ground, 0.0f );
}
