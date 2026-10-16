////////////////////////////////////////////////////////////////////////////////
// Тень рельефа от источника теней (солнце, ночью — луна) дальше каскадов (TerrainSunShadow): для точки области — высота
// края тени над ней и расстояние до заслона. Луч из точки к солнцу идёт над рельефом; точка на расстоянии s по азимуту
// солнца высотой h заслоняет всё ниже h − s · tg(высоты солнца) над исходной точкой, край тени — наибольшая из этих высот.
// Точка мира ниже края — в тени (terrainSunShadow в Shaders/shadows.sh), выше — на свету: так тень получают и земля, и
// кроны деревьев над ней, и туман в воздухе. Хранится запас земли над краем (отрицательный — земля в тени) и сама земля:
// точка мира сравнивается с краем по своей высоте над землёй, и неровности мельче текселя не затеняют ровную землю при
// низком солнце. Шаг растёт с расстоянием, и вдали высоты берутся из мипа по шагу: края тени вдали мягкие и так. Рельеф —
// карта уровня, за её краем — горы вокруг неё (FarTerrain), если они есть; луч кончается, где кончается рельеф, или когда
// выше края уже ничто не поднимется. Область — карта уровня (заслоны за её краем, горы, — во второй цели: g_external) или
// всё кольцо гор
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"
#include "samplers.sh"

#include "terrain_sun_shadow_constants.h"

DM_SRV( Texture2D<float>, g_heightMap, 0 );		// карта уровня, нормированная
DM_SRV( Texture2D<float>, g_farHeightMap, 1 );	// горы вокруг неё, м
DM_UAV( RWTexture2D<float4>, g_result, 0 );
DM_UAV( RWTexture2D<float2>, g_externalResult, 1 );

static const uint maxSteps = 768;
// Шаг — доля пройденного расстояния (не меньше текселя результата): ~220 шагов на 1,4 км, ~560 — до края гор в 32 км
static const float stepFraction = 0.02f;
// Край тени без заслона — ниже земли: билинейная выборка у границы тени не уводит его в бесконечность
static const float noShadowDepth = 64.0f;

// Высота рельефа в точке мира, м; мип — по шагу марша step, м. inMap — точка на карте уровня; false — рельефа там нет
bool heightAt( float2 world, float step, out float height, out bool inMap )
{
	height = 0.0f;
	inMap = g_worldSize > 0.0f && all( world >= 0.0f ) && all( world <= g_worldSize );
	if( inMap )
	{
		const float2 uv = float2( world.x, g_worldSize - world.y ) / g_worldSize;
		const float mip = step > 0.0f ? max( log2( step / g_mapTexel ) - 1.0f, 0.0f ) : 0.0f;
		height = g_heightMap.SampleLevel( g_SamplerLinearClamp, uv, mip ) * g_heightMultiplier + g_heightOffset;
		return true;
	}
	const float2 local = world - g_farOrigin;
	if( g_farExtent <= 0.0f || any( local < 0.0f ) || any( local > g_farExtent ) )
		return false;
	const float2 uv = float2( local.x, g_farExtent - local.y ) / g_farExtent;
	const float mip = step > 0.0f ? max( log2( step / g_farTexel ) - 1.0f, 0.0f ) : 0.0f;
	height = g_farHeightMap.SampleLevel( g_SamplerLinearClamp, uv, mip );
	return true;
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	// Тексель результата: строки сверху вниз, как у карт высот (v = 1 − z / сторона)
	const float2 world = g_areaOrigin + float2( id.x + 0.5f, g_size - id.y - 0.5f ) * g_texelSize;
	float ground;
	bool inMap;
	if( !heightAt( world, 0.0f, ground, inMap ) )
	{
		g_result[id.xy] = float4( noShadowDepth, 0.0f, 0.0f, 0.0f );
		if( g_external != 0 )
			g_externalResult[id.xy] = float2( noShadowDepth, 0.0f );
		return;
	}
	float edge = ground - noShadowDepth;
	float occluder = 0.0f;
	float externalEdge = ground - noShadowDepth;
	float externalOccluder = 0.0f;
	float distance = g_startDistance;
	[loop] for( uint i = 0; i < maxSteps; ++i )
	{
		const float2 probe = world + g_sunAzimuth * distance;
		const float step = max( g_texelSize, distance * stepFraction );
		float height;
		bool probeInMap;
		if( !heightAt( probe, step, height, probeInMap ) )
			break;
		height -= distance * g_sunSlope;
		if( g_external != 0 && !probeInMap )
		{
			if( height > externalEdge )
			{
				externalEdge = height;
				externalOccluder = distance;
			}
		}
		else if( height > edge )
		{
			edge = height;
			occluder = distance;
		}
		// Дальше луч к солнцу выше любой высоты рельефа над точкой — заслонять нечему
		if( g_maxHeight - distance * g_sunSlope < max( max( edge, externalEdge ), ground ) )
			break;
		distance += step;
	}
	// x — запас земли над краем тени, м; y — расстояние до заслона, м (ширина полутени); z — высота земли, м
	g_result[id.xy] = float4( ground - edge, occluder, ground, 0.0f );
	if( g_external != 0 )
		g_externalResult[id.xy] = float2( ground - externalEdge, externalOccluder );
}
