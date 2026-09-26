
/////////////
// GLOBALS //
/////////////

#ifndef COMMON_VS
#define COMMON_VS

cbuffer FrameConstantBuffer : register( b0 )
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
	float3 fcb_dump;
	float4 cb_ambientSkyColor;
	float4 cb_ambientGroundColor;
};

cbuffer WorldBuffer : register( b1 )
{
    matrix cb_worldMatrix; 
};


#endif