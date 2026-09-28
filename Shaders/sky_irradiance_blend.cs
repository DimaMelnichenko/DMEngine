////////////////////////////////////////////////////////////////////////////////
// Переход рассеянного света окружения от прежних гармоник SkyLight к новым: 9 коэффициентов, смесь с долями g_previousWeight и g_currentWeight
// (пара к Shaders/sky_light_blend.ps). Класс SkyLight
////////////////////////////////////////////////////////////////////////////////

// Раскладка — SkyLight::BlendParameters, та же, что у Shaders/sky_light_blend.ps (грань и мип здесь не нужны)
cbuffer IrradianceBlendParameters : register( b4 )
{
	int   g_face;
	float g_mip;
	float g_previousWeight;	// доля прежних гармоник — с поправкой на их нормировку
	float g_currentWeight;	// доля новых
};

StructuredBuffer<float4> g_previous : register( t0 );
StructuredBuffer<float4> g_current : register( t1 );
RWStructuredBuffer<float4> g_irradianceSH : register( u0 );

[numthreads( 9, 1, 1 )]
void main( uint coefficient : SV_GroupIndex )
{
	g_irradianceSH[coefficient] = g_previous[coefficient] * g_previousWeight + g_current[coefficient] * g_currentWeight;
}
