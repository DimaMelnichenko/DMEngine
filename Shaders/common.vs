
/////////////
// GLOBALS //
/////////////
cbuffer FrameConstantBuffer : register( b0 )
{
    matrix cb_viewMatrix;
	matrix cb_viewInverseMatrix;
    matrix cb_projectionMatrix;
	matrix cb_viewProjectionMatrix;
	float3 cb_cameraPosition;
	float3 cb_viewDirection;
	float  cb_appTime;
	float  cb_elapsedTime;
};

cbuffer WorldBuffer : register( b1 )
{
    matrix cb_worldMatrix; 
};