
cbuffer ContantData : register(b0)
{
	float b_groupDim;
	float2 b_rect;
	float b_elapsedTime;
};

struct Particle
{
    float3 position;
	float3 velocity;
	float2 dummy;
};

//StructuredBuffer<BufferType> Buffer0 : register(t0);
RWStructuredBuffer<Particle> Particles : register(u0);

#define THREAD_GROUP_X 32
#define THREAD_GROUP_Y 32
#define THREAD_GROUP_TOTAL 1024

float3 calculate(float3 anchor, float3 position)
{
	float3 direction = anchor - position;
	float distance = length(direction);
	direction /= distance;

	return direction * max( 0.001, ( 1.0 / ( distance * distance ) ) );
}

[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main( uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex )
{
	//uint index = dtID.x * THREAD_GROUP_TOTAL + dtID.y;
	uint index = groupID.x * THREAD_GROUP_TOTAL + groupID.y * b_groupDim * THREAD_GROUP_TOTAL + groupIndex;
	
	if( index >= b_rect.x )
		return;
	
	Particle particle = Particles[index];

	float3 position = particle.position;
	float3 velocity = particle.velocity;
	
	float3 attractor = float3( 500.0, 300.0, 500.0 );

	velocity += calculate(attractor, position);
	velocity += calculate(-attractor/ 2.0, position);
	
	particle.position = position + velocity * b_elapsedTime;
	particle.velocity = velocity;

	Particles[index] = particle;
}