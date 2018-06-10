////////////////////////////////////////////////////////////////////////////////
// Filename: terrain.vs
////////////////////////////////////////////////////////////////////////////////


#include "common.vs"
#include "samplers.sh"

cbuffer TerrainBuffer : register( b2 )
{
	float levelScale;
	float mapSize;
	float mapWidthMultipler;	
	float mapHeightMultippler;
	float map_N;
	float hightOffset;
	float mapScale;	
	float mapOffsetSpeed;
	float2 mapOffset;
	float2 dump;
};

struct InstanceParam
{
	float2 level_pos;
	float2 offset;
	float scale;	
};

StructuredBuffer<InstanceParam> instance_offset: register(t1);

Texture2D terrainTexture : register(t0);

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float3 position : POSITION;
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

float Height( float2 uv )
{
	return terrainTexture.SampleLevel( g_SamplerLinearWrap, uv, 0 ).r;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	
	InstanceParam instItem = instance_offset[input.instance_index];
	
	output.position = float4( input.position, 1.0f );
	//output.position.xz *= map_scale;
	output.position.xz *= instItem.scale;
	
	//old
	//output.position.xz += map_offset.xy;
	//new
	output.position.xz += instItem.level_pos + instItem.offset * instItem.scale;
	

	// сшивание краев разных лодов
	float modX = max( 0, 1 - round( fmod( input.position.x + instItem.offset.x, map_N - 1 ) ) );
	float modZ = max( 0, 1 - round( fmod( input.position.z + instItem.offset.y, map_N - 1 ) ) );
	
	output.position.x -= instItem.scale * modZ * round( fmod( input.position.x + instItem.offset.x, 2 ) );
	output.position.z -= instItem.scale * modX * round( fmod( input.position.z + instItem.offset.y, 2 ) );
	
	output.detail_tex = output.position.xz;
	output.main_tex = output.position.xz * ( 1.0 / (mapSize * mapWidthMultipler) ) * mapScale;
	output.main_tex += mapOffset * cb_appTime * mapOffsetSpeed;
	output.main_tex.y = 1.0f - output.main_tex.y;
	
	output.position.y = Height( output.main_tex ) * mapHeightMultippler + hightOffset;
	//output.position.y = 0.0f;
	
	
	if( output.position.x <= 0.0 )
	{
		output.position.x = 0.0;
		output.position.y = 0.0;
	}
	
	if( output.position.z <= 0.0 )
	{
		output.position.z = 0.0;
		output.position.y = 0.0;
	}
	
	if( output.position.x >= ( mapSize * mapWidthMultipler ) )
	{
		output.position.x = mapSize * mapWidthMultipler;
		output.position.y = 0.0;
	}
	
	if( output.position.z >= ( mapSize * mapWidthMultipler ) )
	{
		output.position.z = mapSize * mapWidthMultipler;
		output.position.y = 0.0;
	}
	
	
    // Calculate the position of the vertex against the world, view, and projection matrices.
	output.position = mul(output.position, cb_worldMatrix);
	output.world_position = output.position;
    output.position = mul(output.position, cb_viewProjectionMatrix);
		
	
    return output;
}
	