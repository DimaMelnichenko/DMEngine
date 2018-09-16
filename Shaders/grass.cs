
#include "samplers.sh"
#include "common.vs"

static const float PI = 3.14159265f;

cbuffer ConstantData : register(b2)
{
	float b_groupDim;
	float2 b_rect;
	float b_elapsedTime;
};

cbuffer ArgsBuffer : register(b3)
{
	int indexCountPerInstance;
	int instanceCount;
	int startIndexLocation;
	int baseVertexLocation;
	int startInstanceLocation;
	float3 padding;
};

cbuffer PopulateParametersBuffer : register(b4)
{
	float nearBorderPopulate;
	float farBorderPopulate;
	float sizeMultiplerPopulate;
	float densityPopulate;
	float nearFallowPopulate;
	float farFallowPopulate;
	float2 dumbPopulateParametersBuffer;
};

struct GrassItem
{
    float3 position;
	float size;
	float4 rotation;
};

#define LOD_COUNT=1

RWByteAddressBuffer drawArgsBuffer : register(u0);
AppendStructuredBuffer<GrassItem> grassBuffer : register(u1);

Texture2D terrainTexture : register(t0);
Texture2D noiseTexture : register(t1);
Texture2D foilageMap : register(t2);

[numthreads(1, 1, 1)]
void init()
{
	drawArgsBuffer.Store4(0, uint4(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation));
    drawArgsBuffer.Store(16, startInstanceLocation);
}

#define THREAD_GROUP_X 32
#define THREAD_GROUP_Y 32
#define THREAD_GROUP_TOTAL 1024

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main( uint3 groupID : SV_GroupID, uint3 dispatchThreadId : SV_DispatchThreadID, uint groupIndex : SV_GroupIndex )
{
	GrassItem grassItem = (GrassItem)0;
	
	//const uint index = groupID.x * THREAD_GROUP_TOTAL + groupID.y * b_groupDim * THREAD_GROUP_TOTAL + groupIndex;
	
    /*
	if(index >= ( b_rect.x * b_rect.y ) )
		return;
	*/
	[flatten]
	if( b_rect.x < dispatchThreadId.x || b_rect.y < dispatchThreadId.y )
		return;
	
	float step = densityPopulate * 0.5;
	float2 texCoord = (float2)dispatchThreadId * step;
	
	float2 steppedCamPos = cb_cameraPosition.xz + ( normalize( cb_viewDirection.xz ) * b_rect * 0.5f * step );
	float scale2 = ( step * 2.0 );
	float2 mod = fmod( steppedCamPos, scale2 );
	mod.x += scale2 * ( 1.0 - max(sign(0 - mod.x), 0.0));
	mod.y += scale2 * ( 1.0 - max(sign(0 - mod.y), 0.0));
	steppedCamPos += float2( scale2, scale2 ) - mod;
	
	texCoord = texCoord - b_rect * 0.5f * step + steppedCamPos;
	//texCoord = texCoord + steppedCamPos;
	
	float2 noiseTexCoord = texCoord / 64.0;
	float4 noise = noiseTexture.SampleLevel( g_SamplerLinearWrap, noiseTexCoord, 0.0 );// * 2.0 - 1.0;
	grassItem.rotation.y = noise.z * 100.0;
	
	texCoord += noise.xy * 0.5;
	float2 offsetPosition = texCoord;
	texCoord = texCoord / 2048.0;
	texCoord.y = 1.0 - texCoord.y;
	
	float sizeMultipler = foilageMap.SampleLevel( g_SamplerLinearClamp, texCoord, 0.0 ).r;
	
	[flatten]
	if( sizeMultipler < 0.1 )
		return;
	
	float4 height = terrainTexture.SampleLevel( g_SamplerLinearClamp, texCoord, 0.0 ).r * 300.0;
	
	float3 grassPosition = { offsetPosition.x, height.x, offsetPosition.y };
	
	[flatten] 
	if( max( dot( cb_viewDirection, normalize( grassPosition - cb_cameraPosition ) ), 0.0 ) < 0.81
		|| distance( cb_cameraPosition, grassPosition ) > b_rect.x * step )
		return;
	
	grassItem.position = grassPosition;
	
	//grassItem.position = float3( groupID.x, 1.0f, groupID.y * 32 ) );
	grassItem.size = sizeMultiplerPopulate * sizeMultipler;//lerp( 0.001, 0.005f, clamp( noise.x, 0.0 , 1.0 ) );
	
	float distanceToGrass = distance( cb_cameraPosition, grassPosition );
	
	float farFallow = clamp( ( farBorderPopulate - distanceToGrass ) / farFallowPopulate, 0.0, 1.0 );
	float nearFallow = clamp( ( distanceToGrass - nearBorderPopulate ) / nearFallowPopulate, 0.0, 1.0 );
	grassItem.size *= farFallow * nearFallow;
	
	[flatten]
	if( distanceToGrass < nearBorderPopulate || distanceToGrass > farBorderPopulate )
	{
		return;
	}
	
	grassBuffer.Append(grassItem);	
	drawArgsBuffer.InterlockedAdd(4, 1);
}