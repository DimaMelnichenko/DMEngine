////////////////////////////////////////////////////////////////////////////////
// Константы материала PBR (PBRMaterial::setParams): пиксельному шейдеру (Shaders/PBRLit.ps) и вершинному
// (Shaders/LightShader.vs — отклик на ветер)
////////////////////////////////////////////////////////////////////////////////

#ifndef PBR_MATERIAL_SH
#define PBR_MATERIAL_SH

#include "slots.h"

// Раскладка — PBRMaterial::PSParam
cbuffer PBRMaterialBuffer : register( SLOT_CB_MATERIAL )
{
	float4 g_baseColorFactor;
	float3 g_emissiveFactor;
	float  g_metallic;
	float  g_roughness;
	float  g_normalScale;
	float  g_normalGreenUp;     // 1 — зелёный канал карты нормалей смотрит вверх по картинке (OpenGL, glTF)
	float  g_occlusionStrength;
	float  g_alphaCutoff;       // AlphaMode MASK: пиксели с альфой ниже порога отбрасываются
	float  g_windWeight;        // отклик на ветер уровня (Wind Weight у SimpleGrassWind в UE): 0 — неподвижен
	float2 g_materialPadding;
};

#endif
