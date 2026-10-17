////////////////////////////////////////////////////////////////////////////////
// Карта отражений места — как Sphere Reflection Capture в UE: кубическая карта сцены, снятая из точки (ReflectionCaptures,
// съёмка сцены без неба — docs/scene_capture.md), с префильтром по шероховатости, как у освещения неба (мипы —
// specularMipCount). Альфа — покрытие сценой: цвет умножен на неё, где съёмка видела небо — 0. Внутри радиуса влияния
// отражения окружения — сцена из карты с поправкой параллакса (луч отражения продолжается до сферы радиуса влияния,
// выборка — по направлению из точки съёмки на место пересечения; Lagarde, Zanuttini 2012, «Local Image-based Lighting With
// Parallax-corrected Cubemap»), сквозь непокрытое — небо освещения окружением по направлению самого луча: оно бесконечно
// далеко, поправка параллакса для него неверна (как сочетание карт отражений со Sky Light в UE). Ресурс — t131, константы —
// cb_reflectionCaptureSphere / cb_reflectionCapture (ближайшая к камере карта кадра). docs/reflection_captures.md
////////////////////////////////////////////////////////////////////////////////

#ifndef REFLECTION_CAPTURE_SH
#define REFLECTION_CAPTURE_SH

#include "ibl.sh"

DM_SRV( TextureCube, g_reflectionCapture, SLOT_REFLECTION_CAPTURE );

// Отражённый свет окружения в направлении r из точки position, кд/м²: карта отражений места внутри её радиуса, у края —
// переход в небо (ambientSpecular), дальше — небо
float3 environmentSpecular( float3 position, float3 r, float roughness )
{
	const float3 sky = ambientSpecular( r, roughness );
	const float radius = cb_reflectionCaptureSphere.w;
	const float3 offset = position - cb_reflectionCaptureSphere.xyz;
	const float distance = length( offset );
	const float weight = radius > 0.0f ? saturate( ( radius - distance ) * cb_reflectionCapture.y ) : 0.0f;
	[branch] if( weight <= 0.0f )
		return sky;
	// Луч position + t·r до сферы: t² + 2t·(offset·r) + |offset|² − R² = 0; точка внутри сферы — корень впереди один
	const float b = dot( offset, r );
	const float c = dot( offset, offset ) - radius * radius;
	const float t = -b + sqrt( max( b * b - c, 0.0f ) );
	const float3 direction = offset + t * r;
	const float4 captured = g_reflectionCapture.SampleLevel( g_SamplerLinearClamp, direction, roughness * ( specularMipCount - 1.0f ) );
	const float3 local = captured.rgb * cb_reflectionCapture.x + sky * ( 1.0f - captured.a );
	return lerp( sky, local, weight );
}

#endif
