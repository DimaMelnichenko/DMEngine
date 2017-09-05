////////////////////////////////////////////////////////////////////////////////
// Filename: terrain.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"

cbuffer TerrainBuffer : register( b2 )
{
	float2 map_offset;
	float map_scale;
	float map_N;
	float map_height_multippler;
	float map_texture_scale;
};

struct InstanceParam
{
	float2 level_pos;
	float2 offset;
	float scale;	
};

StructuredBuffer<InstanceParam> instance_offset: register(t16);

Texture2D terrainTexture[15] : register(t0);

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
    float2 main_tex : TEXCOORD0;
	float2 detail_tex : TEXCOORD1;
	float4 world_position : TEXCOORD2;
	//float4 depthPosition : TEXCOORD3;
};

float Height( float u, float v )
{
	return terrainTexture[0].SampleLevel( g_SamplerPointWrap, float2( u, v ), 0 ).r;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	
	output.position = float4( input.position, 1.0f );
	//output.position.xz *= map_scale;
	output.position.xz *= instance_offset[input.instance_index].scale;
	
	//old
	//output.position.xz += map_offset.xy;
	//new
	output.position.xz += instance_offset[input.instance_index].level_pos + instance_offset[input.instance_index].offset * instance_offset[input.instance_index].scale;
	
	float2 offset = instance_offset[input.instance_index].offset;
	

	// сшивание краев разных лодов
	float modX = max( 0, 1 - round( fmod( input.position.x + offset.x, map_N - 1 ) ) );
	float modZ = max( 0, 1 - round( fmod( input.position.z + offset.y, map_N - 1 ) ) );
	
	output.position.x -= instance_offset[input.instance_index].scale * modZ * round( fmod( input.position.x + offset.x, 2 ) );
	output.position.z -= instance_offset[input.instance_index].scale * modX * round( fmod( input.position.z + offset.y, 2 ) );
	
	
	output.detail_tex = output.position.xz;
	output.main_tex = output.position.xz * map_texture_scale;
	output.main_tex.y = 1.0f - output.main_tex.y;
	
	output.position.y += Height( output.main_tex.x, output.main_tex.y ) * map_height_multippler;
	//output.position.y = 0.0f;
	
	
	if( output.position.x < 0.0 )
	{
		output.position.x = 0.0;
		output.position.y = 0.0;
	}
	
	if( output.position.z < 0.0 )
	{
		output.position.z = 0.0;
		output.position.y = 0.0;
	}
	
	output.position.x = min( output.position.x, 2049 );
	output.position.z = min( output.position.z, 2049 );
	
    // Calculate the position of the vertex against the world, view, and projection matrices.
	output.position = mul(output.position, worldMatrix);
	output.world_position = output.position;
    output.position = mul(output.position, viewMatrix);
    output.position = mul(output.position, projectionMatrix);
		
	
    return output;
}
	