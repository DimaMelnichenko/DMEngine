
cbuffer ContantData : register(b0)
{
	float group_dim;
	float max_particles;
	float elapsed_time;
};

struct BufferType
{
    float4 position;
	float4 velocity;
};

StructuredBuffer<BufferType> Buffer0 : register(t0);
RWStructuredBuffer<BufferType> BufferOut : register(u0);

#define THREAD_GROUP_X 32
#define THREAD_GROUP_Y 24
#define THREAD_GROUP_TOTAL 768

float3 _calculate(float3 anchor, float3 position)
{
	float3 direction = anchor - position;
	float distance = length(direction);
	direction /= distance;

	return direction * max( 0.001, ( 1 / ( distance * distance ) ) );
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main( uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex )
{
	uint index = groupID.x * THREAD_GROUP_TOTAL + groupID.y * group_dim * THREAD_GROUP_TOTAL + groupIndex;
	
	if( index >= max_particles )
		return;
	
	//BufferOut[index].position = Buffer0[index].position;
	
    BufferType data = Buffer0[index];
	
	//data.velocity.xyz += _calculate( float3(20.0, 0.0, 20.0), data.position.xyz ).xyz;
	//data.velocity.xyz += _calculate(-float3(20.0, 0.0, 20.0), data.position.xyz ).xyz;
	
	data.position += data.velocity * elapsed_time * 0.001;
	
	BufferOut[index] = data;
}