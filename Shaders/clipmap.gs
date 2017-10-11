////////////////////////////////////////////////////////////////////////////////
// Filename: particle.gs
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"

/////////////
// GLOBALS //
/////////////
Texture2D texture_distribution : register(t0);
Texture2D texture_height : register(t1);
Texture2D texture_color : register(t2);
Texture2D texture_noise : register(t3);
SamplerState SampleLinearType : register(s0);
SamplerState SamplePointType : register(s1);

cbuffer Parameters : register(b2)
{
	float cb_map_size;
}

//////////////
// TYPEDEFS //
//////////////
struct GS_generate
{
	float3 position : POSITION0;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 main_tex : TEXCOORD0;
	float2 detail_tex : TEXCOORD1;
	float4 world_position : TEXCOORD2;
};

void GenerateLine(PixelInputType vertex)
{
    gl_Position = gl_in[index].gl_Position;
    EmitVertex();
    gl_Position = gl_in[index].gl_Position + vec4(gs_in[index].normal, 0.0) * MAGNITUDE;
    EmitVertex();
    EndPrimitive();
}

[maxvertexcount(6)] // максимальное кол-во вертексов, которое мы можем добавить
void main( triangl PixelInputType input[3], inout LineStream<PixelInputType> stream )
{
	PixelInput pointOut = input[0];
	
	float texel = 1.0f / ( 1024.0 );
	float texel2 = 1.0f / 256.0;
	
	float size = texture_distribution.SampleLevel( SampleLinearType, float2( ( pointOut.position.x ) * texel, 1.0 - ( pointOut.position.z ) * texel ), 0 ).r;
	pointOut.color = texture_color.SampleLevel( SampleLinearType, float2( 1.0 - ( cb_cameraPosition.x - pointOut.position.x - 1 + cb_map_size / 2.0 ) * texel2, 
																		  ( cb_cameraPosition.z - pointOut.position.z - 1 + cb_map_size / 2.0 ) * texel2 ), 0 ).rgb * 1.0;
			
	pointOut.position = mul( pointOut.position, cb_viewMatrix);
	
	CreateBilboard( stream, pointOut, size );

	// создать TriangleStrip
	stream.RestartStrip();
}

