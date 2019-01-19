
cbuffer FrameConstantBuffer : register( b0 )
{
	matrix cb_viewMatrix;
	matrix cb_viewInverseMatrix;
	matrix cb_projectionMatrix;
	matrix cb_viewProjectionMatrix;
	float3 cb_cameraPosition;
	float  cb_appTime;
	float3 cb_viewDirection;
	float  cb_elapsedTime;
	float  cb_lightCount;
	float3 fcb_dump;
};

SamplerState g_SamplerPointClamp : register(s0);
SamplerState g_SamplerPointWrap : register(s1);
SamplerState g_SamplerPointBorder : register(s2);
SamplerState g_SamplerLinearClamp : register(s3);
SamplerState g_SamplerLinearWrap : register(s4);
SamplerState g_SamplerLinearBorder : register(s5);
SamplerState g_SamplerAnisotropicClamp : register(s6);
SamplerState g_SamplerAnisotropicWrap : register(s7);

cbuffer WorldBuffer : register( b1 )
{
	matrix cb_worldMatrix;
}; 

cbuffer cbQuadTesselation : register( b2 )
{
	float4  g_f4TessFactors;            
	float2  g_f2Modes;
	float2	g_hightMultipler;
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
	O.f3Position.xz += posOffset;

	return O;    
}

bool inFrustum(const float3 pt, const float3 eyePos, const float3 viewDir, float margin)
{
	// conservative frustum culling
	float3 eyeToPt = pt - eyePos;
	float3 patch_to_camera_direction_vector = viewDir * dot(eyeToPt, viewDir) - eyeToPt;
	float3 patch_center_realigned = pt + normalize(patch_to_camera_direction_vector) * min(margin, length(patch_to_camera_direction_vector));
	float4 patch_screenspace_center = mul(float4(patch_center_realigned, 1.0), cb_worldMatrix);
	patch_screenspace_center = mul( patch_screenspace_center, cb_viewMatrix );
	patch_screenspace_center = mul( patch_screenspace_center, cb_projectionMatrix );
	

	if(((patch_screenspace_center.x/patch_screenspace_center.w > -1.0) && (patch_screenspace_center.x/patch_screenspace_center.w < 1.0) &&
		(patch_screenspace_center.y/patch_screenspace_center.w > -1.0) && (patch_screenspace_center.y/patch_screenspace_center.w < 1.0) &&
		(patch_screenspace_center.w>0)) || (length(pt-eyePos) < margin))
	{
		return true;
	}

	return false;
}

bool insideMap( float4 b ) 
{
	float4 a = { 0.0, 4096.0, 4096.0, 0.0 };
	return ( a.y < b.w || a.w > b.y || a.z < b.x || a.x > b.z );
}

HS_ConstantOutput_Quad HS_QuadsConstant( InputPatch<Position_Input, 4> I )
{
	HS_ConstantOutput_Quad O = (HS_ConstantOutput_Quad)0;

	float3 centre = 0.25 * ( I[0].f3Position + I[1].f3Position + I[2].f3Position + I[3].f3Position );
	float  sideLen = max(abs(I[1].f3Position.x - I[0].f3Position.x), abs(I[1].f3Position.x - I[2].f3Position.x)); // assume square & uniform
	float  diagLen = sqrt( 2.0f * sideLen * sideLen );

	//if( !inFrustum( centre, cb_cameraPosition, cb_viewDirection, diagLen ) )
	//float3 leftTop = mul( I[3].f3Position, cb_worldMatrix );
	//float3 botomRight = mul( I[1].f3Position, cb_worldMatrix );
	//if( insideMap( float4( leftTop.xz, botomRight.xz ) ) )
	//{
	//	O.fInsideTessFactor[0] = -1;
	//	O.fInsideTessFactor[1] = -1;
	//	O.fTessFactor[0] = -1;
	//	O.fTessFactor[1] = -1;
	//	O.fTessFactor[2] = -1;
	//	O.fTessFactor[3] = -1;
	//}
	//else
	{
		float2 ritf,itf; float4 rtf;
		uint mode = (uint)g_f2Modes.x;

		//Process2DQuadTessFactorsMax( float4( 1.0, 1.0, 1.0, 1.0 ), 1.0, rtf, ritf, itf);


		switch (mode)
		{
			case 0: Process2DQuadTessFactorsMax( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			case 1: Process2DQuadTessFactorsMin( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			case 2: Process2DQuadTessFactorsAvg( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
			default: Process2DQuadTessFactorsMax( g_f4TessFactors, g_f2Modes.y, rtf, ritf, itf);
				break;
		}

		O.fTessFactor[0] = rtf.x;
		O.fTessFactor[1] = rtf.y;
		O.fTessFactor[2] = rtf.z;
		O.fTessFactor[3] = rtf.w;
		O.fInsideTessFactor[0] = ritf.x;
		O.fInsideTessFactor[1] = ritf.y;
	}
	return O;
}

[domain("quad")]
[partitioning("fractional_even")]
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
	O.f4Position = mul(O.f4Position, cb_worldMatrix);

	O.TexCoord = float2( O.f4Position.x, -O.f4Position.z );

	O.f4Position.y = g_heightMap.SampleLevel( g_SamplerLinearWrap, O.TexCoord / 4096.0, 0 ).r * g_hightMultipler.x;
	O.f4Position = mul( O.f4Position, cb_viewMatrix );
	O.f4Position = mul( O.f4Position, cb_projectionMatrix );
	
	return O;
}

float3 normalToTerrain( float3 vec )
{
	vec.yz = vec.zy;
	vec.z *= -1.0f;
	vec.y *= ( 1.0 / 4.0 );
	return normalize( vec );
}

PS_RenderOutput PS_SolidColor( PS_RenderSceneInput I )
{
	PS_RenderOutput O;

	O.f4Color = float4( 0.9, 0.9, 0.0, 1.0 );

	float3 normal = g_normalMap.Sample( g_SamplerLinearWrap, I.TexCoord / 4096.0 ).rgb * 2.0 - 1.0;
	normal = normalToTerrain(normal);

	float lit = dot( normalize( float3( 0.5, 0.5, -0.5 ) ), normal );

	O.f4Color.rgb = g_diffuseMap.Sample( g_SamplerLinearWrap, I.TexCoord * 0.5 ).rgb * lit;

	//O.f4Color.rgb = normal;

	return O;
}