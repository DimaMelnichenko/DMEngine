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
	float  g_specularFactor;    // сила отражения диэлектрика (specularFactor KHR_materials_specular): F0 = 0,04 × множитель
	// Пропускание: какая часть прошедшего света рассеивается вперёд пиком (ширина пика — g_transmissionForwardRoughness)
	float  g_transmissionForward;
	// Пропускание (KHR_materials_diffuse_transmission): цвет прошедшего света и доля рассеянного, проходящая насквозь
	float3 g_diffuseTransmissionColorFactor;
	float  g_diffuseTransmissionFactor;
	// Ветер дерева (материал PBRTree, WIND_TREE — Shaders/wind.sh): углы — радианы при силе ветра 1, частоты — Гц
	float  g_windGlobalAngle;		// наклон дерева у верхушки
	float  g_windGlobalExponent;	// рост наклона с долей высоты
	float  g_windGlobalFrequency;
	float  g_windBranchAngle;		// ветви первого уровня
	float  g_windBranchFrequency;
	float  g_windTwigAngle;			// веточки (второй уровень)
	float  g_windTwigFrequency;
	float  g_windRippleAmplitude;	// рябь листвы вдоль нормали, м
	float  g_windRippleFrequency;
	float  g_transmissionForwardRoughness;	// ширина пика пропускания вперёд — шероховатость GGX «на глаз»
	// Следы в траве (Shaders/foliage_interaction.sh): насколько растение ложится по следу (× «Max angle» поля; 0 — не
	// гнётся) и длина изгибающейся от корня части, м модели — выше она лежит прямо
	float  g_trampleWeight;
	float  g_trampleBendLength;
};

#endif
