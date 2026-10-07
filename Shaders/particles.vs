////////////////////////////////////////////////////////////////////////////////
// Отрисовка частиц (ParticleSystem): экземпляр — живая частица из списка кадра (particles.cs), вершина — угол
// квада (GridMesh 2 × 2). Пятно (dot) повёрнуто к камере, хвоинка (needle) — узкая карточка вдоль скорости (у
// лежащей — вдоль своего направления по земле). Свет считается в вершине — частица мала: небо гармониками, солнце
// с тенью в центре частицы и просвет к солнцу (пыльца светится против солнца), воздушная перспектива
////////////////////////////////////////////////////////////////////////////////

#include "lighting.sh"
#include "particles.sh"
#include "bindless.sh"

DM_SRV( StructuredBuffer<Particle>, g_particles, 0 );
DM_SRV( StructuredBuffer<uint>, g_alive, SLOT_INSTANCE_DATA );

struct VertexInputType
{
	float3 position : POSITION;		// (0 | 1, 0, 0 | 1)
	uint instanceId : SV_InstanceID;
};

struct ParticlePixelInput
{
	float4 position : SV_POSITION;
	float3 color : COLOR0;			// свет × альбедо, с экспозицией
	float  alpha : COLOR1;
	float2 corner : TEXCOORD0;		// −1…1 по квадрату
	float  viewDepth : TEXCOORD1;	// z вида — мягкий край по глубине сцены
};

static const float minimumPixels = 1.5f;
static const float needleWidth = 0.15f;	// ширина хвоинки — доля её длины	// тоньше пары пикселей частица мерцает — шире, но прозрачнее

ParticlePixelInput main( VertexInputType input )
{
	const Particle particle = g_particles[g_alive[input.instanceId]];
	const float2 corner = input.position.xz * 2.0f - 1.0f;
	const float t = saturate( particle.age / max( particle.lifetime, 1e-4f ) );
	const float size = lerp( g_sizeStart, g_sizeEnd, t ) * particle.size;
	const float3 toCamera = cb_cameraPosition - particle.position;
	const float distance = max( length( toCamera ), 1e-3f );
	const float3 view = toCamera / distance;

	// Не мельче пары пикселей: иначе мелкая частица вдали мерцает; ширина растёт, прозрачность — обратно
	const float pixelSize = 2.0f * distance / ( cb_projectionMatrix[1][1] * g_viewportHeight );
	float width = g_shape == particleShapeNeedle ? size * needleWidth : size;
	float coverage = 1.0f;
	if( width < pixelSize * minimumPixels )
	{
		coverage = width / ( pixelSize * minimumPixels );
		width = pixelSize * minimumPixels;
	}

	float3 offset;
	if( g_shape == particleShapeNeedle )
	{
		// Ось — по скорости; у лежащей и медленной — направление из зерна, лёжа на земле
		uint state = particle.seed;
		const float angle = particleRandom( state ) * 6.2831853f;
		float3 axis = float3( cos( angle ), particle.landed > 0.0f ? 0.0f : 0.6f, sin( angle ) );
		if( particle.landed == 0.0f && length( particle.velocity ) > 0.05f )
			axis = particle.velocity;
		axis = normalize( axis );
		const float3 side = normalize( cross( axis, view ) + 1e-5f );
		offset = axis * corner.y * size * 0.5f + side * corner.x * width * 0.5f;
	}
	else
	{
		const float3 right = cb_viewInverseMatrix[0].xyz;
		const float3 up = cb_viewInverseMatrix[1].xyz;
		offset = ( right * corner.x + up * corner.y ) * width * 0.5f;
	}
	const float3 position = particle.position + offset;

	// Свет: небо со всех сторон и солнце (или луна) с тенью; просвет — свет насквозь к камере, сильный против солнца
	float3 light = ambientIrradiance( float3( 0.0f, 1.0f, 0.0f ) );
	[branch] if( g_shadowSunIndex >= 0 )
	{
		const Light sun = g_lights[g_shadowSunIndex];
		const float3 toSun = -sun.direction;
		const float shadow = sunShadow( particle.position, toSun, toSun );
		const float forward = pow( saturate( dot( -view, toSun ) ), 6.0f );
		light += sun.color * shadow * ( 0.6f + g_transmission * forward * 4.0f ) / PI;
	}
	const float3 color = applyAerialPerspective( g_color * light + g_emissive, particle.position ) * preExposure();

	ParticlePixelInput output;
	output.position = mul( float4( position, 1.0f ), cb_viewProjectionMatrix );
	output.color = color;
	// Поле вокруг камеры: к его краю частицы гаснут — рождения и гибель за краем не видны
	float edge = 1.0f;
	if( g_spawn == particleSpawnCamera || g_spawn == particleSpawnWater )
		edge = 1.0f - smoothstep( g_radius * 0.75f, g_radius, length( particle.position.xz - cb_cameraPosition.xz ) );
	output.alpha = g_alpha * particleFade( particle ) * coverage * edge;
	output.corner = corner;
	output.viewDepth = mul( float4( position, 1.0f ), cb_viewMatrix ).z;
	return output;
}
