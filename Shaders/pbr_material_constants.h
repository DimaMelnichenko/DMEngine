////////////////////////////////////////////////////////////////////////////////
// Константы материала PBR (PBRMaterial::setParams): пиксельному шейдеру (Shaders/PBRLit.ps) и вершинному
// (Shaders/LightShader.vs — отклик на ветер). Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_PBR_MATERIAL_CONSTANTS_H
#define DM_PBR_MATERIAL_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( PBRMaterialBuffer, SLOT_CB_MATERIAL )
{
	DM_FIELD( float4, baseColorFactor );
	DM_FIELD( float3, emissiveFactor );
	DM_FIELD( float, metallic );
	DM_FIELD( float, roughness );
	DM_FIELD( float, normalScale );
	DM_FIELD( float, normalGreenUp );     // 1 — зелёный канал карты нормалей смотрит вверх по картинке (OpenGL, glTF)
	DM_FIELD( float, occlusionStrength );
	DM_FIELD( float, alphaCutoff );       // AlphaMode MASK: пиксели с альфой ниже порога отбрасываются
	DM_FIELD( float, windWeight );        // отклик на ветер уровня (Wind Weight у SimpleGrassWind в UE): 0 — неподвижен
	DM_FIELD( float, specularFactor );    // сила отражения диэлектрика (specularFactor KHR_materials_specular): F0 = 0,04 × множитель
	// Пропускание: какая часть прошедшего света рассеивается вперёд пиком (ширина пика — g_transmissionForwardRoughness)
	DM_FIELD( float, transmissionForward );
	// Пропускание (KHR_materials_diffuse_transmission): цвет прошедшего света и доля рассеянного, проходящая насквозь
	DM_FIELD( float3, diffuseTransmissionColorFactor );
	DM_FIELD( float, diffuseTransmissionFactor );
	// Ветер дерева (материал PBRTree, WIND_TREE — Shaders/wind.sh): углы — радианы при силе ветра 1, частоты — Гц
	DM_FIELD( float, windGlobalAngle );		// наклон дерева у верхушки
	DM_FIELD( float, windGlobalExponent );	// рост наклона с долей высоты
	DM_FIELD( float, windGlobalFrequency );
	DM_FIELD( float, windBranchAngle );		// ветви первого уровня
	DM_FIELD( float, windBranchFrequency );
	DM_FIELD( float, windTwigAngle );			// веточки (второй уровень)
	DM_FIELD( float, windTwigFrequency );
	DM_FIELD( float, windRippleAmplitude );	// рябь листвы вдоль нормали, м
	DM_FIELD( float, windRippleFrequency );
	DM_FIELD( float, transmissionForwardRoughness );	// ширина пика пропускания вперёд — шероховатость GGX «на глаз»
	// Следы в траве (Shaders/foliage_interaction.sh): насколько растение ложится по следу (× «Max angle» поля; 0 — не
	// гнётся) и длина изгибающейся от корня части, м модели — выше она лежит прямо
	DM_FIELD( float, trampleWeight );
	DM_FIELD( float, trampleBendLength );
	// Нормаль кроны (LightShader.vs, crownNormal): доля нормали конуса кроны в нормали листвы (0 — своя плоскость карточки) и
	// подъём конуса — вверх на столько к единице наружу от оси ствола
	DM_FIELD( float, crownNormalBlend );
	DM_FIELD( float, crownNormalUp );
	// Переход в землю (terrain_blend.sh, вариант TERRAIN_BLEND): полоса над землёй, м (0 — нет), и насколько высоты текстур
	// земли рвут её край
	DM_FIELD( float, terrainBlendHeight );
	DM_FIELD( float, terrainBlendContrast );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
