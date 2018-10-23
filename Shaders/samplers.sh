////////////////////
//	SAMPLERS

#ifndef SAMPLERS_H
#define SAMPLERS_H
SamplerState g_SamplerPointClamp : register(s0);
SamplerState g_SamplerPointWrap : register(s1);
SamplerState g_SamplerPointBorder : register(s2);
SamplerState g_SamplerLinearClamp : register(s3);
SamplerState g_SamplerLinearWrap : register(s4);
SamplerState g_SamplerLinearBorder : register(s5);
SamplerState g_SamplerAnisotropicClamp : register(s6);
SamplerState g_SamplerAnisotropicWrap : register(s7);

#endif
/////////////////////