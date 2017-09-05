
/////////////
// GLOBALS //
/////////////
cbuffer VPCameraBuffer : register( b0 )
{
    matrix viewMatrix;
    matrix projectionMatrix;
	float3 camera_position;
	float3 view_direction;
};

cbuffer WorldBuffer : register( b1 )
{
    matrix worldMatrix; 
};