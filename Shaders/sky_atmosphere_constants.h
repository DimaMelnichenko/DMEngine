////////////////////////////////////////////////////////////////////////////////
// Константы проходов неба (SkyAtmosphere: таблицы, Sky-View, cubemap освещения, фон, воздушная перспектива),
// Shaders/atmosphere.sh. Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_SKY_ATMOSPHERE_CONSTANTS_H
#define DM_SKY_ATMOSPHERE_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

// умножают при выборке — cb_skyLightScale), поэтому луна и ночное свечение здесь — в тех же единицах: их яркость,
// делённая на освещённость от солнца (~10⁻⁶ и ~10⁻⁹; таблицы неба — во float32)
#define DM_FIELD_PREFIX g_
DM_CONSTANTS( SkyParameters, SLOT_CB_PASS )
{
	DM_FIELD( float3, sunDirection );	// направление на солнце
	DM_FIELD( float, skyIntensity );	// множитель рассеянного света (1 — по модели)
	DM_FIELD( float3, sunColor );		// цветность солнца над атмосферой (яркость 1 — небо запекается для солнца 1 лк)
	DM_FIELD( float, haze );			// множитель плотности аэрозоля (Ми): больше — дымка у горизонта и ореол вокруг солнца
	DM_FIELD( float3, groundAlbedo );
	DM_FIELD( int, face );			// грань cubemap неба: 0…5 — +X, −X, +Y, −Y, +Z, −Z (cubeDirection)
	DM_FIELD( float3, moonDirection );	// направление на луну — второе светило атмосферы (Atmosphere Sun Light Index 1)
	DM_FIELD( float, nightSkyLuminance );	// свечение ночного неба в зените
	DM_FIELD( float3, moonColor );		// свет луны над атмосферой; 0 — луны нет
	DM_FIELD( int, skyViewLight );	// какую таблицу Sky-View считает проход: 0 — солнца, 1 — луны
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
