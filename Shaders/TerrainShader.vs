////////////////////////////////////////////////////////////////////////////////
// Filename: terrain.vs
////////////////////////////////////////////////////////////////////////////////


/////////////
// GLOBALS //
/////////////
cbuffer MatrixBuffer
{
    matrix cb_worldMatrix;
    matrix cb_viewMatrix;
    matrix cb_projectionMatrix;
};

cbuffer TerrainBuffer
{
	float4 textureOffset;
	float4 terrain_params; // terrain_params.x - height multipler, terrain_params.w - scale
	float4 neighbors;
};

cbuffer TopologyBuffer
{
	float4 topology;
}

Texture2D terrainTexture[7];
SamplerState SampleHM;
SamplerState SampleType;


static const float textureSize = 512.0f;
static const float texelSize = 1.0f/textureSize; //size of one texel;
static const float normalStrength = 8;

//////////////
// TYPEDEFS //
//////////////
struct VertexInputType
{
    float4 position : POSITION;
    float2 tex : TEXCOORD0;
	//float3 normal : NORMAL0;
	//float4 instance_pos: INSTANCEPOS1;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
	float4 worldTex : TEXCOORD1;
	float4 depthPosition : TEXCOORD3;
};

float Height( float u, float v )
{
	return terrainTexture[0].SampleLevel( SampleHM, float2( u, v ), 0 ).r;
}

float4 fetchTexture( int idx, float2 tex_uv )
{
	return terrainTexture[idx].SampleLevel( SampleType, tex_uv, 0 );
}

////////////////////////////////////////////////////////////////////////////////
// Vertex Shader
////////////////////////////////////////////////////////////////////////////////
PixelInputType main(VertexInputType input)
{
    PixelInputType output;

    // Change the position vector to be 4 units for proper matrix calculations.
    input.position.w = 1.0f;
	
	output.position = input.position;		
	output.position.xz *= terrain_params.w;	
	
	float2 texHigth = input.tex * textureOffset.z + textureOffset;
	
	if( neighbors.r > 0.0 && input.position.z == 16.0 && ( fmod( input.position.x, 2.0f ) ) )
	{
		output.position.x -= terrain_params.w;
		texHigth.x -=  0.5 * ( input.tex.x * textureOffset.z + textureOffset.x );
	}
	
	output.tex = texHigth;	
	
	output.worldTex.xy = texHigth;
	
	float height = fetchTexture( 0, texHigth ).r;
	
	height *= terrain_params.x;
	//height += fetchTexture( 2, texHigth * textureSize ).r * 0.5f;
	
	output.position.y += height + topology.x;
	output.worldTex.z = output.position.y;
	

	
    // Calculate the position of the vertex against the world, view, and projection matrices.
	output.position = mul(output.position, cb_worldMatrix);
    output.position = mul(output.position, cb_viewMatrix);
    output.position = mul(output.position, cb_projectionMatrix);
	
	output.depthPosition = mul( input.position, cb_worldMatrix );
	
    return output;
}
	