////////////////////////////////////////////////////////////////////////////////
// Константы облаков (VolumetricCloud: тень, луч через слой; Shaders/volumetric_cloud.sh). Общие для C++ и HLSL —
// shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_VOLUMETRIC_CLOUD_CONSTANTS_H
#define DM_VOLUMETRIC_CLOUD_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX g_
DM_CONSTANTS( VolumetricCloudBuffer, b4 )
{
	DM_FIELD( matrix, previousViewProjection );	// главный вид прошлого кадра: перенос облаков по направлению
	DM_FIELD( float, layerBottom );		// высота основания слоя над землёй мира (y = 0), м
	DM_FIELD( float, layerTop );			// высота верха слоя, м
	DM_FIELD( float, coverage );			// общее покрытие 0…1
	DM_FIELD( float, cloudDensity );		// коэффициент ослабления облака полной плотности, 1/м
	DM_FIELD( float3, cloudAlbedo );		// доля рассеянного в ослабленном
	DM_FIELD( float, shapeScale );		// повторов шума формы на метр
	DM_FIELD( float2, windOffset );		// снос облаков ветром, м (x, z)
	DM_FIELD( float, detailScale );		// повторов мелкого шума на метр
	DM_FIELD( float, weatherScale );		// повторов карты погоды на метр
	DM_FIELD( float3, lightDirection );	// на светило облаков (солнце, ночью — луна)
	DM_FIELD( float, historyWeight );		// доля прошлого кадра; 0 — смена плана
	DM_FIELD( float3, lightColor );		// свет светила над атмосферой — в единицах запекания неба (на 1 лк солнца)
	DM_FIELD( float, maxDistance );		// дальше облака не трассируются, м
	DM_FIELD( float2, shadowOrigin );		// карта тени: начало по X и Z, м
	DM_FIELD( float, shadowSize );		// её размер, м
	DM_FIELD( float, shadowStrength );	// множитель оптической толщины тени: 0 — тени нет
	DM_FIELD( uint2, traceSize );			// размер текстуры облаков (половина кадра)
	DM_FIELD( uint, frameIndex );		// кадр с последней смены плана — сдвиг начала лучей
	DM_FIELD( float, outputScale );		// 1 / нормировка неба (SkyAtmosphere::skyNormalization): ночью луна в половинной точности
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
