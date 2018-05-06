////////////////////////////////////////////////////////////////////////////////
// Filename: light.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION0;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL0;
	float3 tangent : TANGENT0;
	float3 binormal : BINORMAL0;
	uint instance_index: SV_InstanceID;
};

#if defined(INST_POS) || defined(INST_SCALE) || defined(INST_TEX)

struct InstanceParam
{
#ifdef INST_POS
	float3 pos;
#else	
	float dummy;
#endif
#ifdef INST_SCALE
	float scale;
#endif
#ifdef INST_TEX
	float2 tex;
#endif
};

StructuredBuffer<InstanceParam> instance_vector: register(t16);

#endif

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
	float3 tangent : TANGENT0;
	float3 binormal : BINORMAL0;
	float3 worldPosition : TEXCOORD1;
	uint instanceIndex : SV_InstanceID;
};


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	float3 worldPosition;

    output.position.xyz = input.position;
	output.position.w = 1.0f;
	
	output.tex = input.tex;
	
	output.instanceIndex = input.instance_index;
	
	// instance defines block
#ifdef INST_SCALE
	output.position.xyz *= instance_vector[input.instance_index].scale;
#endif
	
#ifdef INST_POS
	output.position.xyz += instance_vector[input.instance_index].pos;
#endif
#ifdef INST_TEX
	output.tex += instance_vector[input.instance_index].tex;
#endif
    
    

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(output.position, cb_worldMatrix);
	output.worldPosition = output.position.xyz;
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    
	
	    // Calculate the normal vector against the world matrix only.
    output.normal = normalize( mul(input.normal, (float3x3)cb_worldMatrix) );
	output.tangent	= normalize( mul( input.tangent, (float3x3)cb_worldMatrix ) );
	output.binormal = normalize( mul( input.binormal, (float3x3)cb_worldMatrix ) );
	
    // Normalize the normal vector.
    output.normal = normalize(output.normal);
	

    return output;
}