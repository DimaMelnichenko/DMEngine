
cbuffer ConstantData : register(b0)
{
	float b_groupDim;
	float2 b_rect;
	float b_elapsedTime;
};

cbuffer ArgsBuffer : register(b1)
{
	int indexCountPerInstance;
	int instanceCount;
	int startIndexLocation;
	int baseVertexLocation;
	int startInstanceLocation;
	float3 padding;
};

struct GrassItem
{
    float3 position;
	float size;
};

RWByteAddressBuffer drawArgsBuffer : register(u0);
AppendStructuredBuffer<GrassItem> grassBuffer : register(u1);
Texture2D<float> terrainTexture : register(t0);

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
	
    /*if(index >= ( b_rect.x * b_rect.y ) )
		return;
	*/
	if( b_rect.x < dispatchThreadId.x || b_rect.y < dispatchThreadId.y )
		return;
	
	float2 texCoord = dispatchThreadId.xy;
	float4 height = terrainTexture[dispatchThreadId.xy] * 300.0;
	
	grassItem.position = float3( dispatchThreadId.x, height.x, dispatchThreadId.y );
	//grassItem.position = float3( groupID.x, 1.0f, groupID.y * 32 ) );
	grassItem.size = 0.3f;
	
	grassBuffer.Append(grassItem);
	
	drawArgsBuffer.InterlockedAdd(4, 1);
}