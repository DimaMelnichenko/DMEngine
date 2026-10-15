////////////////////
//	SAMPLERS

#ifndef SAMPLERS_H
#define SAMPLERS_H

// Общие сэмплеры — статические в root signature (DMD3D::createRootSignature): слоты — slots.h, у карты теней — shadows.sh
#include "slots.h"

SamplerState g_SamplerPointClamp : register( SLOT_SAMPLER_POINT_CLAMP );
SamplerState g_SamplerPointWrap : register( SLOT_SAMPLER_POINT_WRAP );
SamplerState g_SamplerPointBorder : register( SLOT_SAMPLER_POINT_BORDER );
SamplerState g_SamplerLinearClamp : register( SLOT_SAMPLER_LINEAR_CLAMP );
SamplerState g_SamplerLinearWrap : register( SLOT_SAMPLER_LINEAR_WRAP );
SamplerState g_SamplerLinearBorder : register( SLOT_SAMPLER_LINEAR_BORDER );
SamplerState g_SamplerAnisotropicClamp : register( SLOT_SAMPLER_ANISOTROPIC_CLAMP );
SamplerState g_SamplerAnisotropicWrap : register( SLOT_SAMPLER_ANISOTROPIC_WRAP );

#endif
