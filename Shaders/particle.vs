////////////////////////////////////////////////////////////////////////////////
// Filename: texture.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"

//////////////
// TYPEDEFS //
//////////////
struct Particle // описание структуры на GPU
{
    float3 position;
	float3 velocity;
	float2 dummy;
};

StructuredBuffer<Particle> Particles : register(t5); // буфер частиц



struct VertexInputType
{
    uint vertex_id : SV_VertexID;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
	float2 uv: TEXCOORD;
	float2 world_x_y : TEXCOORD1;
};

struct VS_generate
{
	float4 position: POSITION;
};

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output = (PixelInputType)0;

    Particle particle = Particles[input.vertex_id];
	
	//particle.position.x += cb_cameraPosition.x;
	//particle.position.z += cb_cameraPosition.z;
	
	output.position = float4( particle.position.xyz, 1.0f );
	
    // Calculate the position of the vertex against the world, view, and projection matrices.
    //output.position = mul(output.position, cb_worldMatrix);
	output.world_x_y = output.position.xz;
    //output.position = mul(output.position, cb_viewMatrix);
    //output.position = mul(output.position, cb_projectionMatrix);

	output.uv = (float2)0.0;
		
    return output;
}



VS_generate generate(VertexInputType input)
{
	VS_generate output = (VS_generate)0;
	
	return output;
}
