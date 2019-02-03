

#include "common.vs"
#include "samplers.sh"

cbuffer cbQuadTesselation : register( b2 )
{
	float4	g_f4TessFactors;
	float	g_innerTess;	
	float	g_hightMultipler;	
	float2	g_snapWorldOffset;
};

struct PathParameters
{
	float2 offset;
	float sizeMultipler;
	float ppDump;
};

Texture2D g_heightMap : register(t0);
Texture2D g_normalMap : register(t1);
Texture2D g_diffuseMap : register(t2);
Texture2D g_microHightMap : register(t3);
Texture2D g_microNormalMap : register(t4);


StructuredBuffer<PathParameters> g_instancedPathParameters : register(t16);

//=================================================================================================================================
// Functions
//=================================================================================================================================
float3 bilerpUV(float3 v[4], float2 uv)
{
	// bilerp the texture coordinates    
	float3 bottom = lerp( v[0], v[1], uv.x );
	float3 top = lerp( v[3], v[2], uv.x );
	float3 result = lerp( bottom, top, uv.y );

	return result;    
}

//=================================================================================================================================
// Shader structures
//=================================================================================================================================


struct Position_Input
{
	float3 f3Position   : POSITION0;
	uint instanceIndex: SV_InstanceID;
};

struct HS_ConstantOutput_Quad
{
	float fTessFactor[4]       : SV_TessFactor;
	float fInsideTessFactor[2] : SV_InsideTessFactor;
};

struct DS_Output
{
	float4 f4Position   : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

struct PS_RenderSceneInput
{
	float4 f4Position   : SV_Position;
	float2 TexCoord : TEXCOORD0;
};

struct PS_RenderOutput
{
	float4 f4Color      : SV_Target0;
};

Position_Input VS_RenderSceneWithTessellation( Position_Input I )
{
	Position_Input O;

	// Pass through world space position
	O.f3Position = I.f3Position;
	O.f3Position *= g_instancedPathParameters[I.instanceIndex].sizeMultipler;
	float2 posOffset = g_instancedPathParameters[I.instanceIndex].offset;
	O.f3Position.xz += posOffset + g_snapWorldOffset;
	
	float2 texCoord = float2( O.f3Position.x, -O.f3Position.z );
	O.f3Position.y = g_heightMap.SampleLevel( g_SamplerLinearWrap, texCoord / 4096.0, 0 ).r * g_hightMultipler;
	
	//O.f3Position = mul( O.f3Position, cb_worldMatrix );
 
	return O;    
}

float3 normalToTerrain( float3 vec, float strength )
{
	vec.yz = vec.zy;
	vec.z *= -1.0f;
	vec.y *= ( 1.0f / strength );
	return normalize( vec );
}

float ClipToScreenSpaceTessellation(float4 clip0, float4 clip1)
{
	clip0 /= clip0.w;
	clip1 /= clip1.w;

	static const float2 g_screenSize = { 1920.0f, 1080.0f };

	clip0.xy *= g_screenSize;
	clip1.xy *= g_screenSize;

	const float d = distance(clip0, clip1);

	// g_tessellatedTriWidth is desired pixels per tri edge
	static const int g_tessellatedTriWidth = 10;
	return clamp( d / g_tessellatedTriWidth, 0.0, 64.0 );
}


// Project a sphere into clip space and return the number of triangles that are required to fit across the 
// screenspace diameter.  (For convenience of the caller, we expect two edge end points and use the mid point as centre.)
float SphereToScreenSpaceTessellation(float3 p0, float3 p1, float diameter)
{
	float3 centre = 0.5 * (p0+p1);
	float4 view0 = mul(float4(centre,1), cb_viewMatrix);
	float4 view1 = view0;
	view1.x += diameter;

	float4 clip0 = mul(view0, cb_projectionMatrix);
	float4 clip1 = mul(view1, cb_projectionMatrix);
	return ClipToScreenSpaceTessellation(clip0, clip1);
}

bool inFrustum(const float3 patch_center, const float3 eyePos, const float3 viewDir, float margin )
{
	// conservative frustum culling
	float3 camera_to_patch_vector = patch_center - eyePos;
	float3 patch_to_camera_direction_vector = viewDir * dot( camera_to_patch_vector, viewDir ) - camera_to_patch_vector;
	float3 patch_center_realigned = patch_center + normalize(patch_to_camera_direction_vector) * min( margin, length( patch_to_camera_direction_vector ) );
	float4 patch_screenspace_center = mul(float4(patch_center_realigned, 1.0), cb_worldMatrix);
	
	patch_screenspace_center = mul( patch_screenspace_center, cb_viewMatrix );
	patch_screenspace_center = mul( patch_screenspace_center, cb_projectionMatrix );
	

	if( ( (  patch_screenspace_center.x / patch_screenspace_center.w > -1.0f ) 
		&& ( patch_screenspace_center.x / patch_screenspace_center.w < 1.0f ) 
		&& ( patch_screenspace_center.y / patch_screenspace_center.w > -1.0f ) 
		&& ( patch_screenspace_center.y / patch_screenspace_center.w < 1.0f ) 
		&& ( patch_screenspace_center.w > 0.0f ) ) 
		|| ( length( patch_center - eyePos ) < margin ) )
	{
		return true;
	}

	return false;
}

bool insideMap( float4 b ) 
{
	float4 a = { 0.0f, 4096.0f, 4096.0f, 0.0f };
	return ( a.y < b.w || a.w > b.y || a.z < b.x || a.x > b.z );
}

HS_ConstantOutput_Quad HS_QuadsConstant( InputPatch<Position_Input, 4> I )
{
	HS_ConstantOutput_Quad O = (HS_ConstantOutput_Quad)0;

	float3 centre = 0.25f * ( I[0].f3Position + I[1].f3Position + I[2].f3Position + I[3].f3Position );
	float  sideLen = max(abs(I[1].f3Position.x - I[0].f3Position.x), abs(I[1].f3Position.x - I[2].f3Position.x)); // assume square & uniform
	float  diagLen = sqrt( 2.0f * sideLen * sideLen );
	//float  diagLen = 2.0f * abs(I[1].f3Position.x - I[0].f3Position.x);

	float3 leftTop = mul( I[3].f3Position, cb_worldMatrix );
	float3 botomRight = mul( I[1].f3Position, cb_worldMatrix );
	
	if( !inFrustum( centre, cb_cameraPosition, cb_viewDirection, diagLen ) )// || insideMap( float4( leftTop.xz, botomRight.xz ) ) )
	{
		O.fInsideTessFactor[0] = 0;
		O.fInsideTessFactor[1] = 0;
		O.fTessFactor[0] = 0;
		O.fTessFactor[1] = 0;
		O.fTessFactor[2] = 0;
		O.fTessFactor[3] = 0;
	}
	else
	{
		/*
		float2 ritf;
		float2 itf; 
		float4 rtf;
		
		Process2DQuadTessFactorsMax( g_f4TessFactors, 1.0f, rtf, ritf, itf);

		O.fTessFactor[0] = rtf.x;
		O.fTessFactor[1] = rtf.y;
		O.fTessFactor[2] = rtf.z;
		O.fTessFactor[3] = rtf.w;
		O.fInsideTessFactor[0] = ritf.x;
		O.fInsideTessFactor[1] = ritf.y;
		*/
		O.fTessFactor[0] = SphereToScreenSpaceTessellation( I[0].f3Position, I[1].f3Position, sideLen );
		O.fTessFactor[3] = SphereToScreenSpaceTessellation( I[1].f3Position, I[2].f3Position, sideLen );
		O.fTessFactor[2] = SphereToScreenSpaceTessellation( I[2].f3Position, I[3].f3Position, sideLen );
		O.fTessFactor[1] = SphereToScreenSpaceTessellation( I[3].f3Position, I[0].f3Position, sideLen );
		O.fInsideTessFactor[1] = 0.5f * ( O.fTessFactor[0] + O.fTessFactor[2] );
		O.fInsideTessFactor[0] = 0.5f * ( O.fTessFactor[1] + O.fTessFactor[3] );
		
	}
	return O;
}

[domain("quad")]
//[partitioning("fractional_odd")]
[partitioning("integer")]
[outputtopology("triangle_ccw")]
[patchconstantfunc("HS_QuadsConstant")]
[outputcontrolpoints(4)]
Position_Input HS_Quads_fractionaleven( InputPatch<Position_Input, 4> I, uint uCPID : SV_OutputControlPointID )
{
	Position_Input O = (Position_Input)0;

	O.f3Position = I[uCPID].f3Position; 

	return O;
}

[domain("quad")]
PS_RenderSceneInput DS_Quads( HS_ConstantOutput_Quad HSConstantData, const OutputPatch<Position_Input, 4> I, float2 uv : SV_DomainLocation )
{
	PS_RenderSceneInput O = (PS_RenderSceneInput)0;
	float3 f3Position; 

	float3 p[4]; 
	[unroll]
	for( uint i = 0; i < 4; i++ )
	{
		p[i] = I[i].f3Position;
	}
	f3Position = bilerpUV(p, uv);

	O.f4Position = float4( f3Position.xyz, 1.0 );
	//O.f4Position = mul(O.f4Position, cb_worldMatrix);

	O.TexCoord = float2( O.f4Position.x, -O.f4Position.z );
	
	
	f3Position.y = g_heightMap.SampleLevel( g_SamplerLinearWrap, O.TexCoord / 4096.0f, 0 ).r * g_hightMultipler;
	O.f4Position.y = f3Position.y;
	
	if( distance( f3Position, cb_cameraPosition ) < 20.0 )
	{
		//float3 normal = g_normalMap.SampleLevel( g_SamplerLinearWrap, O.TexCoord / 4096.0f, 0 ).rgb * 2.0f - 1.0f;
		//float microDetail = g_microHightMap.SampleLevel( g_SamplerLinearWrap, O.TexCoord * 0.5f, 0 ).r * 0.1;
		float microDetail = g_microNormalMap.SampleLevel( g_SamplerLinearWrap, O.TexCoord * 0.25f, 0 ).a * 0.3;
		O.f4Position.y += microDetail;
	}
	//O.f4Position.xyz += normalToTerrain(normal) * microDetail;
	O.f4Position = mul( O.f4Position, cb_viewMatrix );
	O.f4Position = mul( O.f4Position, cb_projectionMatrix );
	
	return O;
}



float3 NormalBlend_UnpackedRNM(float3 n1, float3 n2)
{
	n1 += float3(0, 0, 1);
	n2 *= float3(-1, -1, 1);
	
    return n1 * dot( n1, n2 ) / n1.z - n2;
}

PS_RenderOutput PS_SolidColor( PS_RenderSceneInput I )
{
	PS_RenderOutput O;

	O.f4Color = float4( 0.9f, 0.9f, 0.0f, 1.0f );

	float3 normal = g_normalMap.Sample( g_SamplerLinearWrap, I.TexCoord / 4096.0f ).rgb * 2.0f - 1.0f;
	//normal = normalToTerrain(normal, 5.0f);
	float3 microNormal = g_normalMap.Sample( g_SamplerLinearWrap, I.TexCoord * 0.25f ).rgb * 2.0f - 1.0f;
	//microNormal = normalToTerrain(microNormal, 5.0f);
	
	normal = NormalBlend_UnpackedRNM( normal, microNormal );
	
	normal = normalToTerrain(normal, 7.0f);

	float lit = dot( normalize( float3( 0.5f, 0.2f, -0.5f ) ), normal );

	O.f4Color.rgb = g_diffuseMap.Sample( g_SamplerLinearWrap, I.TexCoord * 0.25f ).rgb * lit;

	//O.f4Color.rgb = normal;

	return O;
}