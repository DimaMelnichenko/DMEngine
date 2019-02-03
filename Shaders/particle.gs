////////////////////////////////////////////////////////////////////////////////
// Filename: particle.gs
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "samplers.sh"

/////////////
// GLOBALS //
/////////////

//Texture2D texture_distribution : register(t0);

cbuffer ParticleSystemParam : register(b2)
{
	
}


//////////////
// TYPEDEFS //
//////////////

struct PixelInput
{
	float4 position : SV_POSITION;
	float2 uv: TEXCOORD;
	float2 world_x_y : TEXCOORD1;
};

////////////////////////////////////////////////////////////////////////////////
// Geometry Shader
////////////////////////////////////////////////////////////////////////////////

PixelInput offsetNprojected(PixelInput data, float2 offset, float2 uv)
{
	data.position.xy += offset;
	data.position = mul(data.position, cb_projectionMatrix);
	data.uv = uv;

	return data;
}

[maxvertexcount(4)] // максимальное кол-во вертексов, которое мы можем добавить
void main( point PixelInput input[1], inout TriangleStream<PixelInput> stream )
{
	PixelInput pointOut = input[0];
	
	float texel = 1.0f / ( 600.0 );

	pointOut.position = mul( pointOut.position, cb_viewMatrix);
	
	const float max_size = 0.05f; // размер конечного квадрата 
	// описание квадрата
	stream.Append( offsetNprojected(pointOut, float2(-1, 0) * max_size, float2(0, 1)) );
	stream.Append( offsetNprojected(pointOut, float2(-1, 2) * max_size, float2(0, 0)) );
	stream.Append( offsetNprojected(pointOut, float2( 1, 0) * max_size, float2(1, 1)) );
	stream.Append( offsetNprojected(pointOut, float2( 1, 2) * max_size, float2(1, 0)) );

	// создать TriangleStrip
	stream.RestartStrip();
}
