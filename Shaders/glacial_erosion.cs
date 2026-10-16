////////////////////////////////////////////////////////////////////////////////
// Ледниковая эрозия (GlacialErosion): лёд — слой толщины H на сетке ложа b, течёт по уклону своей поверхности s = b + H в
// приближении мелкого льда (shallow ice approximation, Mahaffy 1976; как в моделях ландшафтной эволюции Braun 1999,
// Herman, Braun 2008): поток q = −D ∇s, D = D_d + D_s — течение (закон Глена, n = 3: D_d ∝ H⁵ |∇s|²) и скольжение по ложу
// (u_s ∝ τ_b² ∝ H² |∇s|², D_s = u_s H / |∇s|). Баланс массы — от высоты поверхности над линией равновесия ELA (выше
// копится, не больше maxAccumulation, ниже тает). Ложе стирается пропорционально скорости скольжения (Harbor 1988,
// Humphrey, Raymond 1994): так выходят троги с U-профилем, кары и висячие долины. Поток между клетками — с донора: из клетки
// уходит не больше, чем в ней льда (как у модели виртуальных труб воды) — толщина не уходит в минус; D — не выше предела
// устойчивости шага. Два прохода на шаг: mainFlux — сколько льда уходит в каждую из четырёх соседних клеток и скорость
// размыва, mainUpdate — лёд с притоком и балансом, ложе
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"

#include "glacial_erosion_constants.h"

DM_UAV( RWTexture2D<float>, g_bed, 0 );			// ложе, м
DM_UAV( RWTexture2D<float>, g_ice, 1 );			// толщина льда, м
DM_UAV( RWTexture2D<float4>, g_outflow, 2 );		// уходит за шаг, м льда: x — на восток, y — на запад, z — на север, w — на юг
DM_UAV( RWTexture2D<float>, g_erosionRate, 3 );	// размыв ложа за шаг, м
DM_UAV( RWTexture2D<float>, g_iceMax, 4 );		// наибольшая толщина льда за всё время, м
DM_UAV( RWTexture2D<float>, g_sourceBed, 5 );		// исходное ложе, м — предел размыва

static const int2 neighbors[4] = { int2( 1, 0 ), int2( -1, 0 ), int2( 0, -1 ), int2( 0, 1 ) };	// восток, запад, север, юг

int2 clampCell( int2 cell )
{
	return clamp( cell, 0, int( g_size ) - 1 );
}

// Поверхность льда в клетке; за краем сетки — ложе крайней клетки без льда: лёд уходит за край
float surfaceAt( int2 cell )
{
	const int2 inside = clampCell( cell );
	const float bed = g_bed[inside];
	return any( inside != cell ) ? bed : bed + g_ice[inside];
}

[numthreads( 8, 8, 1 )]
void mainFlux( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float ice = g_ice[cell];
	if( ice <= 0.0f )
	{
		g_outflow[cell] = 0.0f;
		g_erosionRate[cell] = 0.0f;
		return;
	}
	const float surface = g_bed[cell] + ice;
	float neighborSurface[4];
	[unroll] for( uint i = 0; i < 4; ++i )
		neighborSurface[i] = surfaceAt( cell + neighbors[i] );
	// Уклон поверхности — центральными разностями (нелинейность течения), поток через грань — по разности с соседом
	const float2 gradient = float2( neighborSurface[0] - neighborSurface[1], neighborSurface[2] - neighborSurface[3] ) / ( 2.0f * g_cell );
	const float slope2 = dot( gradient, gradient );
	const float slope = sqrt( slope2 );
	const float h2 = ice * ice;
	const float deformation = g_deformation * h2 * h2 * ice * slope2;
	const float slidingSpeed = g_sliding * h2 * slope2;
	const float slidingD = slope > 1e-6f ? slidingSpeed * ice / slope : 0.0f;
	const float uncapped = deformation + slidingD;
	const float diffusion = min( uncapped, g_diffusionCap );
	float4 outflow = 0.0f;
	[unroll] for( uint n = 0; n < 4; ++n )
	{
		const float drop = surface - neighborSurface[n];
		if( drop > 0.0f )
			outflow[n] = diffusion * drop / ( g_cell * g_cell ) * g_timeStep;
	}
	// Уходит не больше, чем есть
	const float total = dot( outflow, 1.0f );
	if( total > ice )
		outflow *= ice / total;
	g_outflow[cell] = outflow;
	// Скольжение — с той же долей, что срезал предел D: скорость льда не растёт без конца
	const float speed = uncapped > 0.0f ? slidingSpeed * diffusion / uncapped : 0.0f;
	g_erosionRate[cell] = g_erosion * speed * g_timeStep;
}

[numthreads( 8, 8, 1 )]
void mainUpdate( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float4 own = g_outflow[cell];
	float ice = g_ice[cell] - dot( own, 1.0f );
	// Приток: восточный сосед отдаёт на запад, западный — на восток, северный — на юг, южный — на север
	const int last = int( g_size ) - 1;
	if( cell.x < last )
		ice += g_outflow[cell + int2( 1, 0 )].y;
	if( cell.x > 0 )
		ice += g_outflow[cell + int2( -1, 0 )].x;
	if( cell.y > 0 )
		ice += g_outflow[cell + int2( 0, -1 )].w;
	if( cell.y < last )
		ice += g_outflow[cell + int2( 0, 1 )].z;

	float bed = g_bed[cell];
	// Баланс массы по высоте поверхности: выше ELA копится (не больше maxAccumulation), ниже тает
	const float balance = min( g_balanceGradient * ( bed + max( ice, 0.0f ) - g_ela ), g_maxAccumulation );
	ice = max( ice + balance * g_timeStep, 0.0f );

	[branch] if( g_erode != 0 )
	{
		// У карты уровня эрозия гаснет: её край сводится с горами — глубокий трог у края дал бы ступень
		const float2 world = g_origin + float2( id.x + 0.5f, g_size - id.y - 0.5f ) * g_cell;
		const float2 outside = max( max( -world, world - g_mapSize ), 0.0f );
		const float fade = smoothstep( g_mapFade.x, g_mapFade.y, length( outside ) );
		bed = max( bed - g_erosionRate[cell] * fade, g_sourceBed[cell] - g_maxErosion * fade );
		g_bed[cell] = bed;
	}
	g_ice[cell] = ice;
	g_iceMax[cell] = max( g_iceMax[cell], ice );
}
