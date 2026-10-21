////////////////////////////////////////////////////////////////////////////////
// Карты отражений места — как Sphere Reflection Capture в UE: кубическая карта сцены, снятая из точки (ReflectionCaptures,
// съёмка сцены без неба — docs/scene_capture.md), с префильтром по шероховатости, как у освещения неба (мипы —
// specularMipCount). Альфа — покрытие сценой: цвет умножен на неё, где съёмка видела небо — 0. Рядом — расстояния от
// точки съёмки до того, что она видит. В кадре — две ближайшие к камере карты; точка воды берёт ту, в чьём радиусе влияния
// она глубже (обе полностью — у которой точка съёмки ближе), а если там луч ни во что не упёрся (ушёл за предмет рядом с её
// точкой: что за ним, она не видит) — вторую. Внутри радиуса отражения окружения ищут попадание лучом по
// карте (distance impostors, Szirmay-Kalos, Aszódi, Lazányi, Premecz 2005, «Approximate Ray-Tracing on the GPU with
// Distance Impostors»): луч отражения из точки воды идёт шагами, растущими с расстоянием, и на каждом сравнивает, дальше ли он от
// точки съёмки, чем видимое ею в этом направлении; ушёл за видимое — уточнение делением пополам и секущей, и попадание —
// там, где луч упёрся в поверхность. Так в отражении на своих местах и близкие предметы (шар, закрытый на экране другим),
// и дальние берега. Луч, ушедший за предмет далеко (за ним из точки съёмки ничего не видно), ищет дальше. Сквозь
// непокрытое и где луч ни во что не упёрся — небо освещения окружением по направлению самого луча (как сочетание карт
// отражений со Sky Light в UE). Ресурсы — t131…t134 (цвет и расстояния двух карт; карту точка воды выбирает сама —
// индексом в таблице ресурсов сцены), константы — cb_reflectionCaptureSphere / cb_reflectionCapture.
// docs/reflection_captures.md
////////////////////////////////////////////////////////////////////////////////

#ifndef REFLECTION_CAPTURE_SH
#define REFLECTION_CAPTURE_SH

#include "ibl.sh"
#include "reflection_capture_distance.sh"

static const uint reflectionCaptureSteps = 16;			// шагов луча по карте
static const float reflectionCaptureFirstStep = 0.5f;	// первый шаг, м; дальше каждый — в reflectionCaptureStepGrowth раз длиннее
static const float reflectionCaptureStepGrowth = 1.6f;	// 16 шагов — до ~600 м; дальше — по направлению самого луча
static const uint reflectionCaptureRefineSteps = 5;		// деление пополам у попадания, затем секущая
// Луч, ушедший за видимое из точки съёмки, упёрся в поверхность, если (метры и доля расстояния):
// - ушёл за неё не дальше reflectionCaptureEdge… — вошёл в предмет на его глубине, в том числе с края (крона ели: между
//   иголками в карте видны горы за километры, и почти каждое пересечение с ней — край);
// - или по обе стороны пересечения видно одно и то же — скачок не больше reflectionCaptureSurface…: на поверхности, которую
//   точка съёмки видит вскользь (низ шара), соседние тексели расходятся на D · θ / tg α (θ — тексель, при 256 на грань
//   0,006 рад) — при α ≈ 1,5° на четверть расстояния (меньший допуск — попадания через тексель отбрасываются, полосы).
// Иначе луч прошёл позади предмета (куб в 18 м, а луч — в 3 м за ним: иначе в воде второе, смещённое отражение куба)
static const float reflectionCaptureEdgeThickness = 0.5f;
static const float reflectionCaptureEdgeThicknessScale = 0.02f;
static const float reflectionCaptureSurfaceJump = 0.5f;
static const float reflectionCaptureSurfaceJumpScale = 0.25f;
// Луч ни в одной карте ни во что не упёрся — прошёл позади предмета. За ближним (куб, шар в десятке метров) в широком секторе
// может быть что угодно — небо (карта по направлению показала бы сам шар — его отражение тянулось бы столбом); за дальним
// (ёлки того берега в 20–40 м) — далёкое (reflectionCaptureNearHit): там, скорее всего, то же; от reflectionCaptureNearOccluder
// до reflectionCaptureOpenOccluder — плавно. Прежде граница «небо — цвет предмета» была на 40 м и резкой, и у деревьев того
// берега решения по текселям шли квадратами. Дальше reflectionCaptureFarOccluder вторую карту не ищем
static const float reflectionCaptureNearOccluder = 10.0f;
static const float reflectionCaptureOpenOccluder = 20.0f;
static const float reflectionCaptureFarOccluder = 40.0f;
// Попадание дальше reflectionCaptureFarHit от точки съёмки, м, — уже без параллакса (смещение точки воды от точки съёмки —
// десятки метров, до попадания — сотни метров и километры: горы), и карта берётся по направлению самого луча; от
// reflectionCaptureNearHit — плавно.
// Решения луча по карте — по отдельным текселям расстояний («попал», «прошёл позади», какая карта), и у далёкого (горы и
// небо над рощей, где тексель — десятки метров) соседние точки спокойной воды получали разное — квадратами по 16–32 пикселя
// (найдено 2026-10-21 у камеры Dimok); выборка по направлению непрерывна
static const float reflectionCaptureNearHit = 80.0f;
static const float reflectionCaptureFarHit = 160.0f;

// Насколько точка q (от точки съёмки) дальше видимого ею в своём направлении, м: > 0 — за поверхностью; небо в карте —
// миллион метров. Расстояния — без смешивания соседних текселей: у края предмета их среднее — несуществующая поверхность
// между ним и фоном, лучи, проходящие мимо предмета, упирались бы в его край (серый клин в отражении)
float reflectionCaptureBehind( TextureCube<float> distances, float3 q, out float seen )
{
	seen = distances.SampleLevel( g_SamplerPointClamp, q, 0.0f );
	return length( q ) - seen;
}

// Луч из точки offset (от точки съёмки) в направлении r по карте: направление на попадание из точки съёмки; found — луч
// упёрся в поверхность (нет — ни во что не упёрся или прошёл только позади предметов: что за ними, карта не знает).
// occluderDistance — расстояние до первого предмета, позади которого прошёл луч (0 — не было)
float3 reflectionCaptureTrace( TextureCube<float> distances, float3 offset, float3 r, out bool found, out float occluderDistance )
{
	found = false;
	occluderDistance = 0.0f;
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
			// Упёрся в поверхность (вошёл в предмет на его глубине или по обе стороны пересечения видно одно и то же); иначе
			// прошёл позади предмета — ищем дальше
			if( behindFar < reflectionCaptureEdgeThickness + reflectionCaptureEdgeThicknessScale * seen ||
				behindFar - behindNear < reflectionCaptureSurfaceJump + reflectionCaptureSurfaceJumpScale * seen )
			{
				found = true;
				return offset + r * ( tNear + ( tFar - tNear ) * saturate( -behindNear / max( behindFar - behindNear, 1e-4f ) ) );
			}
			if( occluderDistance <= 0.0f )
				occluderDistance = seen;
		}
		previousBehind = behind;
		previous = t;
		t *= reflectionCaptureStepGrowth;
	}
	// Дошёл до конца шагов перед всем видимым: дальше (горы вокруг карты в десятках километров) смещение точки воды от точки
	// съёмки уже ничего не значит — что карта видит по направлению самого луча, туда он и упрётся. Гора это или небо, решает
	// не тексель расстояний (край гор в отражении шёл бы ступеньками), а покрытие карты со смешиванием: небо подмешает
	// environmentSpecular
	found = previousBehind <= 0.0f;
	return r;
}

// Доля карты в точке position: 1 внутри радиуса влияния, к его краю — переход в небо; distanceSquared — до точки съёмки
float reflectionCaptureWeight( float4 sphere, float4 params, float3 position, out float distanceSquared )
{
	const float3 offset = position - sphere.xyz;
	distanceSquared = dot( offset, offset );
	return sphere.w > 0.0f ? saturate( ( sphere.w - sqrt( distanceSquared ) ) * params.y ) : 0.0f;
}

// Отражённый свет карты k кадра в направлении direction из её точки, кд/м², с небом sky сквозь непокрытое
float3 reflectionCaptureColor( uint k, float3 direction, float roughness, float3 sky )
{
	TextureCube colors = ResourceDescriptorHeap[NonUniformResourceIndex( DM_SCENE_BINDING( SLOT_REFLECTION_CAPTURE_0 - SLOT_SCENE_FIRST + 2 * k ) )];
	const float4 captured = colors.SampleLevel( g_SamplerLinearClamp, direction, roughness * ( specularMipCount - 1.0f ) );
	return captured.rgb * cb_reflectionCapture[k].x + sky * ( 1.0f - captured.a );
}

// Отражённый свет окружения в направлении r из точки position, кд/м²: внутри радиуса карты отражений места — сцена, куда
// упёрся луч по карте, и небо сквозь непокрытое; далёкое (дальше reflectionCaptureFarHit от точки съёмки) — карта по
// направлению луча; у края радиуса — переход в небо (ambientSpecular), дальше — небо
float3 environmentSpecular( float3 position, float3 r, float roughness )
{
	const float3 sky = ambientSpecular( r, roughness );
	float distance0, distance1;
	const float weight0 = reflectionCaptureWeight( cb_reflectionCaptureSphere[0], cb_reflectionCapture[0], position, distance0 );
	const float weight1 = reflectionCaptureWeight( cb_reflectionCaptureSphere[1], cb_reflectionCapture[1], position, distance1 );
	const float weight = max( weight0, weight1 );
	[branch] if( weight <= 0.0f )
		return sky;
	// Сначала карта, в чьём радиусе точка глубже (обе полностью — ближняя). Луч там ушёл за ближний предмет — слепая зона
	// этой карты: вторая карта кадра; ни там, ни там не упёрся — по расстоянию до предмета в первой (reflectionCaptureNearOccluder)
	const uint first = weight1 > weight0 || ( weight1 == weight0 && distance1 < distance0 ) ? 1 : 0;
	// Далёкое — первая карта по направлению луча
	const float3 distant = lerp( sky, reflectionCaptureColor( first, r, roughness, sky ), weight );
	float firstOccluderDistance = 0.0f;
	[loop] for( uint attempt = 0; attempt < 2; ++attempt )
	{
		const uint k = attempt == 0 ? first : 1 - first;
		const float4 sphere = cb_reflectionCaptureSphere[k];
		if( sphere.w <= 0.0f )
			continue;
		TextureCube<float> distances =
			ResourceDescriptorHeap[NonUniformResourceIndex( DM_SCENE_BINDING( SLOT_REFLECTION_CAPTURE_DISTANCE_0 - SLOT_SCENE_FIRST + 2 * k ) )];
		bool found;
		float occluderDistance;
		const float3 direction = reflectionCaptureTrace( distances, position - sphere.xyz, r, found, occluderDistance );
		[branch] if( found )
		{
			// Дошёл до конца шагов (направление — сам луч) или упёрся далеко — далёкое; близко — попадание
			const float far = length( direction ) <= 1.0f ? 1.0f :
							  smoothstep( reflectionCaptureNearHit, reflectionCaptureFarHit, length( direction ) );
			[branch] if( far >= 1.0f )
				return distant;
			return lerp( lerp( sky, reflectionCaptureColor( k, direction, roughness, sky ), weight ), distant, far );
		}
		if( attempt == 0 )
		{
			firstOccluderDistance = occluderDistance;
			if( occluderDistance > reflectionCaptureFarOccluder )
				break;
		}
	}
	return lerp( sky, distant, smoothstep( reflectionCaptureNearOccluder, reflectionCaptureOpenOccluder, firstOccluderDistance ) );
}

#endif
