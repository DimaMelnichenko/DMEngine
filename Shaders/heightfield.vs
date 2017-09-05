////////////////////////////////////////////////////////////////////////////////
// Filename: terrain.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"

Texture2D terrainTexture : register(t0);

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float3 position : POSITION;
    float2 tex : TEXCOORD0;
	//float3 normal : NORMAL0;
	uint instance_index: SV_InstanceID;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float Height( float u, float v )
{
	return terrainTexture.SampleLevel( g_SamplerPointWrap, float2( u, v ), 0 ).r;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	
	output.position = float4( input.position, 1.0f );
	
	output.position.y += terrainTexture.SampleLevel( g_SamplerPointWrap, input.tex, 0 ).r;
	//output.position.y = 0.0f;
	
	output.tex = input.tex;
    // Calculate the position of the vertex against the world, view, and projection matrices.
	output.position = mul(output.position, worldMatrix);	
    output.position = mul(output.position, viewMatrix);
    output.position = mul(output.position, projectionMatrix);
		
	
    return output;
}
	