
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
	float  cb_appTime;
	float3 cb_viewDirection;
	float  cb_elapsedTime;
	float  cb_lightCount;
	float3 cb_lodOrigin;	// откуда считаются LOD и морфинг: у видов теней — позиция главной камеры
	float  cb_skyLightScale;	// масштаб освещения окружением (SkyLight): освещённость от солнца, лк (атмосфера запечена для 1 лк), × нормировка результата, или интенсивность панорамы
	float  cb_aerialPerspectiveDistance;	// до какого расстояния от камеры, м, идут слои воздушной перспективы — дальняя плоскость главного вида
	float  cb_aerialPerspectiveScale;	// множитель объёма воздушной перспективы: cb_skyScale × нормировка объёма
	float  cb_skyScale;			// масштаб фона неба: освещённость от солнца, лк, или интенсивность панорамы
};

// Раскладка — ConstantBuffers::ShaderModelConstant
cbuffer WorldBuffer : register( SLOT_CB_OBJECT )
{
	matrix cb_worldMatrix;
	matrix cb_worldInverseTransposeMatrix;	// для нормалей: перпендикулярны поверхности и при неравномерном масштабе
};


#endif