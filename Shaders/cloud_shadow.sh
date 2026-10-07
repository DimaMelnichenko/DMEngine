////////////////////////////////////////////////////////////////////////////////
// Тень облаков: пропускание облачного слоя к источнику теней (солнцу, ночью — луне). Карту считает VolumetricCloud
// (Shaders/volumetric_cloud.cs, mainShadow) на плоскости в середине слоя вокруг камеры; точка мира смотрит в неё вдоль
// луча к свету. Множитель прямого света — в sunShadow и volumeShadow (Shaders/shadows.sh): его получают поверхности,
// туман, частицы и вода
////////////////////////////////////////////////////////////////////////////////

#ifndef CLOUD_SHADOW_SH
#define CLOUD_SHADOW_SH

#include "slots.h"
#include "common.vs"
#include "samplers.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_cloudShadow, SLOT_CLOUD_SHADOW );

// toLight — направление на источник теней, нормированное
float cloudShadow( float3 position, float3 toLight )
{
	// Облаков нет у уровня без строки VolumetricCloud; источник у горизонта — карта его не описывает
	[branch] if( cb_cloudShadow.z <= 0.0f || toLight.y <= 0.0f )
		return 1.0f;
	const float2 plane = position.xz + toLight.xz * ( ( cb_cloudShadow.w - position.y ) / max( toLight.y, 0.05f ) );
	const float2 uv = ( plane - cb_cloudShadow.xy ) * cb_cloudShadow.z;
	// К краю карты тень уходит: за ней облака не считались
	const float2 edge = saturate( min( uv, 1.0f - uv ) * 10.0f );
	return lerp( 1.0f, g_cloudShadow.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ), edge.x * edge.y );
}

#endif
