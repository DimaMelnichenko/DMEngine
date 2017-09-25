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

struct PixelInput
{
    float4 position : SV_POSITION;
	float2 uv: TEXCOORD0;	
	float3 color : TEXCOORD1;
};

PixelInput _offsetNprojected(PixelInput data, float2 offset, float2 uv, float color_corrector )
{
	data.position.xy += offset;
	data.position = mul(data.position, cb_projectionMatrix);
	data.uv = uv;
	data.color.rgb *= color_corrector;

	return data;
}

void CreateBilboard( inout TriangleStream<PixelInput> stream, PixelInput pointOut, float size )
{
	const float max_size = 0.8f; // размер конечного квадрата 
	// описание квадрата	
	stream.Append( _offsetNprojected( pointOut, float2(-1, 0) * size * max_size, float2(0, 1), 0.5 ) );
	stream.Append( _offsetNprojected( pointOut, float2(-1, 2) * size * max_size, float2(0, 0), 1.0 ) );
	stream.Append( _offsetNprojected( pointOut, float2( 1, 0) * size * max_size, float2(1, 1), 0.5 ) );
	stream.Append( _offsetNprojected( pointOut, float2( 1, 2) * size * max_size, float2(1, 0), 1.0 ) );
}

////////////////////////////////////////////////////////////////////////////////
// Geometry Shader
/////////////////////////////////////////////////////////////////////////////////
[maxvertexcount(1)] // максимальное кол-во вертексов, которое мы можем добавить
void generate( point GS_generate input[1], inout PointStream<GS_generate> stream )
{
	GS_generate pointOut = input[0];	
	
	float tex_size = ( 1024.0 );
	float texel = 1.0f / ( 1024.0 );
	float texel_noise = 1.0f / ( 64.0 );
	
	float2 offset = texture_noise.SampleLevel( SamplePointType, float2( ( pointOut.position.x * 6 ) * texel_noise , 1.0 - ( pointOut.position.z * 10 ) * texel_noise ), 0 ).xz;
	//float2 offset = float2( 0.0, 0.0 );
	
	pointOut.position.xz += ( 2 * offset - 1 ) * 4;
	
	float size = texture_distribution.SampleLevel( SampleLinearType, float2( ( pointOut.position.x ) * texel, 1.0 - ( pointOut.position.z ) * texel ), 0 ).r;
	
	pointOut.position.y = texture_height.SampleLevel( SamplePointType, float2( ( pointOut.position.x ) * texel , 1.0 - ( pointOut.position.z ) * texel ), 0 ).r * 300.0;
	
	float4 frustum_pos;
	frustum_pos.xyz = pointOut.position;
	frustum_pos.w = 1.0f;
	
	frustum_pos = mul( frustum_pos, cb_viewMatrix );
	
	int checks[6] = { 0, 0, 0, 0, 0, 0 };	
	float4 bilboard[4];	
	bilboard[0] = mul( float4( frustum_pos.x - 1.0, frustum_pos.y, frustum_pos.zw ), cb_projectionMatrix );
	bilboard[1] = mul( float4( frustum_pos.x - 1.0, frustum_pos.y + 2.0, frustum_pos.zw ), cb_projectionMatrix );
	bilboard[2] = mul( float4( frustum_pos.x + 1.0, frustum_pos.y, frustum_pos.zw ), cb_projectionMatrix );
	bilboard[3] = mul( float4( frustum_pos.x + 1.0, frustum_pos.y + 2.0, frustum_pos.zw ), cb_projectionMatrix );
	
	for( int i = 0; i < 4; i++ )
	{
		if( bilboard[i].x > bilboard[i].w  ) checks[0]++;
		if( bilboard[i].x < -bilboard[i].w ) checks[1]++;
		if( bilboard[i].y > bilboard[i].w  ) checks[2]++;
		if( bilboard[i].y < -bilboard[i].w ) checks[3]++;
		if( bilboard[i].z > bilboard[i].w  ) checks[4]++;
		if( bilboard[i].z < -bilboard[i].w ) checks[5]++;
	}
	
	for( int i = 0; i < 6; i++ )
	{
		if( checks[i] == 4 )
		{
			stream.RestartStrip();
			return;
		}
	}
	
	if( size < 0.2 || pointOut.position.x >= tex_size - 1 || pointOut.position.x <= 0 || pointOut.position.z >= tex_size - 1 || pointOut.position.z <= 0 || 
		length( cb_cameraPosition.xyz - pointOut.position.xyz ) > cb_map_size / 2  )
	{
		stream.RestartStrip();
		return;
	}
	
	stream.Append( pointOut );	
	
	stream.RestartStrip();
}

[maxvertexcount(4)] // максимальное кол-во вертексов, которое мы можем добавить
void main( point PixelInput input[1], inout TriangleStream<PixelInput> stream )
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

