////////////////////////////////////////////////////////////////////////////////
// Правила пены, которую несёт течение (docs/water.md, «Пена»), — общие у поля пены статичной воды при загрузке
// (water_foam.cs, WaterFoam) и у пены окна подробного течения у камеры каждый кадр (water_near_foam.cs, NearWaterFlow):
// источник (быстрая вода, сдвиг скорости, слив, камень у поверхности) и шаг (перенос даёт вызывающий: таяние по времени
// жизни, сжатие, рост к источнику). Константы — WaterFoamBuffer (b5)
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_FOAM_SH
#define WATER_FOAM_SH

#include "water_foam_constants.h"

static const float fallFoam = 0.6f;			// пена на стенке уступа: узор даёт белые полосы по течению, а не сплошное пятно
static const float maxFoam = 2.0f;			// больше — та же сплошная белая вода
static const float maxCompression = 0.15f;	// сжатие и растяжение пены за шаг — не больше exp(±)
// Пена у камня, который выходит к поверхности: вода над ним тоньше этого, м, — сильнее на быстрой воде (полная — с
// stoneFoamSpeed); у сетки 1 м — по площади камней в клетке (полная — stoneFoamArea: камень 0,4 м, погружённый на 0,2 м)
static const float stoneFoamDepth = 0.12f;
static const float stoneFoamSpeed = 0.5f;		// м/с
static const float stoneFoamArea = 0.08f;

// Сдвиг скорости — норма тензора скоростей деформации ½(∇v + ∇vᵀ) × √2: при чистом сдвиге она равна прежнему скачку
// скорости (|∂v/∂x| + |∂v/∂z|), а вращение воды целиком её не меняет. dvdx = (∂vx/∂x, ∂vz/∂x), dvdz = (∂vx/∂z, ∂vz/∂z)
float strainRate( float2 dvdx, float2 dvdz )
{
	const float exz = 0.5f * ( dvdz.x + dvdx.y );
	return sqrt( 2.0f * ( dvdx.x * dvdx.x + dvdz.y * dvdz.y + 2.0f * exz * exz ) );
}

// Источник пены: быстрая вода (полная — вдвое быстрее foamSpeed), сдвиг, слив (полная — втрое круче foamSlope)
float foamSource( float speed, float shear, float slope )
{
	return saturate( ( speed - g_foamSpeed ) / g_foamSpeed ) + saturate( shear / g_foamShear - 0.5f ) +
		   fallFoam * saturate( ( slope - g_foamSlope ) / ( 2.0f * g_foamSlope ) );
}

// Пена за шаг: принесённая течением тает (в медленной воде — дольше) и сжимается, у источника растёт к нему
float foamStep( float carried, float divergence, float source, float speed )
{
	// С той скорости, на которой пена рождается (foam_speed), — foamLifetime, в стоячей воде — foamStillLifetime, между
	// ними — по логарифму: вода около 1 м/с ближе к быстрой (на каскадах пена не заливает плёсы), стоячая держит пену долго
	const float lifetime = g_foamStillLifetime * pow( g_foamLifetime / g_foamStillLifetime, saturate( speed / g_foamSpeed ) );
	const float compression = exp( clamp( -divergence * g_foamTimeStep, -maxCompression, maxCompression ) );
	const float foam = carried * exp( -g_foamTimeStep / lifetime ) * compression;
	return min( foam + max( source - foam, 0.0f ) * g_foamGrowth, maxFoam );
}

#endif
