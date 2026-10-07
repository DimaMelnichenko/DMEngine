////////////////////////////////////////////////////////////////////////////////
// Объёмный туман (Volumetric Fog в UE; B. Wronski, «Volumetric Fog», SIGGRAPH 2014; S. Hillaire, «Physically Based and
// Unified Volumetric Rendering in Frostbite», SIGGRAPH 2015): сетка над экраном главного вида — ячейки
// VOLUMETRIC_FOG_TILE² пикселей × VOLUMETRIC_FOG_DEPTH слоёв по глубине взгляда до дальности объёма. Два прохода:
// - mainLight — ячейка: плотность тумана по высоте (Shaders/height_fog.sh) и свет, рассеянный к камере: солнце (луна)
//   с каскадной тенью — лучи в дымке, небо, лампы. Точка в ячейке сдвигается каждый кадр, результат смешивается
//   с прошлым кадром, перенесённым по движению камеры (temporal reprojection): тени без ступенек слоёв;
// - mainIntegrate — столбец: от камеры по слоям накапливает свет и пропускание, слой хранит накопленное к своему
//   концу. Его читает applyHeightFog. Каждый кадр после карты теней, класс VolumetricFog
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "height_fog.sh"
#include "bindless.sh"

// Раскладка — VolumetricFog::Parameters
cbuffer VolumetricFogBuffer : register( b4 )
{
	matrix g_previousViewProjection;	// главный вид прошлого кадра
	float3 g_jitter;			// сдвиг точки в ячейке этого кадра, 0…1
	float  g_historyWeight;		// доля прошлого кадра; 0 — смена плана, прошлого нет
	uint3  g_gridSize;
	float  g_historyScale;		// нормировка прошлого кадра / нынешняя
	float  g_inverseScale;		// 1 / cb_fogScale: объём хранит свет, делённый на нормировку
	float3 g_fogPadding;
};

DM_SRV( Texture3D<float4>, g_history, 0 );		// свет и плотность ячеек прошлого кадра
DM_SRV( Texture3D<float4>, g_lighting, 1 );		// свет и плотность ячеек этого кадра (mainIntegrate)
DM_UAV( RWTexture3D<float4>, g_lightingOut, 0 );
DM_UAV( RWTexture3D<float4>, g_integrated, 0 );

// Направление луча через точку экрана uv главного вида: z вида = 1 (длина — во сколько раз путь длиннее глубины)
float3 fogRay( float2 uv )
{
	const float2 ndc = float2( uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f );
	const float3 viewDirection = float3( ndc.x / cb_projectionMatrix[0][0], ndc.y / cb_projectionMatrix[1][1], 1.0f );
	return mul( viewDirection, (float3x3)cb_viewInverseMatrix );
}

// Точка мира в сетке: xy — ячейка экрана, z — слой (дробные — внутри ячейки)
float3 froxelPosition( float3 grid )
{
	return cb_cameraPosition + fogRay( grid.xy / g_gridSize.xy ) * fogSliceDepth( grid.z );
}

// Свет, рассеянный в точке к камере единицей рассеяния: небо со всех сторон, солнце с тенью, лампы без теней
float3 froxelLight( float3 position )
{
	const float3 toCamera = normalize( cb_cameraPosition - position );
	const float g = cb_fogLayer1.w;
	float3 light = ambientAverageRadiance();
	[loop] for( int i = 0; i < (int)cb_lightCount; ++i )
	{
		const Light source = g_lights[i];
		if( source.type == lightDirectional )
		{
			float attenuation = henyeyGreenstein( dot( source.direction, toCamera ), g );
			if( i == g_shadowSunIndex )
				attenuation *= volumeShadow( position, -source.direction );
			light += source.color * attenuation;
		}
		else
		{
			const float3 offset = position - source.position;
			const float distanceSq = dot( offset, offset );
			const float3 fromLight = offset * rsqrt( max( distanceSq, 1e-8f ) );
			float attenuation = distanceAttenuation( distanceSq, source.attenuationRadius );
			if( source.type == lightSpot )
				attenuation *= spotAttenuation( -fromLight, source );
			light += source.color * attenuation * henyeyGreenstein( dot( fromLight, toCamera ), g );
		}
	}
	return light;
}

[numthreads( 4, 4, 4 )]
void mainLight( uint3 id : SV_DispatchThreadID )
{
	if( any( id >= g_gridSize ) )
		return;

	const float3 position = froxelPosition( id + g_jitter );
	const float density = fogDensity( position.y );
	float3 scattering = 0.0f;
	[branch] if( density > 0.0f )
		scattering = density * cb_fogAlbedo * froxelLight( position );
	float4 result = float4( scattering * g_inverseScale, density );

	// Прошлый кадр — по центру ячейки, перенесённому в сетку прошлого вида; ушедшее за край сетки — без истории
	[branch] if( g_historyWeight > 0.0f )
	{
		const float4 clip = mul( float4( froxelPosition( id + 0.5f ), 1.0f ), g_previousViewProjection );
		if( clip.w > 0.0f )
		{
			const float3 uvw = float3( clip.xy / clip.w * float2( 0.5f, -0.5f ) + 0.5f, fogSliceOf( clip.w ) / VOLUMETRIC_FOG_DEPTH );
			if( all( uvw >= 0.0f ) && all( uvw <= 1.0f ) )
			{
				float4 history = g_history.SampleLevel( g_SamplerLinearClamp, uvw, 0.0f );
				history.rgb *= g_historyScale;
				result = lerp( result, history, g_historyWeight );
			}
		}
	}
	g_lightingOut[id] = result;
}

[numthreads( 8, 8, 1 )]
void mainIntegrate( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_gridSize.xy ) )
		return;

	// Путь вдоль луча длиннее глубины взгляда во столько раз
	const float rayScale = length( fogRay( ( id.xy + 0.5f ) / g_gridSize.xy ) );
	float3 light = 0.0f;
	float transmittance = 1.0f;
	float start = fogSliceDepth( 0.0f );
	[loop] for( uint slice = 0; slice < g_gridSize.z; ++slice )
	{
		const float end = fogSliceDepth( slice + 1.0f );
		const float4 froxel = g_lighting[uint3( id.xy, slice )];
		const float extinction = max( froxel.a, 1e-7f );
		const float stepTransmittance = exp( -extinction * ( end - start ) * rayScale );
		// Свет слоя, проинтегрированный с ослаблением внутри него (Hillaire 2015): не зависит от толщины слоя
		light += transmittance * froxel.rgb * ( 1.0f - stepTransmittance ) / extinction;
		transmittance *= stepTransmittance;
		g_integrated[uint3( id.xy, slice )] = float4( light, transmittance );
		start = end;
	}
}
