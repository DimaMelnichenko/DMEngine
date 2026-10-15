////////////////////////////////////////////////////////////////////////////////
// Переход рассеянного света окружения от прежних гармоник SkyLight к новым: 9 коэффициентов, смесь с долями g_previousWeight и g_currentWeight
// (пара к Shaders/sky_light_blend.ps). Класс SkyLight
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"
// Константы — те же, что у Shaders/sky_light_blend.ps (грань и мип здесь не нужны)
#include "sky_light_blend_constants.h"

DM_SRV( StructuredBuffer<float4>, g_previous, 0 );
DM_SRV( StructuredBuffer<float4>, g_current, 1 );
DM_UAV( RWStructuredBuffer<float4>, g_irradianceSH, 0 );

[numthreads( 9, 1, 1 )]
void main( uint coefficient : SV_GroupIndex )
{
	g_irradianceSH[coefficient] = g_previous[coefficient] * g_previousWeight + g_current[coefficient] * g_currentWeight;
}
