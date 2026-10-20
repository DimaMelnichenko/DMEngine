////////////////////////////////////////////////////////////////////////////////
// Плотность леса дальних гор (FarTerrain::bakeForestDensity) — на сетке карты гор, теми же правилами, что тон леса в их
// материале (farRuleWeights, far_terrain.sh: ниже границы леса на некрутых склонах без снега, скалы и льда, пятнами). По ней
// слой расстановки far_forest (деревья на горах вокруг камеры, scatter.cs, far_ground.sh) ставит деревья
////////////////////////////////////////////////////////////////////////////////

#include "far_terrain.sh"

DM_UAV( RWTexture2D<float>, g_farForestDensity, 0 );

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	uint width, height;
	g_farHeightMap.GetDimensions( width, height );
	if( any( id.xy >= uint2( width, height ) ) )
		return;
	// Центр текселя карты гор (строки — с севера)
	const float cell = g_farExtent / float( width );
	const float2 worldXZ = g_farOrigin + float2( id.x + 0.5f, float( height - id.y ) - 0.5f ) * cell;
	const float3 position = float3( worldXZ.x, farFineHeight( worldXZ ), worldXZ.y );
	const float3 normal = farNormal( worldXZ, cell );
	// Лёд — мип 0 (farGlacier берёт мип по пикселю — в compute его нет)
	const float4 ice = g_farGlacier.SampleLevel( g_SamplerLinearClamp, farHeightMapUV( worldXZ ), 0.0f );
	FarGlacier glacier;
	glacier.thickness = ice.r * 255.0f;
	glacier.debris = ice.g;
	glacier.downhill = ice.ba * 2.0f - 1.0f;
	float weights[maxLayers];
	float forest;
	float bareIce;
	float glacierShare;
	farRuleWeights( position, normal, glacier, weights, forest, bareIce, glacierShare );
	g_farForestDensity[id.xy] = saturate( forest );
}
