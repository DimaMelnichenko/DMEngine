
/////////////
// GLOBALS //
/////////////

#ifndef COMMON_VS
#define COMMON_VS

#include "slots.h"

cbuffer FrameConstantBuffer : register( SLOT_CB_FRAME )
{
    matrix cb_viewMatrix;
	matrix cb_viewInverseMatrix;
    matrix cb_projectionMatrix;
	matrix cb_viewProjectionMatrix;
	float3 cb_cameraPosition;
	float  cb_gameTime;		// время игры, с — шагами кадра (с timestep — фиксированными), как View.GameTime в UE
	float3 cb_viewDirection;
	float  cb_deltaTime;	// длительность кадра, с
	float  cb_lightCount;
	float3 cb_lodOrigin;	// откуда считаются LOD и морфинг: у видов теней — позиция главной камеры
	float  cb_skyLightScale;	// масштаб освещения окружением (SkyLight): освещённость от солнца, лк (атмосфера запечена для 1 лк), × нормировка результата, или интенсивность панорамы
	float  cb_aerialPerspectiveDistance;	// до какого расстояния от камеры, м, идут слои воздушной перспективы — дальняя плоскость главного вида
	float  cb_aerialPerspectiveScale;	// множитель объёма воздушной перспективы: cb_skyScale × нормировка объёма
	float  cb_skyScale;			// масштаб фона неба: освещённость от солнца, лк, или интенсивность панорамы
	// Ветер уровня (Wind, Shaders/wind.sh); cb_windStrength 0 — растения неподвижны
	float2 cb_windDirection;	// куда дует, нормированное (x, z)
	float  cb_windStrength;
	float  cb_windSpeed;		// скорость волн порывов, м/с
	float  cb_windGustMin;		// порыв между волнами и на гребне — доли силы
	float  cb_windGustMax;
	float  cb_windGustSize;		// длина волны порыва, м
	float  cb_framePadding;
};

// Раскладка — ConstantBuffers::ShaderModelConstant
cbuffer WorldBuffer : register( SLOT_CB_OBJECT )
{
	matrix cb_worldMatrix;
	matrix cb_worldInverseTransposeMatrix;	// для нормалей: перпендикулярны поверхности и при неравномерном масштабе
};


#endif