////////////////////////////////////////////////////////////////////////////////
// Общее освещение поверхностей: материал заполняет Surface и зовёт evaluateLighting — прямой свет всех источников
// (Cook-Torrance GGX, Shaders/brdf.sh) и освещение окружением от неба (Shaders/ibl.sh). Новая составляющая
// освещения (тени, туман) добавляется здесь, а не в каждый материал: так тень солнца (Shaders/shadows.sh) получают
// все материалы с освещением. Используют PBRLit.ps и terrain.ps
////////////////////////////////////////////////////////////////////////////////

#ifndef LIGHTING_SH
#define LIGHTING_SH

#include "slots.h"
#include "common.vs"
#include "samplers.sh"
#include "brdf.sh"
#include "ibl.sh"
#include "shadows.sh"
#include "lights.sh"
#include "height_fog.sh"
#include "exposure.sh"
#include "bindless.sh"

// Минимальная шероховатость: при меньшей блик GGX от точечного источника вырождается в точку
static const float minRoughness = 0.045f;

// Всё, что материал знает о поверхности в точке
struct Surface
{
	float3 position;	// мир
	float3 normal;		// мир, нормированная
	float3 geometricNormal;	// нормаль геометрии без карты нормалей (для смещения при выборке тени), нормированная
	float3 baseColor;	// линейный
	float  metallic;
	float  roughness;	// «на глаз» (perceptual), 0…1
	float  occlusion;	// затенение освещения окружением, 0…1
	float3 emissive;
	// Пропускание тонкой поверхностью — листья, травинки (KHR_materials_diffuse_transmission в glTF, Two Sided Foliage
	// в UE): доля рассеянного света, прошедшая насквозь по Ламберту, и её цвет; 0 — не пропускает
	float  transmission;
	float3 transmissionColor;	// линейный
};

// Нормаль, повёрнутая к свету: тонкая поверхность освещается с обеих сторон, и выборка тени смещается по нормали
// к источнику, а не от него — иначе травинка, светящаяся на просвет, затеняла бы сама себя. Непрозрачным телам это не
// меняет ничего: со стороны, обращённой от света, отражения и так нет
float3 normalTowardLight( float3 normal, float3 toLight )
{
	return dot( normal, toLight ) < 0.0f ? -normal : normal;
}

// diffuseColor — отражаемая доля рассеянного цвета, transmissionColor — пропускаемая (у непрозрачных 0)
float3 evaluateDirectLighting( Surface surface, float3 view, float3 F0, float3 diffuseColor, float3 transmissionColor,
							   float roughness )
{
	// Тень солнца — один раз до цикла источников
	float sunShadowFactor = 1.0f;
	[branch] if( g_shadowSunIndex >= 0 )
	{
		const float3 toSun = -g_lights[g_shadowSunIndex].direction;
		sunShadowFactor = sunShadow( surface.position, normalTowardLight( surface.geometricNormal, toSun ), toSun );
	}
	const bool transmits = any( transmissionColor > 0.0f );

	float3 result = 0.0f;
	[loop] for( int i = 0; i < (int)cb_lightCount; ++i )
	{
		Light light = g_lights[i];

		float3 toLight;
		float attenuation = 1.0f;
		if( light.type == lightDirectional )
		{
			toLight = -light.direction;
			if( i == g_shadowSunIndex )
				attenuation = sunShadowFactor;
		}
		else
		{
			float3 offset = light.position - surface.position;
			float distanceSq = dot( offset, offset );
			toLight = offset * rsqrt( max( distanceSq, 1e-8f ) );
			attenuation = distanceAttenuation( distanceSq, light.attenuationRadius );
			if( light.type == lightSpot )
				attenuation *= spotAttenuation( toLight, light );
		}

		[branch] if( attenuation > 0.0f )
		{
			result += CookTorrance_GGX( surface.normal, toLight, view, F0, roughness, diffuseColor ) * light.color * attenuation;
			// Свет с обратной стороны проходит насквозь по Ламберту (diffuse BTDF KHR_materials_diffuse_transmission)
			[branch] if( transmits )
				result += transmissionColor * ( saturate( -dot( surface.normal, toLight ) ) / PI ) * light.color * attenuation;
		}
	}
	return result;
}

// Полное освещение точки: прямой свет, освещение окружением (рассеянное и отражённое, split-sum) и свечение,
// затем туман и воздушная перспектива до камеры (applyFogging, Shaders/height_fog.sh). Линейный HDR с экспозицией
// прошлого кадра (pre-exposure) — новую применяет постобработка
float3 evaluateLighting( Surface surface )
{
	const float3 view = normalize( cb_cameraPosition - surface.position );
	const float roughness = clamp( surface.roughness, minRoughness, 1.0f );
	const float NV = saturate( dot( surface.normal, view ) );

	// Металл отражает своим цветом и не рассеивает, диэлектрик отражает ~4 % и рассеивает цвет основы. У тонкой
	// поверхности доля transmission рассеянного света не отражается, а проходит насквозь
	const float3 F0 = lerp( 0.04f, surface.baseColor, surface.metallic );
	const float transmission = saturate( surface.transmission ) * ( 1.0f - surface.metallic );
	const float3 diffuseColor = surface.baseColor * ( 1.0f - surface.metallic ) * ( 1.0f - transmission );
	const float3 transmissionColor = surface.transmissionColor * transmission;

	float3 direct = evaluateDirectLighting( surface, view, F0, diffuseColor, transmissionColor, roughness );

	float2 environment = environmentBRDF( NV, roughness );
	float3 ambient = diffuseColor * ambientIrradiance( surface.normal ) +
					 ambientSpecular( reflect( -view, surface.normal ), roughness ) * ( F0 * environment.x + environment.y );
	// Небо с обратной стороны тоже просвечивает (как Subsurface Color × гармоники по −N у Two Sided Foliage в UE)
	[branch] if( transmission > 0.0f )
		ambient += transmissionColor * ambientIrradiance( -surface.normal );

	// Освещение окружением тень не гасит (как в UE без затенения окружения)
	const float3 color = ( direct + ambient * surface.occlusion + surface.emissive ) * shadowCascadeTint( surface.position );
	// Яркость, кд/м², — в буфер сцены с экспозицией прошлого кадра (pre-exposure, Shaders/exposure.sh)
	return applyFogging( color, surface.position ) * preExposure();
}

#endif
