////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"
#include "instance.sh"

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION0;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL0;
	uint instanceIndex: SV_InstanceID;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
	float4 color : COLOR0;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
    output.position = float4( input.position.xyz, 1.0f );
	
	output.tex = input.tex;
	
	#if defined(INSTANCE_INCLUDE)
		output.position.xyz = calcInstance( output.position.xyz, input.instanceIndex );
	#endif
	
    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(output.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
	
	// Calculate the normal vector against the world matrix only.
    float3 normal = normalize( mul(input.normal, (float3x3)cb_worldMatrix) );
	output.color = dot( normal, normalize(float3( 0.1, 0.5, -0.1 )));
	output.color.a = 1.0f;
	
	

    return output;
}