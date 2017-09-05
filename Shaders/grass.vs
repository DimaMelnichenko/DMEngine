////////////////////////////////////////////////////////////////////////////////
// Filename: texture.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"

//////////////
// TYPEDEFS //
//////////////

cbuffer Parameters : register(b2)
{
	float cb_map_size;
}

struct VertexInputGen
{
	uint vertex_id : SV_VertexID;
};


struct VertexInputType
{
    float3 position : POSITION0;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float3 color : TEXCOORD1;
};

float fmod( float x, float y )
{
	return x - y * trunc(x/y);
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
VertexInputType generate(VertexInputGen input)
{
	VertexInputType output = (VertexInputType)0;
	
	output.position.x = trunc( camera_position.x ) + fmod( input.vertex_id, cb_map_size ) + 1 - cb_map_size / 2;	
	output.position.y = 0.0f;
	output.position.z = trunc( camera_position.z ) + trunc( input.vertex_id / cb_map_size ) + 1 - cb_map_size / 2;		
    return output;
}


PixelInputType main(VertexInputType input)
{
	PixelInputType output = (PixelInputType)0;
	
	output.position.xyz = input.position;
	output.position.w = 1.0f;
	
    return output;
}