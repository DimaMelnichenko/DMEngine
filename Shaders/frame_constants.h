////////////////////////////////////////////////////////////////////////////////
// Константы кадра и вида (ConstantBuffers::setFrameBuffer / setViewBuffer) и объекта (setPerObjectBuffer) — всем
// шейдерам через Shaders/common.vs. Общие для C++ и HLSL — shader_constants.h
////////////////////////////////////////////////////////////////////////////////

#ifndef DM_FRAME_CONSTANTS_H
#define DM_FRAME_CONSTANTS_H

#include "shader_constants.h"
#include "slots.h"

#define DM_FIELD_PREFIX cb_
DM_CONSTANTS( FrameConstantBuffer, SLOT_CB_FRAME )
{
	DM_FIELD( matrix, viewMatrix );
	DM_FIELD( matrix, viewInverseMatrix );
	DM_FIELD( matrix, projectionMatrix );
	DM_FIELD( matrix, viewProjectionMatrix );
	DM_FIELD( float3, cameraPosition );
	DM_FIELD( float, gameTime );		// время игры, с — шагами кадра (с timestep — фиксированными), как View.GameTime в UE
	DM_FIELD( float3, viewDirection );
	DM_FIELD( float, deltaTime );	// длительность кадра, с
	DM_FIELD( float, lightCount );
	DM_FIELD( float3, lodOrigin );	// откуда считаются LOD и морфинг: у видов теней — позиция главной камеры
	DM_FIELD( float, skyLightScale );	// масштаб освещения окружением (SkyLight): освещённость от солнца, лк (атмосфера запечена для 1 лк), × нормировка результата, или интенсивность панорамы
	DM_FIELD( float, aerialPerspectiveDistance );	// до какого расстояния от камеры, м, идут слои воздушной перспективы — дальняя плоскость главного вида
	DM_FIELD( float, aerialPerspectiveScale );	// множитель объёма воздушной перспективы: cb_skyScale × нормировка объёма
	DM_FIELD( float, skyScale );			// масштаб фона неба: освещённость от солнца, лк, или интенсивность панорамы
	// Ветер уровня (Wind, Shaders/wind.sh); cb_windStrength 0 — растения неподвижны
	DM_FIELD( float2, windDirection );	// куда дует, нормированное (x, z)
	DM_FIELD( float, windStrength );
	DM_FIELD( float, windSpeed );		// скорость волн порывов, м/с
	DM_FIELD( float, windGustMin );		// порыв между волнами и на гребне — доли силы
	DM_FIELD( float, windGustMax );
	DM_FIELD( float, windGustSize );		// длина волны порыва, м
	DM_FIELD( float, windFieldScale );	// поле ветра по рельефу (wind_field.sh): 1 / сторона карты, 1/м; 0 — поля нет, ветер ровный
	// Туман по высоте (Shaders/height_fog.sh, VolumetricFog). Слой: плотность — коэффициент ослабления, 1/м; высота, ниже
	// которой она ровная, м; спад выше неё, 1/м
	DM_FIELD( float4, fogLayer0 );		// w — расстояние до фона неба, м (дальняя плоскость вида)
	DM_FIELD( float4, fogLayer1 );		// w — анизотропия рассеяния g (Scattering Distribution в UE)
	DM_FIELD( float3, fogAlbedo );
	DM_FIELD( float, fogScale );			// множитель объёма тумана (свет в нём делён на него); 0 — тумана нет
	DM_FIELD( float3, fogGridZ );			// слои объёма по глубине взгляда: слой = log₂(z · B + O) · S, (B, O, S)
	DM_FIELD( float, fogVolumeDistance );	// дальность объёма по глубине взгляда, м; 0 — только туман по формуле
	// Тень облаков (Shaders/cloud_shadow.sh, VolumetricCloud): начало карты по X и Z мира, м; 1 / её размер, 1/м (0 —
	// облаков нет); высота плоскости карты — середина облачного слоя, м
	DM_FIELD( float4, cloudShadow );
	// Детальная вода у русел (water.sh, WaterSimulation): x — размер мира, м; y — сторона плитки, м (0 — её нет)
	DM_FIELD( float4, waterDetail );
	// Следы в траве (foliage_interaction.sh, FoliageInteraction): xy — начало окна поля по X и Z мира, м; z — 1 / его сторона,
	// 1/м (0 — поля нет); w — наклон полностью придавленного растения, рад
	DM_FIELD( float4, foliageInteraction );
	// Экранное затенение окружением (ambient_occlusion.sh, AmbientOcclusion): x — сила (0 — затенения нет), yz — размер
	// текстуры в текселях (половина кадра)
	DM_FIELD( float4, ambientOcclusion );
	// Карты отражений места (reflection_capture.sh, ReflectionCaptures) — две ближайшие к камере: xyz — точка съёмки, м мира;
	// w — радиус влияния, м (0 — карты нет)
	DM_ARRAY( float4, reflectionCaptureSphere, 2 );
	// x — множитель выборки до кд/м²: Brightness / экспозиция съёмки; y — 1 / ширина полосы перехода в небо у края радиуса, 1/м
	DM_ARRAY( float4, reflectionCapture, 2 );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#define DM_FIELD_PREFIX cb_
DM_CONSTANTS( WorldBuffer, SLOT_CB_OBJECT )
{
	DM_FIELD( matrix, worldMatrix );
	DM_FIELD( matrix, worldInverseTransposeMatrix );	// для нормалей: перпендикулярны поверхности и при неравномерном масштабе
	DM_FIELD( float, lodDither );		// смена LOD дизерингом у модели в полосе перехода (Shaders/lod_dither.sh), 0 — вне её
	DM_FIELD( float3, objectPadding );
};
DM_CONSTANTS_END
#undef DM_FIELD_PREFIX

#endif
