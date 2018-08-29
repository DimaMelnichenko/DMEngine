////////////////////////////////////////////////////////////////////////////////
// Filename: texture.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"

//////////////
// TYPEDEFS //
//////////////

struct GrassItem
{
    float3 position;
	float size;
};
StructuredBuffer<GrassItem> g_grassBuffer : register(t5); // буфер частиц

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
	float size : TEXCOORD0;
	float3 color : TEXCOORD1;
};

float fmod( float x, float y )
{
	return x - y * trunc(x/y);
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////

PixelInputType main(VertexInputType input)
{
	PixelInputType output = (PixelInputType)0;
	
	output.position.xyz = input.position;
	output.position.w = 1.0f;
	
    return output;
}