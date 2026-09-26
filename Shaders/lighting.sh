////////////////////////////////////////////////////////////////////////////////
// Общее освещение поверхностей: материал заполняет Surface и зовёт evaluateLighting — прямой свет всех источников
// (Cook-Torrance GGX, Shaders/brdf.sh) и освещение окружением от неба (Shaders/ibl.sh). Новая составляющая
// освещения (тени, туман) добавляется здесь, а не в каждый материал. Используют PBRLit.ps и terrain.ps
////////////////////////////////////////////////////////////////////////////////

#ifndef LIGHTING_SH
#define LIGHTING_SH

#include "slots.h"
#include "common.vs"
#include "samplers.sh"
#include "brdf.sh"
#include "ibl.sh"

// Источник света, раскладка — DMLightDriver::LightBuffer
struct Light
{
	float3 position;			// точечный и прожектор
	int    type;				// lightDirectional, lightPoint, lightSpot
	float3 direction;			// направленный и прожектор: куда идёт свет, нормированное
	float  attenuationRadius;	// точечный и прожектор: на нём свет спадает до нуля, м; 0 — без обрезания
	float3 color;				// яркость (цвет × интенсивность)
	float  cosOuterCone;	// прожектор: косинус половины внешнего угла конуса — за ним света нет
	float  cosInnerCone;	// прожектор: косинус половины внутреннего угла — внутри полная яркость
	float3 padding;
};

StructuredBuffer<Light> g_lights : register( SLOT_LIGHTS );

static const int lightDirectional = 0;
static const int lightPoint = 1;
static const int lightSpot = 2;

// Минимальная шероховатость: при меньшей блик GGX от точечного источника вырождается в точку
static const float minRoughness = 0.045f;

// Всё, что материал знает о поверхности в точке
struct Surface
{
	float3 position;	// мир
	float3 normal;		// мир, нормированная
	float3 baseColor;	// линейный
	float  metallic;
	float  roughness;	// «на глаз» (perceptual), 0…1
	float  occlusion;	// затенение освещения окружением, 0…1
	float3 emissive;
};

// Затухание по расстоянию: обратный квадрат с плавным обрезанием к радиусу, чтобы свет заканчивался на границе
// (B. Karis, «Real Shading in Unreal Engine 4», 2013 — так же в UE)
float distanceAttenuation( float distanceSq, float radius )
{
	float falloff = 1.0f / ( distanceSq + 1.0f );
	if( radius <= 0.0f )
		return falloff;
	float ratio = distanceSq / ( radius * radius );
	float window = saturate( 1.0f - ratio * ratio );
	return falloff * window * window;
}

// Конус прожектора: полная яркость внутри внутреннего угла, плавный спад до нуля к внешнему
float spotAttenuation( float3 toLight, Light light )
{
	float cosAngle = dot( -toLight, light.direction );
	float t = saturate( ( cosAngle - light.cosOuterCone ) / max( light.cosInnerCone - light.cosOuterCone, 1e-4f ) );
	return t * t;
}

float3 evaluateDirectLighting( Surface surface, float3 view, float3 F0, float3 diffuseColor, float roughness )
{
	float3 result = 0.0f;
	[loop] for( int i = 0; i < (int)cb_lightCount; ++i )
	{
		Light light = g_lights[i];

		float3 toLight;
		float attenuation = 1.0f;
		if( light.type == lightDirectional )
		{
			toLight = -light.direction;
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
			result += CookTorrance_GGX( surface.normal, toLight, view, F0, roughness, diffuseColor ) * light.color * attenuation;
	}
	return result;
}

// Полное освещение точки: прямой свет, освещение окружением (рассеянное и отражённое, split-sum) и свечение.
// Линейный HDR без экспозиции — её применяет постобработка
float3 evaluateLighting( Surface surface )
{
	const float3 view = normalize( cb_cameraPosition - surface.position );
	const float roughness = clamp( surface.roughness, minRoughness, 1.0f );
	const float NV = saturate( dot( surface.normal, view ) );

	// Металл отражает своим цветом и не рассеивает, диэлектрик отражает ~4 % и рассеивает цвет основы
	const float3 F0 = lerp( 0.04f, surface.baseColor, surface.metallic );
	const float3 diffuseColor = surface.baseColor * ( 1.0f - surface.metallic );

	float3 direct = evaluateDirectLighting( surface, view, F0, diffuseColor, roughness );

	float2 environment = environmentBRDF( NV, roughness );
	float3 ambient = diffuseColor * ambientIrradiance( surface.normal ) +
					 ambientSpecular( reflect( -view, surface.normal ), roughness ) * ( F0 * environment.x + environment.y );

	return direct + ambient * surface.occlusion + surface.emissive;
}

#endif
