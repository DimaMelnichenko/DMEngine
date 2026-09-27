////////////////////////////////////////////////////////////////////////////////
// Грани cubemap: направление на тексель грани. Общее для неба (sky_cube.ps), панорамы (hdri_cube.ps) и освещения
// окружением из cubemap (sky_irradiance.cs, sky_prefilter.ps)
////////////////////////////////////////////////////////////////////////////////

#ifndef CUBEMAP_SH
#define CUBEMAP_SH

static const float cubemapPi = 3.14159265f;

// Направление на тексель грани cubemap (соглашение Direct3D: v вниз); грань 0…5 — +X, −X, +Y, −Y, +Z, −Z
float3 cubeDirection( int face, float2 uv )
{
	float2 st = uv * 2.0f - 1.0f;
	float3 direction;
	if( face == 0 )			direction = float3( 1.0f, -st.y, -st.x );
	else if( face == 1 )	direction = float3( -1.0f, -st.y, st.x );
	else if( face == 2 )	direction = float3( st.x, 1.0f, st.y );
	else if( face == 3 )	direction = float3( st.x, -1.0f, -st.y );
	else if( face == 4 )	direction = float3( st.x, -st.y, 1.0f );
	else					direction = float3( -st.x, -st.y, -1.0f );
	return normalize( direction );
}

#endif
