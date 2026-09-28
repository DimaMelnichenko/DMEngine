////////////////////////////////////////////////////////////////////////////////
// Переход рассеянного света окружения от прежних гармоник SkyLight к новым: 9 коэффициентов, смесь с долей g_blend
// (пара к Shaders/sky_light_blend.ps). Класс SkyLight
////////////////////////////////////////////////////////////////////////////////

// Раскладка — SkyLight::BlendParameters, та же, что у Shaders/sky_light_blend.ps (грань и мип здесь не нужны)
cbuffer IrradianceBlendParameters : register( b4 )
{
	int   g_face;
	float g_mip;
	float g_blend;		// 0 — прежние гармоники, 1 — новые
	float g_irradianceBlendPadding;
};

StructuredBuffer<float4> g_previous : register( t0 );
StructuredBuffer<float4> g_current : register( t1 );
RWStructuredBuffer<float4> g_irradianceSH : register( u0 );

[numthreads( 9, 1, 1 )]
void main( uint coefficient : SV_GroupIndex )
{
	g_irradianceSH[coefficient] = lerp( g_previous[coefficient], g_current[coefficient], g_blend );
}
