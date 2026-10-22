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
	float  landed;		// 1 — лежит на земле (needle); particleSurfaced — бурление всплыло (fall)
	float  surface;		// бурление (spawn fall): уровень воды под частицей, м — подъём к нему и видимость под водой
};

static const uint particleSpawnPoint = 0;
static const uint particleSpawnSphere = 1;
static const uint particleSpawnCamera = 2;
static const uint particleSpawnWater = 3;
static const uint particleSpawnFall = 4;
static const uint particleShapeDot = 0;
static const uint particleShapeNeedle = 1;
static const uint particleShapeFroth = 2;
// Бурление (spawn fall) всплыло к поверхности: дальше его несёт течение (у лежащей хвоинки landed — 1)
static const float particleSurfaced = 0.5f;

// Байты буфера состояния эмиттера (ParticleSystem::stateBytes): команда ExecuteIndirect {root-константа b9,
// IndexCountPerInstance, InstanceCount — живых, StartIndexLocation, BaseVertexLocation, StartInstanceLocation}, число
// команд, число мёртвых и наименьший индекс живой частицы в пуле (drawPool: рисуется пул с него до конца — места берутся
// с вершины стека мёртвых, то есть с конца пула; root-константа b9 — этот индекс)
static const uint particleStateInstances = 8;
static const uint particleStateCommandCount = 24;
static const uint particleStateDead = 28;
static const uint particleStateFirstAlive = 32;

#include "particle_emitter_constants.h"

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
// Размер частицы, м: с рождения — от birthSize до sizeStart за growTime (появление бурления — рост клочка, Dimok:
// «увеличение с 1 см до 20 см за 300 мс»), дальше до конца жизни — к sizeEnd; × случайная доля частицы
float particleSize( Particle particle )
{
	const float grow = min( g_growTime, particle.lifetime * 0.9f );
	const float size = particle.age < grow ? lerp( g_birthSize, g_sizeStart, particle.age / max( grow, 1e-4f ) ) :
											 lerp( g_sizeStart, g_sizeEnd, saturate( ( particle.age - grow ) / max( particle.lifetime - grow, 1e-4f ) ) );
	return size * particle.size;
}

float particleFade( Particle particle )
{
	const float t = saturate( particle.age / max( particle.lifetime, 1e-4f ) );
	return saturate( t / max( g_fadeIn, 1e-4f ) ) * saturate( ( 1.0f - t ) / max( g_fadeOut, 1e-4f ) );
}

#endif
