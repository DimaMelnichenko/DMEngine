////////////////////////////////////////////////////////////////////////////////
// Источники света кадра (DMLightDriver): структурный буфер t100 и затухание точечных источников и прожекторов —
// общее для освещения поверхностей (Shaders/lighting.sh) и света в тумане (Shaders/volumetric_fog.cs)
////////////////////////////////////////////////////////////////////////////////

#ifndef LIGHTS_SH
#define LIGHTS_SH

#include "slots.h"
#include "bindless.sh"

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

DM_SRV( StructuredBuffer<Light>, g_lights, SLOT_LIGHTS );

static const int lightDirectional = 0;
static const int lightPoint = 1;
static const int lightSpot = 2;

// Затухание по расстоянию: обратный квадрат с плавным обрезанием к радиусу, чтобы свет заканчивался на границе
// (B. Karis, «Real Shading in Unreal Engine 4», 2013 — так же в UE)
float distanceAttenuation( float distanceSq, float radius )
{
	// Обратный квадрат с ограничением в 1 см, как в UE: сила света в канделах даёт освещённость в люксах
	float falloff = 1.0f / max( distanceSq, 1e-4f );
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

#endif
