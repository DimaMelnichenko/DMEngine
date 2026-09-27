
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
	float  cb_skyIlluminance;	// освещённость от солнца, лк: небо и освещение окружением запечены для солнца 1
	float3 cb_framePadding;
};

// Раскладка — ConstantBuffers::ShaderModelConstant
cbuffer WorldBuffer : register( SLOT_CB_OBJECT )
{
	matrix cb_worldMatrix;
	matrix cb_worldInverseTransposeMatrix;	// для нормалей: перпендикулярны поверхности и при неравномерном масштабе
};


#endif