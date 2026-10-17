////////////////////////////////////////////////////////////////////////////////
// Покрытие и расстояния грани карты отражений места (ReflectionCaptures): после съёмки грани без неба альфа — 1 там, где
// сцена что-то нарисовала (глубина съёмки не на дальней плоскости), иначе цвет и альфа — 0; расстояние от точки съёмки до
// нарисованного, м (небо — reflectionCaptureSkyDistance: дальше всего, луч за ним не бывает). Альфу материалов не берём:
// у Masked она между порогом и 1, а покрытие нужно точное. Шейдер воды по расстояниям ищет, куда попал отражённый луч, а
// сквозь непокрытое берёт небо (reflection_capture.sh)
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "bindless.sh"
#include "reflection_capture_constants.h"

DM_SRV( Texture2D<float>, g_depth, 0 );				// глубина съёмки грани (обратная: 0 — дальняя плоскость)
DM_UAV( RWTexture2DArray<float4>, g_cube, 0 );		// мип 0 кубической карты как массив из 6 граней
DM_UAV( RWTexture2DArray<float>, g_distance, 1 );	// расстояния как массив из 6 граней

static const float reflectionCaptureSkyDistance = 1e6f;	// м: небо — дальше всего, что бывает в сцене

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const uint3 texel = uint3( id.xy, g_face );
	const float depth = g_depth[id.xy];
	const bool scene = depth > 0.0f;
	g_cube[texel] = scene ? float4( g_cube[texel].rgb, 1.0f ) : 0.0f;
	// Вдоль оси грани — z вида, до точки — во столько раз дальше, во сколько направление на тексель длиннее своей оси
	// (у грани 90° — вектор (s, t, 1), s и t от −1 до 1)
	const float2 st = ( id.xy + 0.5f ) / g_size * 2.0f - 1.0f;
	const float viewZ = g_projection32 / ( depth - g_projection22 );
	g_distance[texel] = scene ? viewZ * length( float3( st, 1.0f ) ) : reflectionCaptureSkyDistance;
}
