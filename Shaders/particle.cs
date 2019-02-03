
#include "samplers.sh"
#include "common.vs"


cbuffer ThreadsData : register(b2)
{
	float b_groupDim;
	float2 b_rect;
	float b_elapsedTime;
};

cbuffer ParticleParameters : register(b3)
{
	float4 g_heightMultipler;
	float4 g_highOfDeath;
};

struct Particle
{
    float3 position;
	float3 velocity;
	float2 dummy;
};

Texture2D heightMap : register(t0);

RWStructuredBuffer<Particle> Particles : register(u0);

#define THREAD_GROUP_X 32
#define THREAD_GROUP_Y 32
#define THREAD_GROUP_TOTAL 1024


[numthreads(THREAD_GROUP_X, THREAD_GROUP_Y, 1)]
void main( uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex, uint3 dispatchThreadId : SV_DispatchThreadID )
{
	//uint index = dtID.x * THREAD_GROUP_TOTAL + dtID.y;
	uint index = groupID.x * THREAD_GROUP_TOTAL + groupID.y * b_groupDim * THREAD_GROUP_TOTAL + groupIndex;
	
	if( index >= b_rect.x )
		return;
	
	Particle particle = Particles[index];

	float3 position = particle.position;
	float3 velocity = particle.velocity;
	
	particle.position = position + velocity * b_elapsedTime;
	
	float2 texCoord = position.xz / 4096.0f;
	texCoord.y = 1.0 - texCoord.y;
	float height = heightMap.SampleLevel( g_SamplerLinearClamp, texCoord, 0.0 ).r * g_heightMultipler.x;
	
	if( particle.position.y >= ( height + g_highOfDeath.x ) || particle.position.y < height - 0.1 )
		particle.position.y = height;
	
	//particle.velocity = velocity;

	Particles[index] = particle;
}