////////////////////////////////////////////////////////////////////////////////
// Объём воздушной перспективы главного вида (S. Hillaire, EGSR 2020, раздел 6 — Camera Aerial Perspective Volume
// в UE5): сетка AERIAL_PERSPECTIVE_SIZE² над экраном × AERIAL_PERSPECTIVE_DEPTH слоёв по расстоянию от камеры.
// Слой s — от камеры до расстояния D · ((s + 1) / N)², D — дальняя плоскость (cb_aerialPerspectiveDistance): слои
// гуще у камеры. В слое — свет солнца и луны, рассеянный воздухом на этом пути (в единицах запекания, как небо), и
// среднее пропускание. Та же модель, что небо (integrateScattering, Shaders/atmosphere.sh); камера — наблюдатель
// неба на ATMOSPHERE_OBSERVER_ALTITUDE, точки берутся относительно неё. Поток — столбец объёма: идёт от камеры
// по слоям и пишет накопленное к концу каждого. Каждый кадр, класс SkyAtmosphere
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "atmosphere.sh"
#include "bindless.sh"

DM_UAV( RWTexture3D<float4>, g_volume, 0 );

// Раскладка — SkyAtmosphere::updateAerialPerspective
cbuffer AerialPerspectiveBuffer : register( b4 )
{
	float  g_viewDistanceScale;	// Aerial Perspective View Distance Scale: во сколько раз путь в воздухе длиннее, 0 — выключено
	float  g_outputScale;		// 1 / нормировка (SkyAtmosphere::aerialPerspectiveNormalization): объём — в половинной точности
	float2 g_aerialPadding;
};

// Шагов интегрирования на слой (AerialPerspectiveLUTSampleCountMaxPerSlice в UE)
static const int samplesPerSlice = 2;

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	// Луч через центр ячейки экрана: из экранных координат в пространство камеры, затем в мир (как в sky_background.ps)
	const float2 uv = ( id.xy + 0.5f ) / AERIAL_PERSPECTIVE_SIZE;
	const float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	const float3 viewDirection = float3( ndc.x / cb_projectionMatrix[0][0], ndc.y / cb_projectionMatrix[1][1], 1.0f );
	const float3 direction = normalize( mul( viewDirection, (float3x3)cb_viewInverseMatrix ) );
	const float3 origin = float3( 0.0f, planetRadius + observerAltitude, 0.0f );

	float3 luminance = 0.0f;
	float3 transmittance = 1.0f;
	float start = 0.0f;
	[loop] for( uint slice = 0; slice < AERIAL_PERSPECTIVE_DEPTH; ++slice )
	{
		const float t = ( slice + 1.0f ) / AERIAL_PERSPECTIVE_DEPTH;
		const float end = cb_aerialPerspectiveDistance * t * t * g_viewDistanceScale;
		integrateScattering( origin, direction, start, end, samplesPerSlice, g_sunColor, g_moonColor, luminance, transmittance );
		start = end;
		g_volume[uint3( id.xy, slice )] = float4( luminance * ( g_skyIntensity * g_outputScale ), dot( transmittance, 1.0f / 3.0f ) );
	}
}
