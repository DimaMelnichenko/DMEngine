
#include "common.vs"

//////////////
// TYPEDEFS //
//////////////
struct VertexInput
{
    float3 position : POSITION;
	float2 texCoord : TEXCOORD0;
};

struct PixelInput
{
    float4 position : SV_POSITION;
	float2 texCoord : TEXCOORD0;
};

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInput main(VertexInput input)
{
    PixelInput output;
    

    // Change the position vector to be 4 units for proper matrix calculations.
	output.position = float4( input.position, 1.0f );

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(output.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
	
	output.texCoord = input.texCoord;
    
    return output;
}