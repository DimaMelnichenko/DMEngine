////////////////////////////////////////////////////////////////////////////////
// Ночное небо фона (SkyAtmosphere, docs/sky.md, «Ночь»): константы диска луны и звёзд — общие для фона
// (Shaders/sky_background.ps) и звёзд каталога (Shaders/sky_stars.vs)
////////////////////////////////////////////////////////////////////////////////

#ifndef NIGHT_SKY_SH
#define NIGHT_SKY_SH

// Раскладка — SkyAtmosphere::NightSkyParameters. Яркости — в единицах запекания неба (на 1 лк солнца)
cbuffer NightSkyParameters : register( b4 )
{
	float3 g_equatorialX;			// экваториальный базис в мире: к точке весеннего равноденствия,
	float  g_moonAngularRadius;		// угловой радиус диска луны (увеличенный, как у солнца), рад
	float3 g_equatorialY;			// к прямому восхождению 90°,
	float  g_starIlluminanceScale;	// освещённость от звезды, лк → единицы запекания (1 / освещённость от солнца); 0 — звёзд нет
	float3 g_equatorialZ;			// к северному полюсу мира
	float  g_starTwinkle;			// сила мерцания звёзд, 1 — по модели, 0 — не мерцают
	float3 g_moonDiskLuminance;		// яркость диска полной луны (Ломмель — Зелигер: ровный диск), с цветом луны; 0 — луны нет
	float  g_moonDiskBrightnessLimit;	// предел яркости полного диска после экспозиции; 0 — физическая яркость
	float2 g_viewportSize;			// размер цели фона, пиксели
	float  g_milkyWayLuminance;		// средняя яркость карты Млечного Пути (её среднее по сфере — 1); 0 — его нет или день
	float  g_nightSkyPadding;
	// Базис тела луны в мире (либрация и позиционный угол оси, SunPosition::computeMoon): ось вращения, к нулевому
	// меридиану, к 90° восточной селенографической долготы (Море Кризисов)
	float3 g_moonNorth;
	float  g_earthshine;			// освещённость луны Землёй относительно солнечной (пепельный свет); 0 — нет
	float3 g_moonPrime;
	float  g_earthshineBrightnessLimit;	// предел яркости пепельного света после экспозиции; 0 — физическая
	float3 g_moonEast;
	float  g_nightSkyPadding2;
};

#endif
