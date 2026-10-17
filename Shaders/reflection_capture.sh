////////////////////////////////////////////////////////////////////////////////
// Карты отражений места — как Sphere Reflection Capture в UE: кубическая карта сцены, снятая из точки (ReflectionCaptures,
// съёмка сцены без неба — docs/scene_capture.md), с префильтром по шероховатости, как у освещения неба (мипы —
// specularMipCount). Альфа — покрытие сценой: цвет умножен на неё, где съёмка видела небо — 0. Рядом — расстояния от
// точки съёмки до того, что она видит. В кадре — две ближайшие к камере карты; точка воды берёт ту, в чьём радиусе влияния
// она глубже (обе полностью — у которой точка съёмки ближе). Внутри радиуса отражения окружения ищут попадание лучом по
// карте (distance impostors, Szirmay-Kalos, Aszódi, Lazányi, Premecz 2005, «Approximate Ray-Tracing on the GPU with
// Distance Impostors»): луч отражения из точки воды идёт шагами, растущими с расстоянием, и на каждом сравнивает, дальше ли он от
// точки съёмки, чем видимое ею в этом направлении; ушёл за видимое — уточнение делением пополам и секущей, и попадание —
// там, где луч упёрся в поверхность. Так в отражении на своих местах и близкие предметы (шар, закрытый на экране другим),
// и дальние берега. Луч, ушедший за предмет далеко (за ним из точки съёмки ничего не видно), ищет дальше. Сквозь
// непокрытое и где луч ни во что не упёрся — небо освещения окружением по направлению самого луча (как сочетание карт
// отражений со Sky Light в UE). Ресурсы — t131…t134 (цвет и расстояния двух
// карт), константы — cb_reflectionCaptureSphere / cb_reflectionCapture. docs/reflection_captures.md
////////////////////////////////////////////////////////////////////////////////

#ifndef REFLECTION_CAPTURE_SH
#define REFLECTION_CAPTURE_SH

#include "ibl.sh"

DM_SRV( TextureCube, g_reflectionCapture0, SLOT_REFLECTION_CAPTURE_0 );
DM_SRV( TextureCube<float>, g_reflectionCaptureDistance0, SLOT_REFLECTION_CAPTURE_DISTANCE_0 );
DM_SRV( TextureCube, g_reflectionCapture1, SLOT_REFLECTION_CAPTURE_1 );
DM_SRV( TextureCube<float>, g_reflectionCaptureDistance1, SLOT_REFLECTION_CAPTURE_DISTANCE_1 );

static const uint reflectionCaptureSteps = 24;			// шагов луча по карте
static const float reflectionCaptureFirstStep = 0.5f;	// первый шаг, м; дальше каждый — в reflectionCaptureStepGrowth раз длиннее
static const float reflectionCaptureStepGrowth = 1.43f;	// 24 шага — до ~2 км
static const uint reflectionCaptureRefineSteps = 6;		// деление пополам у попадания, затем секущая
// Луч за видимым из точки съёмки дальше этого — прошёл позади предмета, а не упёрся в него: метры и доля расстояния. На
// поверхности, которую точка съёмки видит вскользь (низ шара), соседние тексели расходятся на D · θ / tg α (θ — тексель, при
// 256 на грань 0,006 рад): при α ≈ 1,5° — на четверть расстояния; край предмета (шар в 12 м перед берегом в сотне) — много
// больше. Меньший допуск — у такой поверхности попадания через тексель отбрасываются, в отражении полосы
static const float reflectionCaptureThickness = 0.5f;
static const float reflectionCaptureThicknessScale = 0.25f;

// Насколько точка q (от точки съёмки) дальше видимого ею в своём направлении, м: > 0 — за поверхностью; небо в карте —
// миллион метров. Расстояния — без смешивания соседних текселей: у края предмета их среднее — несуществующая поверхность
// между ним и фоном, лучи, проходящие мимо предмета, упирались бы в его край (серый клин в отражении)
float reflectionCaptureBehind( TextureCube<float> distances, float3 q, out float seen )
{
	seen = distances.SampleLevel( g_SamplerPointClamp, q, 0.0f );
	return length( q ) - seen;
}

// Луч из точки offset (от точки съёмки) в направлении r по карте: направление на попадание из точки съёмки; found — луч
// упёрся в поверхность (нет — ни во что не упёрся или прошёл только позади предметов: что за ними, карта не знает — небо)
float3 reflectionCaptureTrace( TextureCube<float> distances, float3 offset, float3 r, out bool found )
{
	found = false;
	float previous = 0.0f;
	float previousBehind = -1.0f;
	float t = reflectionCaptureFirstStep;
	[loop] for( uint i = 0; i < reflectionCaptureSteps; ++i )
	{
		float seen;
		const float behind = reflectionCaptureBehind( distances, offset + r * t, seen );
		if( behind > 0.0f && previousBehind <= 0.0f )
		{
			// Ушёл за видимое: место, где луч пересёк его, — делением пополам между прошлым шагом и этим, затем секущей по
			// тому, насколько луч перед поверхностью и за ней
			float tNear = previous;
			float tFar = t;
			float behindNear = previousBehind;
			float behindFar = behind;
			[loop] for( uint k = 0; k < reflectionCaptureRefineSteps; ++k )
			{
				const float middle = 0.5f * ( tNear + tFar );
				float middleSeen;
				const float middleBehind = reflectionCaptureBehind( distances, offset + r * middle, middleSeen );
				if( middleBehind > 0.0f )
				{
					tFar = middle;
					behindFar = middleBehind;
					seen = middleSeen;
				}
				else
				{
					tNear = middle;
					behindNear = middleBehind;
				}
			}
			// Упёрся в поверхность — луч за ней не дальше толщины; иначе прошёл позади предмета (край его силуэта из точки
			// съёмки) — ищем дальше
			if( behindFar < reflectionCaptureThickness + reflectionCaptureThicknessScale * seen )
			{
				found = true;
				return offset + r * ( tNear + ( tFar - tNear ) * saturate( -behindNear / max( behindFar - behindNear, 1e-4f ) ) );
			}
		}
		previousBehind = behind;
		previous = t;
		t *= reflectionCaptureStepGrowth;
	}
	return r;
}

// Доля карты в точке position: 1 внутри радиуса влияния, к его краю — переход в небо; distanceSquared — до точки съёмки
float reflectionCaptureWeight( float4 sphere, float4 params, float3 position, out float distanceSquared )
{
	const float3 offset = position - sphere.xyz;
	distanceSquared = dot( offset, offset );
	return sphere.w > 0.0f ? saturate( ( sphere.w - sqrt( distanceSquared ) ) * params.y ) : 0.0f;
}

// Отражённый свет окружения по карте: сцена, куда упёрся луч, и небо sky сквозь непокрытое; у края радиуса — небо
float3 reflectionCaptureSpecular( TextureCube colors, TextureCube<float> distances, float4 sphere, float4 params, float weight,
								  float3 position, float3 r, float roughness, float3 sky )
{
	bool found;
	const float3 direction = reflectionCaptureTrace( distances, position - sphere.xyz, r, found );
	[branch] if( !found )
		return sky;
	const float4 captured = colors.SampleLevel( g_SamplerLinearClamp, direction, roughness * ( specularMipCount - 1.0f ) );
	const float3 local = captured.rgb * params.x + sky * ( 1.0f - captured.a );
	return lerp( sky, local, weight );
}

// Отражённый свет окружения в направлении r из точки position, кд/м²: внутри радиуса карты отражений места — сцена, куда
// упёрся луч по карте, и небо сквозь непокрытое; у края радиуса — переход в небо (ambientSpecular), дальше — небо
float3 environmentSpecular( float3 position, float3 r, float roughness )
{
	const float3 sky = ambientSpecular( r, roughness );
	float distance0, distance1;
	const float weight0 = reflectionCaptureWeight( cb_reflectionCaptureSphere[0], cb_reflectionCapture[0], position, distance0 );
	const float weight1 = reflectionCaptureWeight( cb_reflectionCaptureSphere[1], cb_reflectionCapture[1], position, distance1 );
	[branch] if( max( weight0, weight1 ) <= 0.0f )
		return sky;
	[branch] if( weight1 > weight0 || ( weight1 == weight0 && distance1 < distance0 ) )
		return reflectionCaptureSpecular( g_reflectionCapture1, g_reflectionCaptureDistance1, cb_reflectionCaptureSphere[1],
										  cb_reflectionCapture[1], weight1, position, r, roughness, sky );
	return reflectionCaptureSpecular( g_reflectionCapture0, g_reflectionCaptureDistance0, cb_reflectionCaptureSphere[0],
									  cb_reflectionCapture[0], weight0, position, r, roughness, sky );
}

#endif
