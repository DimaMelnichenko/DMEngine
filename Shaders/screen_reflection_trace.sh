////////////////////////////////////////////////////////////////////////////////
// Луч экранного отражения (ScreenSpaceReflections, docs/screen_reflections.md, screen_reflections.cs) от точки поверхности,
// которая просит отражения (вода: её глубина и нормаль — проход reflectionPrepass), без ряби по пирамиде глубины
// сцены (Hi-Z: в ячейке уровня L, 2^L пикселей, — ближайшая и дальняя глубина, как HZB в UE; обход — Uludag 2014,
// «Hi-Z Screen-Space Cone-Traced Reflections», GPU Pro 5). В экране луч — отрезок, вдоль которого обратная глубина линейна:
// ячейку, где всё дальше луча, он проходит целиком и поднимается на уровень крупнее, иначе — до глубины ближайшего в ячейке
// и спускается на уровень мельче; на уровне 0 это и есть попадание — с точностью до пикселя, за десятки шагов. Буфер
// глубины — поверхность без толщины: за ней луч ещё попадает, если ушёл не дальше толщины предмета, а дальше — проходит за
// ним (ветки у камеры, низ висящего шара) и ищет дальше; ячейку, где он за всем дальше толщины (по дальней глубине), тоже
// проходит целиком. Прежний луч в water.ps шёл по миру шагами, растущими в 1,15 раза, с толщиной в два шага: полосы
// попаданий и промахов ложились дугами (номер шага менял толщину)
////////////////////////////////////////////////////////////////////////////////

#ifndef SCREEN_REFLECTION_TRACE_SH
#define SCREEN_REFLECTION_TRACE_SH

#include "common.vs"
#include "screen_reflections.sh"
#include "screen_reflection_constants.h"
#include "bindless.sh"

DM_SRV( Texture2D<float>, g_reflectionSceneDepth, SLOT_SCENE_DEPTH );		// глубина сцены (обратная: 1 — у камеры, 0 — небо)
DM_SRV( Texture2D<float2>, g_sceneDepthPyramid, SLOT_SCENE_DEPTH_PYRAMID );	// уровни 1…: x — ближайшая, y — дальняя

// Толщина предмета за поверхностью буфера: метры и доля глубины вида; у луча от камеры больше — за предмет он заходит к его
// невидимой стороне (низ камня над водой), у луча к камере меньше — он проходит за тем, что ближе (трава у ближнего берега);
// между ними — по составляющей луча вдоль взгляда. У предела толщины вес попадания гаснет: у многогранника (камень, шар)
// глубина у края силуэта скачет по граням, и граница «попал — прошёл за» шла бы зубцами
static const float reflectionThicknessToward = 0.5f;
static const float reflectionThicknessTowardScale = 0.02f;
static const float reflectionThicknessAway = 1.5f;
static const float reflectionThicknessAwayScale = 0.02f;
static const float reflectionThicknessTurn = 4.0f;	// от камеры — полная толщина с составляющей луча вдоль взгляда 1 / это
static const uint reflectionMaxIterations = 96;
static const float reflectionRayOffset = 0.05f;		// начало луча над поверхностью по нормали, м
static const float reflectionMaxRayLength = 1000.0f;	// м
static const float reflectionCrossOffset = 0.01f;	// за границу ячейки, пикселей: точка луча — уже в следующей

// Расстояние вдоль оси взгляда по обратной глубине: z вида = P₃₂ / (глубина − P₂₂)
float reflectionViewDepth( float deviceDepth )
{
	return cb_projectionMatrix[3][2] / ( deviceDepth - cb_projectionMatrix[2][2] );
}

// Точка мира → пиксель кадра (x, y; y — вниз) и обратная глубина
float3 reflectionToScreen( float3 world, float2 size )
{
	const float4 clip = mul( float4( world, 1.0f ), cb_viewProjectionMatrix );
	const float3 ndc = clip.xyz / clip.w;
	return float3( ( ndc.x * 0.5f + 0.5f ) * size.x, ( 0.5f - ndc.y * 0.5f ) * size.y, ndc.z );
}

// Луч от точки position поверхности с нормалью normal (без ряби). Результат — uv попадания (центр пикселя), вес и доля пути
// луча в глубине точки попадания (по ней материал сдвигает отражение по своей ряби, water_shading.sh, screenReflection);
// промах — 0
float4 traceScreenReflection( float3 position, float3 normal )
{
	uint width, height;
	g_reflectionSceneDepth.GetDimensions( width, height );
	const float2 size = float2( width, height );
	const float3 r = upwardReflection( reflect( normalize( position - cb_cameraPosition ), normal ) );

	// Отрезок луча в экране: к камере — не ближе ближней плоскости, дальше — до края кадра
	const float3 origin = position + normal * reflectionRayOffset;
	const float originViewZ = mul( float4( origin, 1.0f ), cb_viewMatrix ).z;
	const float rViewZ = mul( float4( r, 0.0f ), cb_viewMatrix ).z;
	float rayLength = reflectionMaxRayLength;
	if( rViewZ < 0.0f )
		rayLength = min( rayLength, ( originViewZ - 1.5f * reflectionViewDepth( 1.0f ) ) / -rViewZ );
	if( rayLength <= 0.0f )
		return 0.0f;
	const float3 start = reflectionToScreen( origin, size );
	const float3 delta = reflectionToScreen( origin + r * rayLength, size ) - start;
	float tMax = 1.0f;
	[unroll] for( uint axis = 0; axis < 2; ++axis )
	{
		if( delta[axis] > 0.0f )
			tMax = min( tMax, ( size[axis] - reflectionCrossOffset - start[axis] ) / delta[axis] );
		else if( delta[axis] < 0.0f )
			tMax = min( tMax, ( reflectionCrossOffset - start[axis] ) / delta[axis] );
	}
	const float2 crossStep = float2( delta.x >= 0.0f ? 1.0f : 0.0f, delta.y >= 0.0f ? 1.0f : 0.0f );
	const float2 crossShift = float2( delta.x >= 0.0f ? reflectionCrossOffset : -reflectionCrossOffset,
									  delta.y >= 0.0f ? reflectionCrossOffset : -reflectionCrossOffset );
	const float2 inverseDelta = float2( abs( delta.x ) > 1e-6f ? 1.0f / delta.x : 1e30f, abs( delta.y ) > 1e-6f ? 1.0f / delta.y : 1e30f );
	const float awayShare = saturate( rViewZ * reflectionThicknessTurn );
	const float thicknessBase = lerp( reflectionThicknessToward, reflectionThicknessAway, awayShare );
	const float thicknessScale = lerp( reflectionThicknessTowardScale, reflectionThicknessAwayScale, awayShare );

	// Начало — на выходе из своего пикселя: в нём сама поверхность и дно под ней
	const float2 firstExits = ( floor( start.xy ) + crossStep + crossShift - start.xy ) * inverseDelta;
	float t = min( firstExits.x, firstExits.y );
	int level = 0;
	bool hit = false;
	float hitDepth = 0.0f;
	float behind = 0.0f;	// на сколько луч в пикселе попадания ушёл за поверхность буфера, м
	for( uint i = 0; i < reflectionMaxIterations && t <= tMax; ++i )
	{
		const float3 p = start + delta * t;
		const float cellSize = exp2( (float)level );
		const float2 cell = floor( p.xy / cellSize );
		// Уровень 0 — сама глубина сцены (за краем кадра Load даёт 0 — дальняя плоскость)
		const float2 range = level == 0 ? g_reflectionSceneDepth.Load( int3( cell, 0 ) ).xx :
										  g_sceneDepthPyramid.Load( int3( cell, level - 1 ) );
		const float closest = range.x;
		const float2 exits = ( ( cell + crossStep ) * cellSize + crossShift - start.xy ) * inverseDelta;
		const float tCell = min( exits.x, exits.y );
		if( p.z > closest )
		{
			// Луч ближе всего в ячейке; уходя от камеры, дойдёт до глубины ближайшего в ней на tPlane
			const float tPlane = delta.z < 0.0f ? ( closest - start.z ) / delta.z : 1e30f;
			if( tPlane < tCell )
			{
				t = max( t, tPlane );
				if( level == 0 )
				{
					hit = t <= tMax;
					hitDepth = closest;
					break;
				}
				--level;
			}
			else
			{
				t = tCell;
				level = min( level + 1, SCREEN_REFLECTION_LEVELS - 1 );
			}
		}
		else
		{
			// Луч за ближайшим в ячейке. За всем в ней дальше толщины (по дальней глубине и ближней к камере точке луча в ячейке:
			// идя к камере, он приближается) — проходит ячейку; на уровне 0 иначе — попадание, крупнее — уровень мельче
			const float rayNear = max( p.z, start.z + delta.z * min( tCell, tMax ) );
			const float farthestViewZ = reflectionViewDepth( range.y );
			if( rayNear < range.y && reflectionViewDepth( rayNear ) - farthestViewZ > thicknessBase + thicknessScale * farthestViewZ )
			{
				t = tCell;
				level = min( level + 1, SCREEN_REFLECTION_LEVELS - 1 );
			}
			else if( level > 0 )
				--level;
			else
			{
				hit = true;
				hitDepth = closest;
				behind = max( reflectionViewDepth( p.z ) - reflectionViewDepth( closest ), 0.0f );
				break;
			}
		}
	}
	// Небо (глубина 0) — промах: его отражение — небо материала (ambientSpecular), без ряби экрана
	if( !hit || hitDepth <= 0.0f )
		return 0.0f;

	// Центр пикселя попадания: луч входит в пиксель на его границе, и выборка цвета там — пополам с соседом (у края предмета
	// — шахматка «предмет — фон» от пикселя к пикселю)
	const float2 hitUV = ( floor( start.xy + delta.xy * t ) + 0.5f ) / size;
	const float hitViewZ = reflectionViewDepth( hitDepth );
	// Вес гаснет к краям кадра (там отражение обрывается) и к пределу толщины
	const float2 edge = saturate( min( hitUV, 1.0f - hitUV ) * 10.0f );
	const float weight = edge.x * edge.y * ( 1.0f - smoothstep( 0.5f, 1.0f, behind / ( thicknessBase + thicknessScale * hitViewZ ) ) );
	// Доля пути луча в глубине точки попадания: сдвиг точки по ряби — сдвиг отражённого направления «на бесконечности» × эту
	// долю (у далёкого попадания — весь, у близкого к воде — малая его часть)
	const float parallax = saturate( 1.0f - originViewZ / max( hitViewZ, 1e-3f ) );
	return float4( saturate( hitUV ), weight, parallax );
}

#endif
