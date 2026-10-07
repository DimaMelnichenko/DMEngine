////////////////////////////////////////////////////////////////////////////////
// Частицы (ParticleSystem, docs/particles.md): частица пула эмиттера и константы эмиттера — общее для симуляции
// (particles.cs) и отрисовки (particles.vs, particles.ps)
////////////////////////////////////////////////////////////////////////////////

#ifndef PARTICLES_SH
#define PARTICLES_SH

#include "slots.h"

// Частица пула — раскладка ParticleSystem::Particle (48 байт). Мёртвая — age ≥ lifetime
struct Particle
{
	float3 position;
	float  age;			// с
	float3 velocity;	// м/с
	float  lifetime;	// с
	float  size;		// случайная доля размера, 0,7…1
	uint   seed;		// случайное число частицы: поворот, направление лежащей хвоинки
	float  landed;		// 1 — лежит на земле (needle)
	float  padding;
};

static const uint particleSpawnPoint = 0;
static const uint particleSpawnSphere = 1;
static const uint particleSpawnCamera = 2;
static const uint particleSpawnWater = 3;
static const uint particleShapeDot = 0;
static const uint particleShapeNeedle = 1;

// Байты буфера состояния эмиттера (ParticleSystem::stateBytes): команда ExecuteIndirect {root-константа b9,
// IndexCountPerInstance, InstanceCount — живых, StartIndexLocation, BaseVertexLocation, StartInstanceLocation}, число
// команд и число мёртвых
static const uint particleStateInstances = 8;
static const uint particleStateCommandCount = 24;
static const uint particleStateDead = 28;

// Раскладка — ParticleSystem::EmitterParameters
cbuffer ParticleEmitterBuffer : register( b4 )
{
	float3 g_origin;			// экземпляр, м мира
	uint   g_spawn;				// particleSpawn*
	float  g_radius;
	float  g_heightMin;
	float  g_heightMax;
	uint   g_capacity;			// частиц в пуле
	uint   g_spawnCount;		// попыток рождения в этом кадре
	uint   g_frame;				// номер шага симуляции — зерно случайных чисел
	uint   g_emitterIndex;
	float  g_timeStep;			// с
	float  g_lifetimeMin;
	float  g_lifetimeMax;
	float  g_sizeStart;
	float  g_sizeEnd;
	float3 g_color;
	float  g_alpha;
	float3 g_velocity;
	float  g_velocitySpread;
	float  g_gravity;
	float  g_drag;
	float  g_wind;
	float  g_curl;
	float  g_curlScale;
	float  g_waterFlow;
	float  g_waterSpeed;
	uint   g_collide;
	float  g_fadeIn;
	float  g_fadeOut;
	float  g_transmission;
	float  g_emissive;
	uint   g_shape;				// particleShape*
	uint   g_hasMask;
	uint   g_hasTerrain;
	float  g_viewportHeight;	// пикселей кадра по вертикали — наименьший размер частицы на экране
};

uint particleHash( uint value )
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return value;
}

// Случайное число 0…1 и следующее состояние
float particleRandom( inout uint state )
{
	state = particleHash( state + 0x9e3779b9u );
	return ( state >> 8 ) * ( 1.0f / 16777216.0f );
}

// Видимость частицы по жизни: появление и угасание долями жизни
float particleFade( Particle particle )
{
	const float t = saturate( particle.age / max( particle.lifetime, 1e-4f ) );
	return saturate( t / max( g_fadeIn, 1e-4f ) ) * saturate( ( 1.0f - t ) / max( g_fadeOut, 1e-4f ) );
}

#endif
