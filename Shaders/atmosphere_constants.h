////////////////////////////////////////////////////////////////////////////////
// Параметры атмосферы Земли — общие для C++ и HLSL, как slots.h: небо считает шейдер (atmosphere.sh), а пропускание
// для света солнца у земли — SkyAtmosphere::sunTransmittance на CPU, и модель у них должна быть одна. Значения —
// по умолчанию Sky Atmosphere в UE (Hillaire 2020). Векторы — тремя числами через запятую: float3( X ) в HLSL,
// XMFLOAT3( X ) в C++
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_ATMOSPHERE_CONSTANTS_H
#define DM_ATMOSPHERE_CONSTANTS_H

#define ATMOSPHERE_PLANET_RADIUS		6360e3f		// м
#define ATMOSPHERE_TOP_RADIUS			6460e3f
#define ATMOSPHERE_OBSERVER_ALTITUDE	100.0f		// высота наблюдателя над уровнем моря: камера и сцена
#define ATMOSPHERE_RAYLEIGH_SCATTERING	5.802e-6f, 13.558e-6f, 33.1e-6f		// 1/м, на уровне моря
#define ATMOSPHERE_RAYLEIGH_SCALE_HEIGHT	8000.0f
#define ATMOSPHERE_MIE_SCATTERING		3.996e-6f
#define ATMOSPHERE_MIE_ABSORPTION		4.40e-6f
#define ATMOSPHERE_MIE_SCALE_HEIGHT		1200.0f
#define ATMOSPHERE_MIE_ANISOTROPY		0.8f
#define ATMOSPHERE_OZONE_ABSORPTION		0.650e-6f, 1.881e-6f, 0.085e-6f		// слой OZONE_CENTER ± OZONE_HALF_WIDTH
#define ATMOSPHERE_OZONE_CENTER			25e3f
#define ATMOSPHERE_OZONE_HALF_WIDTH		15e3f

#endif
