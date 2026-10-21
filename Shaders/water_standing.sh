////////////////////////////////////////////////////////////////////////////////
// Стоячие волны над перекатами (docs/water.md, «Стоячие волны над перекатами»): волна стоит на месте, когда её скорость
// против течения равна скорости воды. Длина — из дисперсии волн на воде, сила — по числу Фруда (на глубоком медленном плёсе
// волн нет, на мелком быстром перекате — есть). Два узора: короткие гребни на плитках ряби (water_shading.sh, standingWaves)
// и длинные — через всё русло, по фазе вдоль оси ручья (SLOT_WATER_STANDING: ∫ k ds при загрузке, TerrainHydrology);
// длинные в окне подробного течения у камеры — ещё и геометрия его сетки (water.vs с WATER_NEAR): поверхность и отражение
// по ней изгибаются. Общее у материала воды и сетки окна
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_STANDING_SH
#define WATER_STANDING_SH

#include "water_surface.sh"
#include "water.sh"

// Фаза вдоль оси ручья, рад, и волновое число, 1/м (0 — не ручей): тексели и срезы — как у g_waterDetail
DM_SRV( Texture2DArray<float2>, g_waterStanding, SLOT_WATER_STANDING );

static const float standingSlope = 0.18f;			// наклон при силе 1 (среднеквадратичный)
static const float standingFroudeStart = 0.5f;		// число Фруда v / √(g h): волны с этого, полные — с standingFroudeFull
static const float standingFroudeFull = 0.9f;
static const float standingMinWavelength = 0.1f;	// м
static const float standingMinDepth = 0.03f;		// мельче — волн нет, м
static const float standingMinSpeed = 0.3f;			// м/с: медленнее — числа Фруда для волн нет и на мелкой воде
static const float gravityAcceleration = 9.81f;
// Длинные гребни: не ровный вал — высота вдоль гребня меняется (доля standingCrestVariation на пятнах standingCrestNoiseCell)
// и гребень изгибается (фаза сдвигается шумом до standingCrestBend рад)
static const float standingCrestNoiseCell = 1.5f;	// м
static const float standingCrestVariation = 0.6f;
static const float standingCrestBend = 1.2f;
// Геометрия в сетке окна — где волна длиннее standingGeometryCells её ячеек (короче сетка её не держит)
static const float standingGeometryCells = 4.0f;

// Волновое число стоячей волны на течении, 1/м: скорость волны против течения равна скорости воды — дисперсия волн на воде
// конечной глубины v² = g · th(k h) / k (Finch, GPU Gems 1, гл. 1: скорость волны — от её длины); у почти критического
// течения (Фруд → 1) решение уходит в длинные волны — не длиннее 2π глубины (k h ≥ 1); не короче standingMinWavelength.
// То же на CPU — StandingWaves.h (фаза вдоль оси ручья)
float standingWavenumber( float speed, float depth )
{
	const float v2 = max( speed * speed, 1e-4f );
	const float h = max( depth, 0.01f );
	float k = gravityAcceleration / v2;
	[unroll] for( int i = 0; i < 3; ++i )
		k = max( gravityAcceleration * tanh( k * h ) / v2, 1.0f / h );
	return min( k, 6.2831853f / standingMinWavelength );
}

// Сила стоячих волн — наклон (среднеквадратичный) по числу Фруда и глубине; на медленной и мелкой воде — 0
float standingWaveStrength( float speed, float depth )
{
	[branch] if( g_standingWaves <= 0.0f || speed <= standingMinSpeed )
		return 0.0f;
	const float froude = speed / sqrt( gravityAcceleration * max( depth, 0.01f ) );
	return smoothstep( standingFroudeStart, standingFroudeFull, froude ) * saturate( depth / standingMinDepth - 1.0f ) * standingSlope *
		   g_standingWaves;
}

// Фаза и волновое число длинных гребней в текселе детальной воды texel (waterDetailTexel) — билинейно по текселям ручья
// (у берега и озера фазы нет: соседний тексель без неё не тянет фазу к нулю); (0, 0) — не ручей
float2 sampleStanding( float3 texel )
{
	const float2 position = texel.xy - 0.5f;
	const int2 base = int2( floor( position ) );
	const float2 f = position - (float2)base;
	float2 sum = 0.0f;
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const float2 value = g_waterStanding.Load( int4( base + corner, texel.z, 0 ) );
		const float weight = value.y > 0.0f ? ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y ) : 0.0f;
		sum += weight * value;
		weights += weight;
	}
	return weights > 1e-4f ? sum / weights : 0.0f;
}

// PCG (Jarzynski, Olano 2020)
uint standingHash( uint value )
{
	const uint state = value * 747796405u + 2891336453u;
	const uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

// Плавный шум 0…1 в точке мира (решётка cell, smoothstep между узлами)
float standingNoise( float2 xz, float cell, uint seed )
{
	const float2 position = xz / cell;
	const int2 base = int2( floor( position ) );
	const float2 f = smoothstep( 0.0f, 1.0f, position - (float2)base );
	float noise = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = base + int2( k & 1, k >> 1 );
		const float value = ( standingHash( (uint)corner.x + standingHash( (uint)corner.y + seed ) ) >> 8u ) / 16777216.0f;
		noise += value * ( k & 1 ? f.x : 1.0f - f.x ) * ( k >> 1 ? f.y : 1.0f - f.y );
	}
	return noise;
}

// Длинный гребень в точке: высота (x, доля амплитуды наклона, умножить на наклон / k — метры) и наклон вдоль течения (y, доля
// амплитуды наклона, × along) при фазе standing.x; амплитуда наклона — √2 (среднеквадратичный — 1, как у узора на плитках)
float2 standingCrest( float2 xz, float2 standing )
{
	const float phase = standing.x + standingCrestBend * ( standingNoise( xz, standingCrestNoiseCell, 0x9e3779b9u ) - 0.5f ) * 2.0f;
	const float amplitude = 1.41421356f * ( 1.0f - standingCrestVariation * standingNoise( xz, standingCrestNoiseCell, 0x7f4a7c15u ) );
	return amplitude * float2( sin( phase ), cos( phase ) );
}

// Доля длинных гребней геометрией сетки окна у камеры в точке мира: по весу окна, где волна длиннее standingGeometryCells
// ячеек окна; столько же их наклона уже в нормали сетки — материал добавляет остальное
float standingGeometryShare( float2 xz, float wavenumber )
{
	if( wavenumber <= 0.0f || cb_waterNear.w <= 0.0f )
		return 0.0f;
	return nearWaterWeight( xz ) * saturate( 6.2831853f / wavenumber / ( standingGeometryCells * cb_waterNear.z ) - 1.0f );
}

// Высота длинных гребней над уровнем воды в точке мира, м, — геометрии сетки окна (× standingGeometryShare); 0 — не ручей
float standingCrestHeight( float2 xz )
{
	const float2 uv = float2( xz.x, cb_waterDetail.x - xz.y ) / cb_waterDetail.x;
	float3 texel;
	if( g_standingCrest <= 0.0f || !waterDetailTexel( uv, texel ) )
		return 0.0f;
	const float2 standing = sampleStanding( texel );
	if( standing.y <= 0.0f )
		return 0.0f;
	uint width, height, slices;
	g_waterDetail.GetDimensions( width, height, slices );
	const float4 water = g_waterDetail.SampleLevel( g_SamplerLinearClamp, float3( texel.xy / float2( width, height ), texel.z ), 0.0f );
	const float strength = standingWaveStrength( length( water.yz ), water.w );
	return strength * g_standingCrest * standingCrest( xz, standing ).x / standing.y * standingGeometryShare( xz, standing.y );
}

#endif
