////////////////////////////////////////////////////////////////////////////////
// Переход модели в землю (как Runtime Virtual Texture blending в UE: камень, пень, корни «врастают» в рельеф без шва). Ниже
// TerrainBlendHeight над землёй поверхность модели переходит в материал террейна в этой же точке — те же слои, веса,
// проекции, смешивание по высоте и пятна яркости, что у terrain.ps (terrain_material.sh), нормаль — к нормали земли. Край —
// рваный по высотам текстур земли (TerrainBlendContrast). Вариант TERRAIN_BLEND материала PBR (PBRLit.ps); читать —
// только у уровня с террейном (cb_terrain.x > 0)
////////////////////////////////////////////////////////////////////////////////

#ifndef TERRAIN_BLEND_SH
#define TERRAIN_BLEND_SH

#include "terrain_detail.sh"
#include "terrain_material.sh"

DM_SRV( Texture2D<float>, g_terrainSceneHeight, SLOT_TERRAIN_HEIGHT );

// Высота земли в точке, м: детальная плитка у русла или итоговая карта (как terrainHeight расстановки)
float sceneTerrainHeight( float2 worldXZ )
{
	const TerrainLayerConstants layers = terrainLayers();
	float detail;
	const float normalized = sampleTerrainDetail( worldXZ, layers.detailTile, detail ) ? detail :
							 g_terrainSceneHeight.SampleLevel( g_SamplerLinearClamp, terrainLayerUV( worldXZ ), 0.0f );
	return normalized * layers.heightMultiplier + layers.heightOffset;
}

// Нормаль земли — разностями через offset, м
float3 sceneTerrainNormal( float2 worldXZ, float offset )
{
	const float left = sceneTerrainHeight( worldXZ - float2( offset, 0.0f ) );
	const float right = sceneTerrainHeight( worldXZ + float2( offset, 0.0f ) );
	const float back = sceneTerrainHeight( worldXZ - float2( 0.0f, offset ) );
	const float front = sceneTerrainHeight( worldXZ + float2( 0.0f, offset ) );
	return normalize( float3( left - right, 2.0f * offset, back - front ) );
}

// Поверхность модели в полосе у земли — к материалу террейна. height — полоса над землёй, м; contrast — насколько высоты
// текстур земли рвут край (0 — ровная полоса). Производные позиции — снаружи ветвлений (SampleGrad внутри)
void blendWithTerrain( inout Surface surface, float3 positionDX, float3 positionDY, float height, float contrast )
{
	const float3 position = surface.position;
	const float above = position.y - sceneTerrainHeight( position.xz );
	[branch] if( above > height * ( 1.0f + contrast ) )
		return;
	const float3 normal = sceneTerrainNormal( position.xz, 0.5f );
	float weights[maxLayers];
	terrainLayerWeightsAll( position.xz, weights );
	const TerrainLayerMix mixed = mixTerrainLayers( weights, farTextureShare( position ), 1.0f, position, positionDX, positionDY,
													projectionWeights( normal ) );
	// Доля земли: 1 у самой земли (и под ней), к height — 0; высоты текстур поднимают и опускают край
	const float share = saturate( 1.0f - above / max( height, 1e-3f ) + ( mixed.height - 0.5f ) * 2.0f * contrast );
	[branch] if( share <= 0.0f )
		return;
	surface.baseColor = lerp( surface.baseColor, mixed.albedo * terrainBrightness( position.xz ), share );
	surface.normal = normalize( lerp( surface.normal, normalize( normal + mixed.detail ), share ) );
	surface.metallic = lerp( surface.metallic, 0.0f, share );
	surface.specular = lerp( surface.specular, 1.0f, share );
	surface.roughness = lerp( surface.roughness, mixed.roughness, share );
	surface.occlusion = lerp( surface.occlusion, saturate( mixed.occlusion ), share );
	surface.microShadows = lerp( surface.microShadows, terrainLayers().microShadows, share );
	surface.transmission = lerp( surface.transmission, 0.0f, share );
}

#endif
