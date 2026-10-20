////////////////////////////////////////////////////////////////////////////////
// Полог — где земля под кронами деревьев (TerrainCanopy: круги крон леса и высоких моделей уровня, TerrainPipeline::setCanopy;
// docs/scatter.md, «Полог»). Ресурс сцены SLOT_CANOPY, только у уровня с террейном
////////////////////////////////////////////////////////////////////////////////

#ifndef CANOPY_SH
#define CANOPY_SH

#include "slots.h"
#include "samplers.sh"
#include "bindless.sh"

// R — покрытие всеми кронами, G — лиственными (TerrainCanopy::buildWithBroadleaf)
DM_SRV( Texture2D<float2>, g_canopy, SLOT_CANOPY );

// Покрытие кронами 0…1 в точке; uv — карты высот (строка 0 — дальний край по z)
float canopyCover( float2 uv )
{
	return g_canopy.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).r;
}

// Покрытие кронами (x) и лиственными кронами (y)
float2 canopyCoverAndBroadleaf( float2 uv )
{
	return g_canopy.SampleLevel( g_SamplerLinearClamp, uv, 0.0f );
}

#endif
