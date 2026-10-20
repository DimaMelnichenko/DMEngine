////////////////////////////////////////////////////////////////////////////////
// Пена окна подробного течения у камеры (NearWaterFlow, docs/water.md, «Подробное течение у камеры»): своё поле пены на
// сетке окна (6,25 см) каждый кадр — по тем же правилам, что поле пены статичной воды (water_foam.sh), но по течению окна:
// пена рождается там, где вода бьёт в камень, выходящий из воды, и где её тонкий слой бежит по камню под самой
// поверхностью, на сдвиге скорости (струя между камнями, край затишья за камнем), на быстрой воде и на сливе; течение окна
// уносит её хвостами за камни и вокруг них, она тает и густеет там, где течение сходится. Вошедшие в окно ячейки и то, что
// течение приносит из-за края окна, — пена поля 0,25 м (WaterFoam). Тексель — ячейка мира по модулю стороны окна, как у
// течения (mainFlowOutput в water_flow.cs); окно — константы b7 этого кадра (cb_waterNear — с прошлого расчёта окна).
// Там же — смещения координат узора пены (advected textures, Neyret 2003; узор — nearFoamPattern в water_shading.sh): два
// слоя смещений несёт течение окна (точка узора, которая сейчас здесь, пришла оттуда, где была при обновлении слоя), слой
// обновляется (смещения — к нулю), когда его фаза переходит через целое (nearPatternPhase, water_near.sh)
////////////////////////////////////////////////////////////////////////////////

#include "water.sh"
#include "water_near_flow_constants.h"
#include "water_foam.sh"

DM_SRV( Texture2D<float4>, g_nearState, 0 );		// итог окна этого кадра: подъём, течение по X и Z мира, доля глубины над камнем
DM_SRV( Texture2D<float>, g_nearFoamPrevious, 1 );	// пена окна прошлого кадра
DM_SRV( Texture2DArray<float>, g_detailFoam, 2 );	// поле пены детальной сетки (WaterFoam): тексели и срезы детальной воды
DM_SRV( Texture2D<float4>, g_nearPatternPrevious, 3 );	// смещения координат узора прошлого кадра: слой 1 — xy, слой 2 — zw, м

DM_UAV( RWTexture2D<float>, g_nearFoam, 0 );
DM_UAV( RWTexture2D<float4>, g_nearPattern, 1 );

// Скачок скорости для сдвига — через две ячейки в каждую сторону (25 см), делится на столько, м: как у сетки 1 м и
// детальной (там скачок через 0,5 м — на 2 м), — тот же порог foam_shear ловит струю у камня, а не профиль поперёк ручья
static const int shearReach = 2;
static const float shearSpan = 2.0f;
// Камень выше воды бьёт воду ячейкам не дальше стольких ячеек от себя
static const int stoneReach = 2;

// Ячейка мира → тексель окна по модулю его стороны
uint2 nearAddress( int2 cell )
{
	const int size = (int)g_nearSize;
	return uint2( ( cell % size + size ) % size );
}

bool insideWindow( int2 cell, int2 origin )
{
	const int2 local = cell - origin;
	return all( local >= 0 ) && all( local < (int)g_nearSize );
}

// Итог окна в ячейке; вне окна — fallback
float4 nearState( int2 cell, float4 fallback )
{
	return insideWindow( cell, g_nearOrigin ) ? g_nearState[nearAddress( cell )] : fallback;
}

// Тексель детальной воды под точкой мира (water.sh: x, y — в текселях плитки с полем, z — срез); false — плитки нет
bool detailTexelAtWorld( float2 world, out float3 texel )
{
	return waterDetailTexel( float2( world.x, cb_waterDetail.x - world.y ) / cb_waterDetail.x, texel );
}

// Поле пены детальной сетки в точке мира: вне плиток — 0
float detailFoamAt( float2 world )
{
	float3 texel;
	if( !detailTexelAtWorld( world, texel ) )
		return 0.0f;
	uint width, height, slices;
	g_detailFoam.GetDimensions( width, height, slices );
	return g_detailFoam.SampleLevel( g_SamplerLinearClamp, float3( texel.xy / float2( width, height ), texel.z ), 0.0f );
}

// Уровень детальной воды в точке мира (тексель под точкой); сухо или плитки нет — fallback
float detailLevelAt( float2 world, float fallback )
{
	float3 texel;
	if( !detailTexelAtWorld( world, texel ) )
		return fallback;
	const float level = g_waterDetail.Load( int4( int2( texel.xy ), texel.z, 0 ) ).x;
	return level > -1e29f ? level : fallback;
}

// Глубина детальной воды в точке мира (без камней окна)
float detailDepthAt( float2 world )
{
	float3 texel;
	return detailTexelAtWorld( world, texel ) ? max( g_waterDetail.Load( int4( int2( texel.xy ), texel.z, 0 ) ).w, 0.0f ) : 0.0f;
}

// Пена прошлого кадра в точке мира — билинейно по ячейкам окна прошлого кадра; ячейки вне него — поле пены 0,25 м
float carriedFoam( float2 world )
{
	const float2 position = world / g_nearStep - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float foam = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		const float value = insideWindow( cell, g_nearPreviousOrigin ) ? g_nearFoamPrevious[nearAddress( cell )] :
																		 detailFoamAt( ( cell + 0.5f ) * g_nearStep );
		foam += value * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return foam;
}

// Смещения узора прошлого кадра в точке мира — билинейно по ячейкам окна прошлого кадра; вне него — 0 (узор только что
// обновлён: у края окна вес узора по течению — 0)
float4 carriedPattern( float2 world )
{
	const float2 position = world / g_nearStep - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float4 offsets = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		if( insideWindow( cell, g_nearPreviousOrigin ) )
			offsets += g_nearPatternPrevious[nearAddress( cell )] * ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
	}
	return offsets;
}

// Пена ячейки за кадр (here — итог окна в ячейке, вода есть)
float stepFoam( int2 cell, float2 world, float4 here );

[numthreads( 8, 8, 1 )]
void mainNearFoam( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_nearSize ) )
		return;
	const int2 cell = g_nearOrigin + int2( id.xy );
	const uint2 address = nearAddress( cell );
	const float2 world = ( cell + 0.5f ) * g_nearStep;
	const float4 here = g_nearState[address];
	// Суша (и камень на берегу) — без пены; вошла в окно в этом кадре (или окно заново) — пена поля 0,25 м, узор — обновлён
	const bool fresh = g_nearReset != 0 || !insideWindow( cell, g_nearPreviousOrigin );
	if( here.w <= 0.0f || fresh )
	{
		g_nearFoam[address] = here.w > 0.0f ? detailFoamAt( world ) : 0.0f;
		g_nearPattern[address] = 0.0f;
		return;
	}
	g_nearFoam[address] = stepFoam( cell, world, here );

	// Смещения узора: назад по течению и на смещение за кадр; слой, чья фаза перешла через целое, — заново
	float4 offsets = carriedPattern( world - g_nearPatternStep * here.yz ) - g_nearPatternStep * here.yzyz;
	const float phase = nearPatternPhase( world, g_nearPatternTime, g_nearPatternPeriod );
	const float before = nearPatternPhase( world, g_nearPatternTime - g_nearPatternStep, g_nearPatternPeriod );
	if( floor( phase ) != floor( before ) )
		offsets.xy = 0.0f;
	if( floor( phase + 0.5f ) != floor( before + 0.5f ) )
		offsets.zw = 0.0f;
	g_nearPattern[address] = offsets;
}

float stepFoam( int2 cell, float2 world, float4 here )
{
	const float2 velocity = here.yz;
	const float speed = length( velocity );

	// Сдвиг скорости и сжатие течения — по соседям окна (за краем окна и у суши — как сама ячейка)
	const float4 east = nearState( cell + int2( shearReach, 0 ), here );
	const float4 west = nearState( cell - int2( shearReach, 0 ), here );
	const float4 north = nearState( cell + int2( 0, shearReach ), here );
	const float4 south = nearState( cell - int2( 0, shearReach ), here );
	const float2 dvdx = ( ( east.w > 0.0f ? east.yz : velocity ) - ( west.w > 0.0f ? west.yz : velocity ) ) / ( 2.0f * shearSpan );
	const float2 dvdz = ( ( north.w > 0.0f ? north.yz : velocity ) - ( south.w > 0.0f ? south.yz : velocity ) ) / ( 2.0f * shearSpan );
	float4 n[4];
	n[0] = nearState( cell + int2( 1, 0 ), here );
	n[1] = nearState( cell - int2( 1, 0 ), here );
	n[2] = nearState( cell + int2( 0, 1 ), here );
	n[3] = nearState( cell - int2( 0, 1 ), here );
	const float divergence = ( n[0].y - n[1].y + n[2].z - n[3].z ) / ( 2.0f * g_nearStep );

	// Слив — уклон детальной воды (уровень на 0,25 м в обе стороны)
	const float level = detailLevelAt( world, 0.0f );
	const float reach = 4.0f * g_nearStep;
	const float slope = length( float2( detailLevelAt( world + float2( reach, 0.0f ), level ) - detailLevelAt( world - float2( reach, 0.0f ), level ),
										detailLevelAt( world + float2( 0.0f, reach ), level ) - detailLevelAt( world - float2( 0.0f, reach ), level ) ) ) /
						( 2.0f * reach );
	float source = foamSource( speed, strainRate( dvdx, dvdz ), slope );

	// Камень у поверхности. В самом камне выше воды пену закрывает камень, а течение сквозь него — просачивание: не источник
	const float fast = saturate( speed / stoneFoamSpeed );
	if( here.w >= nearWaterOpen )
	{
		// Тонкий слой над камнем под самой поверхностью (доля глубины над камнем × глубина без камней)
		if( here.w < 0.95f )
			source += saturate( 1.0f - here.w * detailDepthAt( world ) / stoneFoamDepth ) * fast;
		// Вода бьёт в камень выше воды: ячейки перед камнем по течению (с боков — вдвое слабее, за ним — нет)
		const float2 along = speed > 1e-3f ? velocity / speed : float2( 0.0f, 0.0f );
		float impact = 0.0f;
		[unroll] for( int r = 1; r <= stoneReach; ++r )
			[unroll] for( uint k = 0; k < 4; ++k )
			{
				const int2 offset = k == 0 ? int2( r, 0 ) : k == 1 ? int2( -r, 0 ) : k == 2 ? int2( 0, r ) : int2( 0, -r );
				const float4 neighbor = nearState( cell + offset, here );
				if( neighbor.w > 0.0f && neighbor.w < nearWaterOpen )
					impact = max( impact, saturate( 0.5f + 0.5f * dot( along, normalize( float2( offset ) ) ) ) );
			}
		source += impact * fast;
	}

	// Назад по течению (смещение за кадр — доли ячейки: скорость в середине шага та же)
	const float carried = carriedFoam( world - g_foamTimeStep * velocity );
	return foamStep( carried, divergence, source, speed );
}
