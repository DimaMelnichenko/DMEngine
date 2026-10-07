////////////////////////////////////////////////////////////////////////////////
// Симуляция частиц эмиттера на GPU (ParticleSystem, docs/particles.md). Пул частиц, стек мёртвых индексов и список
// живых кадра — без перестройки на CPU:
//   mainInit   — при создании: все частицы мертвы, стек мёртвых — весь пул;
//   mainReset  — каждый кадр: команда косвенного вызова без экземпляров;
//   mainEmit   — попытки рождения кадра: место по форме эмиттера (поле вокруг камеры — по маске плотности, быстрая
//                вода — по полю скорости симуляции), принятая попытка берёт индекс со стека мёртвых;
//   mainUpdate — все частицы пула: тяжесть, сопротивление к скорости воздуха (ветер уровня, вихри curl noise) и воды,
//                столкновение с рельефом и водой; умершая — на стек мёртвых, живая — в список живых и в число экземпляров
//                косвенного вызова
////////////////////////////////////////////////////////////////////////////////

#include "common.vs"
#include "particles.sh"
#include "terrain_height.sh"
#include "water.sh"
#include "wind_field.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_densityMask, 2 );		// плотность рождения (camera), UV карты высот

DM_UAV( RWStructuredBuffer<Particle>, g_particles, 0 );
DM_UAV( RWStructuredBuffer<uint>, g_dead, 1 );		// стек мёртвых индексов, вершина — particleStateDead
DM_UAV( RWByteAddressBuffer, g_state, 2 );			// команда косвенного вызова и счётчики (particles.sh)
DM_UAV( RWStructuredBuffer<uint>, g_alive, 3 );		// живые кадра — экземпляры вызова

[numthreads( 64, 1, 1 )]
void mainInit( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_capacity )
		return;
	g_particles[id.x] = (Particle)0;
	g_dead[id.x] = id.x;
	if( id.x == 0 )
		g_state.Store( particleStateDead, g_capacity );
}

[numthreads( 1, 1, 1 )]
void mainReset()
{
	g_state.Store( 0, 0 );
	g_state.Store4( 4, uint4( 6, 0, 0, 0 ) );
	g_state.Store( 20, 0 );
	g_state.Store( particleStateCommandCount, 1 );
}

static const float minSprayDepth = 0.01f;	// м: мельче — брызгам не из чего родиться

// Случайная точка в круге радиуса radius
float2 randomDisk( inout uint state, float radius )
{
	const float angle = particleRandom( state ) * 6.2831853f;
	return float2( cos( angle ), sin( angle ) ) * radius * sqrt( particleRandom( state ) );
}

float3 randomDirection( inout uint state )
{
	const float z = particleRandom( state ) * 2.0f - 1.0f;
	const float angle = particleRandom( state ) * 6.2831853f;
	const float r = sqrt( max( 1.0f - z * z, 0.0f ) );
	return float3( r * cos( angle ), z, r * sin( angle ) );
}

[numthreads( 64, 1, 1 )]
void mainEmit( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_spawnCount )
		return;
	uint state = particleHash( particleHash( g_emitterIndex * 7919u + g_frame ) ^ id.x );

	// Место рождения — до того, как брать индекс: отвергнутая попытка пул не трогает
	float3 position = g_origin;
	float3 velocity = g_velocity;
	if( g_spawn == particleSpawnSphere )
		position += randomDirection( state ) * g_radius * pow( particleRandom( state ), 1.0f / 3.0f );
	else if( g_spawn == particleSpawnCamera || g_spawn == particleSpawnWater )
	{
		if( g_hasTerrain == 0 )
			return;
		const float2 xz = cb_cameraPosition.xz + randomDisk( state, g_radius );
		const float2 uv = terrainUV( xz );
		if( any( uv < 0.0f ) || any( uv > 1.0f ) )
			return;
		const WaterSample water = sampleWater( uv );
		float chance;
		float base = terrainHeight( xz );
		if( g_spawn == particleSpawnCamera )
		{
			// Плотность маски, без воды (пыльца и хвоя не рождаются над озером)
			chance = ( g_hasMask != 0 ? g_densityMask.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ) : 1.0f ) *
					 ( 1.0f - waterFoliageClear( uv ) );
		}
		else
		{
			// Брызги — на воде быстрее порога, чаще с ростом скорости; летят вверх и вдоль течения
			const float speed = length( water.velocity );
			chance = water.depth > minSprayDepth ? saturate( ( speed - g_waterSpeed ) / max( g_waterSpeed, 0.1f ) ) : 0.0f;
			base += water.depth;
			velocity += float3( water.velocity.x, 0.0f, water.velocity.y ) * g_waterFlow;
		}
		if( particleRandom( state ) >= chance )
			return;
		position = float3( xz.x, base + lerp( g_heightMin, g_heightMax, particleRandom( state ) ), xz.y );
	}
	velocity += randomDirection( state ) * g_velocitySpread * particleRandom( state );

	// Индекс со стека мёртвых; пусто — вернуть счётчик
	uint top;
	g_state.InterlockedAdd( particleStateDead, 0xffffffffu, top );
	if( top == 0 || top > g_capacity )
	{
		g_state.InterlockedAdd( particleStateDead, 1u );
		return;
	}
	const uint index = g_dead[top - 1];

	Particle particle;
	particle.position = position;
	particle.age = 0.0f;
	particle.velocity = velocity;
	particle.lifetime = lerp( g_lifetimeMin, g_lifetimeMax, particleRandom( state ) );
	particle.size = lerp( 0.7f, 1.0f, particleRandom( state ) );
	particle.seed = state;
	particle.landed = 0.0f;
	particle.padding = 0.0f;
	g_particles[index] = particle;
}

// Тяжелее этого (тяжесть, м/с²) частица при касании земли или воды гибнет, легче — отскакивает (пушинки)
static const float heavyGravity = 1.0f;

// Вихри: ротор векторного потенциала из синусов (две октавы) — поле без расхождения, частицы кружат, а не
// сбиваются в кучи
float3 curlNoise( float3 p )
{
	const float t = cb_gameTime * 0.2f;
	p /= max( g_curlScale, 0.01f );
	float3 curl = 0.0f;
	[unroll] for( int octave = 0; octave < 2; ++octave )
	{
		const float s = octave == 0 ? 1.0f : 2.3f;
		const float3 q = p * s;
		const float dAxdy = 1.3f * cos( 1.3f * q.y + t + 0.7f * octave );
		const float dAxdz = 0.7f * cos( 0.7f * q.z + 1.1f );
		const float dAydz = 1.1f * cos( 1.1f * q.z + 0.8f * t + 2.3f * octave );
		const float dAydx = 0.9f * cos( 0.9f * q.x + 2.3f );
		const float dAzdx = 1.2f * cos( 1.2f * q.x + 1.2f * t + 1.7f * octave );
		const float dAzdy = 0.8f * cos( 0.8f * q.y + 0.4f );
		curl += float3( dAzdy - dAydz, dAxdz - dAzdx, dAydx - dAxdy ) / s;
	}
	return curl * 0.4f;
}

[numthreads( 64, 1, 1 )]
void mainUpdate( uint3 id : SV_DispatchThreadID )
{
	if( id.x >= g_capacity )
		return;
	Particle particle = g_particles[id.x];
	if( particle.age >= particle.lifetime )
		return;

	const float dt = g_timeStep;
	particle.age += dt;
	bool alive = particle.age < particle.lifetime;

	float ground = -1e9f;
	float waterDepth = 0.0f;
	float2 waterVelocity = 0.0f;
	if( g_hasTerrain != 0 )
	{
		const float2 uv = terrainUV( particle.position.xz );
		ground = terrainHeight( particle.position.xz );
		const WaterSample water = sampleWater( uv );
		waterDepth = water.depth;
		waterVelocity = water.velocity;
	}

	if( alive && particle.landed == 0.0f )
	{
		// Скорость воздуха: ветер уровня с порывами (без ветра, -nowind, — штиль) и вихри; у воды — её течение
		const float3 wind = float3( cb_windDirection.x, 0.0f, cb_windDirection.y ) * cb_windSpeed *
							windGust( particle.position.xz ) * ( cb_windStrength > 0.0f ? g_wind : 0.0f );
		float3 target = wind + curlNoise( particle.position ) * g_curl;
		if( waterDepth > 0.01f && particle.position.y < ground + waterDepth + 0.5f )
			target += float3( waterVelocity.x, 0.0f, waterVelocity.y ) * g_waterFlow;
		particle.velocity += ( target - particle.velocity ) * ( 1.0f - exp( -g_drag * dt ) );
		particle.velocity.y -= g_gravity * dt;
		particle.position += particle.velocity * dt;

		// Земля и вода: хвоинка ложится на землю и тонет в воде, тяжёлые капли (брызги) гибнут, лёгкие пушинки
		// отскакивают вверх — вихри у земли не губят их раньше срока
		if( g_collide != 0 && particle.position.y < ground + waterDepth )
		{
			if( g_shape == particleShapeNeedle && waterDepth < 0.01f )
			{
				particle.position.y = ground + 0.01f;
				particle.velocity = 0.0f;
				particle.landed = 1.0f;
			}
			else if( g_shape == particleShapeDot && g_gravity < heavyGravity )
			{
				particle.position.y = ground + waterDepth;
				particle.velocity.y = abs( particle.velocity.y );
			}
			else
				alive = false;
		}
	}

	// Поле вокруг камеры: ушедшие за его край гибнут — на их место родятся новые
	if( ( g_spawn == particleSpawnCamera || g_spawn == particleSpawnWater ) &&
		length( particle.position.xz - cb_cameraPosition.xz ) > g_radius * 1.1f )
		alive = false;

	if( !alive )
	{
		particle.age = particle.lifetime;
		g_particles[id.x] = particle;
		uint top;
		g_state.InterlockedAdd( particleStateDead, 1u, top );
		g_dead[top] = id.x;
		return;
	}
	g_particles[id.x] = particle;
	uint slot;
	g_state.InterlockedAdd( particleStateInstances, 1u, slot );
	g_alive[slot] = id.x;
}
