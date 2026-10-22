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
	nointerpolation float3 random : TEXCOORD2;	// случайные числа частицы 0…1: у клочка пены — поворот и узор
	// Клочок взбитой воды (froth) освещается в пикселе: свет тени (небо) — shade, а color — с солнцем; блик солнца на
	// пузырях — glint; направление к солнцу — toSun (0 — солнца нет)
	float3 shade : COLOR2;
	nointerpolation float3 glint : COLOR3;
	nointerpolation float3 toSun : TEXCOORD3;
	// Оси квада в мире (corner.x — вдоль axisRight, w — его растяжение вдоль течения; corner.y — вдоль axisUp): нормаль и
	// тень внутри клочка — в его осях
	nointerpolation float4 axisRight : TEXCOORD4;
	nointerpolation float3 axisUp : TEXCOORD5;
};

static const float minimumPixels = 1.5f;
// Клочок взбитой воды вытянут вдоль движения: 1 + скорость × frothStretch, не больше 1 + frothMaxStretch
static const float frothStretch = 0.6f;
static const float frothMaxStretch = 1.2f;
// Клочок сдвинут вниз по течению на такую долю своей полудлины: точка рождения — у его верхнего по течению края
static const float frothDownstream = 0.8f;
// Бурление под водой: видимость падает в e раз на такую глубину, м
static const float frothUnderwaterFade = 0.08f;
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
	float4 axisRight = float4( cb_viewInverseMatrix[0].xyz, 1.0f );
	float3 axisUp = cb_viewInverseMatrix[1].xyz;
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
		float3 right = cb_viewInverseMatrix[0].xyz;
		float3 up = cb_viewInverseMatrix[1].xyz;
		// Клочок взбитой воды вытянут вдоль течения (его горизонтальной скорости по плоскости экрана; всплытие не тянет) и
		// сдвинут вниз по течению: тянется от точки рождения у подошвы, а не лежит половиной на стенке и кромке выше
		float stretch = 1.0f;
		float3 shift = 0.0f;
		if( g_shape == particleShapeFroth )
		{
			const float3 flow = float3( particle.velocity.x, 0.0f, particle.velocity.z );
			const float3 along = flow - dot( flow, view ) * view;
			const float speed = length( along );
			if( speed > 0.05f )
			{
				right = along / speed;
				up = normalize( cross( view, right ) );
				stretch = 1.0f + min( speed * frothStretch, frothMaxStretch );
				shift = flow / max( length( flow ), 1e-4f ) * width * stretch * 0.5f * frothDownstream;
			}
		}
		offset = ( right * corner.x * stretch + up * corner.y ) * width * 0.5f + shift;
		axisRight = float4( right, stretch );
		axisUp = up;
	}
	const float3 position = particle.position + offset;

	// Свет: небо со всех сторон и солнце (или луна) с тенью; просвет — свет насквозь к камере, сильный против солнца. У
	// клочка взбитой воды солнце — по нормали в пикселе (particles.ps): здесь — свет с солнцем (как у обращённой к нему
	// стороны) и без него
	const float3 ambient = ambientIrradiance( float3( 0.0f, 1.0f, 0.0f ) );
	float3 light = ambient;
	float3 sunLight = 0.0f;
	float3 toSun = 0.0f;
	[branch] if( g_shadowSunIndex >= 0 )
	{
		const Light sun = g_lights[g_shadowSunIndex];
		toSun = -sun.direction;
		const float shadow = sunShadow( particle.position, toSun, toSun );
		const float forward = pow( saturate( dot( -view, toSun ) ), 6.0f );
		sunLight = sun.color * shadow;
		light += sunLight * ( ( g_shape == particleShapeFroth ? 1.0f : 0.6f ) + g_transmission * forward * 4.0f ) / PI;
	}
	const float3 color = applyFogging( g_color * light + g_emissive, particle.position ) * preExposure();

	ParticlePixelInput output;
	output.position = mul( float4( position, 1.0f ), cb_viewProjectionMatrix );
	output.color = color;
	// Поле вокруг камеры: к его краю частицы гаснут — рождения и гибель за краем не видны
	float edge = 1.0f;
	if( g_spawn == particleSpawnCamera || g_spawn == particleSpawnWater )
		edge = 1.0f - smoothstep( g_radius * 0.75f, g_radius, length( particle.position.xz - cb_cameraPosition.xz ) );
	// Бурление под водой видно слабо: проявляется, всплывая к поверхности
	float underwater = 1.0f;
	if( g_spawn == particleSpawnFall )
		underwater = exp( -max( particle.surface - particle.position.y, 0.0f ) / frothUnderwaterFade );
	output.alpha = g_alpha * particleFade( particle ) * coverage * edge * underwater;
	output.corner = corner;
	output.viewDepth = mul( float4( position, 1.0f ), cb_viewMatrix ).z;
	uint state = particle.seed ^ 0x9e3779b9u;
	output.random = float3( particleRandom( state ), particleRandom( state ), particleRandom( state ) );
	output.shade = g_shape == particleShapeFroth ? applyFogging( g_color * ambient + g_emissive, particle.position ) * preExposure() : color;
	output.glint = ( applyFogging( sunLight, particle.position ) - applyFogging( 0.0f, particle.position ) ) * preExposure();
	output.toSun = toSun;
	output.axisRight = axisRight;
	output.axisUp = axisUp;
	return output;
}
