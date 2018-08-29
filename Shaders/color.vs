
#include "common.vs"

//////////////
// TYPEDEFS //
//////////////
struct VertexInput
{
    float3 position : POSITION;
	uint instanceIndex: SV_InstanceID;
};

struct PixelInput
{
    float4 position : SV_POSITION;
};

#if defined(INST_POS) || defined(INST_SCALE) || defined(INST_TEX)

struct InstanceItem
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

StructuredBuffer<InstanceItem> instanceVector: register(t16);

#endif

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInput main(VertexInput input)
{
    PixelInput output;
    
	// Change the position vector to be 4 units for proper matrix calculations.
	output.position = float4( input.position, 1.0f );

	// instance defines block
#ifdef INST_SCALE
	output.position.xyz *= instanceVector[input.instanceIndex].scale;
#endif
#ifdef INST_POS
	output.position.xyz += instanceVector[input.instanceIndex].pos;
#endif
#ifdef INST_TEX
	output.tex += instanceVector[input.instanceIndex].tex;
#endif

    

    // Calculate the position of the vertex against the world, view, and projection matrices.
    output.position = mul(output.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
    
    return output;
}