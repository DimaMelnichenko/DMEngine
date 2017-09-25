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
SamplerState SampleType;

//////////////
// TYPEDEFS //
//////////////

struct PixelInput
{
	float4 position : SV_POSITION; // стандартный System-Value для вертекса
	float2 uv: TEXCOORD;
	float2 world_x_y : TEXCOORD1;
};

struct GS_generate
{
	float4 position : POSITION;
};


////////////////////////////////////////////////////////////////////////////////
// Geometry Shader
////////////////////////////////////////////////////////////////////////////////

PixelInput _offsetNprojected(PixelInput data, float2 offset, float2 uv)
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
	
	float texel = 1.0f / ( 1024.0 * 0.175 );
	
	float size = texture_distribution.SampleLevel( SampleType, float2( ( pointOut.world_x_y.x ) * texel, 1.0 - ( pointOut.world_x_y.y ) * texel ), 0 ).r;
	
	if( size < 0.4 )
	{
		//stream.RestartStrip();
		return;
	}	
	
	pointOut.position.y = texture_height.SampleLevel( SampleType, float2( ( pointOut.position.x ) * texel , 1.0 - ( pointOut.position.z ) * texel ), 0 ).r * 40.0;
	
	pointOut.position = mul( pointOut.position, cb_viewMatrix);
	
	const float max_size = 0.3f; // размер конченого квадрата 
	// описание квадрата
	stream.Append( _offsetNprojected(pointOut, float2(-1, 0) * size, float2(0, 1)) );
	stream.Append( _offsetNprojected(pointOut, float2(-1, 2) * size, float2(0, 0)) );
	stream.Append( _offsetNprojected(pointOut, float2( 1, 0) * size, float2(1, 1)) );
	stream.Append( _offsetNprojected(pointOut, float2( 1, 2) * size, float2(1, 0)) );

	// создать TriangleStrip
	stream.RestartStrip();
}


/*
[maxvertexcount(1)] // максимальное кол-во вертексов, которое мы можем добавить
void generate( point GS_generate input[1], inout PointStream<GS_generate> stream )
{
	GS_generate pointOut = input[0];	
	
	stream.Append( pointOut );	
	
	stream.RestartStrip();
}

*/