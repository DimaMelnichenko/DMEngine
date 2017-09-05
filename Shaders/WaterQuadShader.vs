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
	float3 cameraPosition;
	float mb_padding;
};

cbuffer TerrainBuffer
{
	float4 textureOffset;
	float height_multipler;
	float elapsedTime;
	float sumTime;
	float scale;
};

cbuffer TopologyBuffer
{
	float4 topology;
}

// Per draw call constants
cbuffer cbChangePerCall : register(b4)
{
	// Transform matrices
	float4x4	g_matLocal;
	float4x4	g_matWorldViewProj;

	// Misc per draw call constants
	float2		g_UVBase;
	float2		g_PerlinMovement;
	float3		g_LocalEye;
}

Texture2D colorTexture[7];
SamplerState SampleHM;
SamplerState SampleType;

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float2 position : POSITION;
    //float2 tex : TEXCOORD0;
	//float3 normal : NORMAL0;
	//float4 instance_pos: INSTANCEPOS1;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
	float4 worldTex : TEXCOORD1;	
	float3 normal : TEXCOORD2;
	float4 depthPosition : TEXCOORD3;
	float3 worldPosition : TEXCOORD4;
	float3 camPos : TEXCOORD5;
};

float Height( float u, float v )
{
	return colorTexture[0].SampleLevel( SampleHM, float4( u, v, 0.0, 0.0 ), 0 ).r;
}

float4 fetchTexture( int idx, float2 tex_uv )
{
	return colorTexture[idx].SampleLevel( SampleType, float4( tex_uv.x, tex_uv.y, 0.0, 0.0 ), 0 );
}

float hash( float2 p )
{
	float h = dot( p, float2( 127.1, 311.7 ) );	
    return frac( sin(h) * 43758.5453123 );
}

float noise( float p )
{
    float2 i = floor( p );
    float2 f = frac( p );	
	float2 u = f * f * ( 3.0 - 2.0 * f );
    return -1.0 + 2.0 * lerp( 
							lerp( hash( i + float2( 0.0,0.0 ) ), hash( i + float2(1.0,0.0) ), u.x), lerp( hash( i + float2(0.0,1.0) ), 
						hash( i + float2(1.0,1.0) ), u.x), u.y);
}

static const float SEA_CHOPPY = 4.0;
static const float SEA_SPEED = 0.0008;
static const float SEA_FREQ = 0.16;
static const float SEA_HEIGHT = 0.6;
static const int ITER_GEOMETRY = 3;



float sea_octave( float2 uv, float choppy)
{
    uv.xy += noise(uv);        
    float2 wv = 1.0 - abs( sin( uv ) );
    float2 swv = abs( cos( uv ) );
    wv = lerp( wv, swv, wv );
    return pow( 1.0 - pow( wv.x * wv.y, 0.65f ), choppy );
}

float map(float3 p)
{
	float2x2 octave_m = float2x2(1.6,1.2,-1.2,1.6);
	float SEA_sumTime = sumTime * SEA_SPEED;
    float freq = SEA_FREQ;
    float amp = SEA_HEIGHT;
    float choppy = SEA_CHOPPY;
    float2 uv = p.xz; 
	uv.x *= 0.75;
    
    float d, h = 0.0;    
    for( int i = 0; i < ITER_GEOMETRY; i++ )
	{
    	d = sea_octave((uv+SEA_sumTime)*freq,choppy);
    	d += sea_octave((uv-SEA_sumTime)*freq,choppy);
        h += d * amp;        
    	uv = mul( octave_m, uv ); 
		freq *= 1.9; 
		amp *= 0.22;
        choppy = lerp( choppy, 1.0, 0.2 );
    }
    return p.y + h;
}


////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;
	
	// Local position
	float4 pos_local = mul(float4(input.position.xy, 0, 1), g_matLocal);
	
	// UV
	//float2 uv_local = pos_local.xy * g_UVScale + g_UVOffset;
	float2 uv_local = pos_local.xy * ( 1.0f / 20.0f);
	
	
	/*
	output.position = pos_local;
	//output.position = mul(output.position, worldMatrix);
    output.position = mul(output.position, viewMatrix);
    output.position = mul(output.position, projectionMatrix);
	*/
	//output.position = mul(pos_local, g_matWorldViewProj);
	
	//return output;
	
	float2 texHigth = uv_local;
	
	output.tex = texHigth;	
	
	output.worldTex.xy = texHigth;
	output.worldTex.w = height_multipler;	
	

	// positions calc
    //float4 position;
	//position = input.position;
	//position.w = 1.0f;
	
	//position.xz *= scale;
	
	// in world space
	//position = mul( position, worldMatrix );	
	
	//position.y += topology.x; // add waterlevel
	
	//output.worldPosition = position.xyz;
	
	//float ground = fetchTexture( 0, texHigth );
	
	//pos_local.z += lerp( map( pos_local.xzy ), 0, 0 ); // add walves
	
	output.camPos = cameraPosition;
	
	//output.worldTex.z = pos_local.y;
	
	output.position = mul(pos_local, g_matWorldViewProj);

    return output;
}
	