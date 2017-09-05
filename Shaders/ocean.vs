////////////////////////////////////////////////////////////////////////////////
// Filename: terrain.vs
////////////////////////////////////////////////////////////////////////////////


/////////////
// GLOBALS //
/////////////
cbuffer MatrixBuffer
{
    matrix worldMatrix;
    matrix viewMatrix;
    matrix projectionMatrix;
};

cbuffer TerrainBuffer
{
	float2 map_offset;
	float map_scale;
	float map_N;
	float map_height_multippler;
	float map_texture_scale;
	float map_time;
};

Texture2D terrainTexture[7];
SamplerState SampleHM;
SamplerState SampleType;

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float3 position : POSITION;
    float2 tex : TEXCOORD0;
	//float3 normal : NORMAL0;
	//float4 instance_pos: INSTANCEPOS1;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 main_tex : TEXCOORD0;
	float2 detail_tex : TEXCOORD1;
	//float4 depthPosition : TEXCOORD3;
};

float Height( float u, float v )
{
	return terrainTexture[1].SampleLevel( SampleHM, float2( u, v ), 0 ).r;
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	
	output.position = float4( input.position, 1.0f );
	output.position.xz *= map_scale;
	output.position.xz += map_offset.xy;
	
	// сшивание краев разных лодов
	float modX = max( 0, 1 - round( fmod( input.position.x, map_N - 1 ) ) );
	float modZ = max( 0, 1 - round( fmod( input.position.z, map_N - 1 ) ) );
	
	output.position.x -= map_scale * modZ * round( fmod( input.position.x, 2 ) );
	output.position.z -= map_scale * modX * round( fmod( input.position.z, 2 ) );
	//
	
	output.detail_tex = output.position.xz;
	output.main_tex = output.position.xz * map_texture_scale;
	
	output.position.y += Height( output.main_tex.x + map_time, output.main_tex.y ) * map_height_multippler;
	
	
    // Calculate the position of the vertex against the world, view, and projection matrices.
	output.position = mul(output.position, worldMatrix);
    output.position = mul(output.position, viewMatrix);
    output.position = mul(output.position, projectionMatrix);
		
	
    return output;
}
	