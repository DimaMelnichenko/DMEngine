////////////////////////////////////////////////////////////////////////////////
// Правила пены, которую несёт течение (docs/water.md, «Пена»), — общие у поля пены статичной воды при загрузке
// (water_foam.cs, WaterFoam) и у пены окна подробного течения у камеры каждый кадр (water_near_foam.cs, NearWaterFlow):
// источник (быстрая вода, сдвиг скорости, падение со ступени, камень у поверхности) и шаг (перенос даёт вызывающий: таяние
// по времени жизни, сжатие, рост к источнику). Константы — WaterFoamBuffer (b5). Уровень поверхности для падения даёт
// вызывающий: fallLevelAt
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_FOAM_SH
#define WATER_FOAM_SH

#include "water_foam_constants.h"

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

// Источник пены: быстрая вода (полная — вдвое быстрее foamSpeed), сдвиг
float foamSource( float speed, float shear )
{
	return saturate( ( speed - g_foamSpeed ) / g_foamSpeed ) + saturate( shear / g_foamShear - 0.5f );
}

// Падение со ступени у точки — по уровню поверхности вдоль течения. Как на ступенчатых водосбросах (точка начала аэрации,
// Chanson): сверху вода прозрачная, воздух в поток попадает ниже, и больше всего пены — у подошвы и в котле, куда бьёт струя
// (Dimok, 2026-10-22, по видео ручьёв: «верх должен быть чистый, пена начинается с середины падения»; прежде пена слива белила
// всю стенку уступа от кромки)
struct Fall
{
	float above;	// от кромки до точки, м
	float below;	// от точки до подошвы, м
	float plunge;	// точка в котле под падением (полого, выше по течению — стенка): расстояние до подошвы, м; на стенке — 0
	float ahead;	// спад уровня вперёд по течению на lipReach, м: у кромки следующей ступени — велик
};

// Насколько вперёд смотреть, нет ли кромки следующей ступени (бурление там не рождается), м
static const float lipReach = 0.3f;

// Падение, ниже которого — не падение, м
static const float minFallHeight = 0.03f;

// Уровень поверхности в точке мира, м; сухо или воды нет — fallback (у вызывающего: окно у камеры, детальная сетка)
float fallLevelAt( float2 world, float fallback );

// Падение у точки world с уровнем level: шаги step вдоль течения along (единичный вектор), не больше steps в каждую сторону.
// Вверх — пока поверхность круче foamFallSlope (стенка; её верх — кромка), полого до первой стенки — котёл, не дальше
// foamPlungeLength; вниз — до подошвы
Fall traceFall( float2 world, float2 along, float level, float step, uint steps )
{
	Fall fall = (Fall)0;
	const float rise = g_foamFallSlope * step;
	float previous = level;
	bool wall = false;
	[loop] for( uint k = 1; k <= steps; ++k )
	{
		const float next = fallLevelAt( world - along * ( k * step ), previous );
		if( next - previous > rise )
		{
			wall = true;
			fall.above = next - level;
		}
		else if( wall )
			break;
		else
		{
			fall.plunge += step;
			if( fall.plunge > g_foamPlungeLength )
				break;
		}
		previous = next;
	}
	if( !wall )
		fall.plunge = 0.0f;
	fall.ahead = level - fallLevelAt( world + along * lipReach, level );
	previous = level;
	[loop] for( uint m = 1; m <= steps; ++m )
	{
		const float next = fallLevelAt( world + along * ( m * step ), previous );
		if( previous - next <= rise )
			break;
		fall.below = level - next;
		previous = next;
	}
	return fall;
}

// Доля падения, где пена есть: 0 — ближе foamFallStart к кромке (по высоте), 1 — у подошвы и в котле; падение ниже
// foamFallStart пены не даёт; не падение — 1
float fallFoamShare( Fall fall )
{
	const float height = fall.above + fall.below;
	return height >= minFallHeight ? smoothstep( g_foamFallStart, max( height, g_foamFallStart + 0.02f ), fall.above ) : 1.0f;
}

// Сила падения в точке 0…1: с foamFallStart ниже кромки растёт к подошве, у подошвы и в котле — 1 (падение ниже
// foamFallHeight — меньше), в котле гаснет к foamPlungeLength от подошвы; не падение — 0
float fallStrength( Fall fall )
{
	const float height = fall.above + fall.below;
	if( height < minFallHeight )
		return 0.0f;
	return fallFoamShare( fall ) * saturate( height / g_foamFallHeight ) * ( 1.0f - smoothstep( 0.0f, g_foamPlungeLength, fall.plunge ) );
}

// Пена падения на поверхности: сила падения × foamFall
float fallFoam( Fall fall )
{
	return g_foamFall * fallStrength( fall );
}

// Бурление падения 0…1 (частицы spawn fall): сила падения у его подошвы и в котле — где струя входит в воду; на стенке
// падения (ниже по течению ещё круто) — нет: бурление рождается на дне котла, а не в самом водопаде (Dimok, 2026-10-22).
// И не у кромки следующей ступени: котёл каскада короткий, и вода уже начинает спадать дальше (спад на lipReach впереди —
// от половины крутизны стенки), — иначе бурление рождалось в начале спада (Dimok, 2026-10-22). От пены на поверхности
// (foamFall) не зависит: основную белизну у падения дают частицы, на поверхности после них — тонкая пена
float fallChurn( Fall fall )
{
	const float lip = 1.0f - smoothstep( 0.25f, 0.5f, fall.ahead / ( g_foamFallSlope * lipReach ) );
	return fall.below > 0.0f ? 0.0f : fallStrength( fall ) * lip;
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
