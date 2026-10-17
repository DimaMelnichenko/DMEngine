////////////////////////////////////////////////////////////////////////////////
// Покрытие грани карты отражений места (ReflectionCaptures): после съёмки грани без неба альфа — 1 там, где сцена что-то
// нарисовала (глубина съёмки не на дальней плоскости), иначе цвет и альфа — 0. Альфу материалов не берём: у Masked она
// между порогом и 1, а покрытие нужно точное. Шейдер воды сквозь непокрытое берёт небо (reflection_capture.sh)
////////////////////////////////////////////////////////////////////////////////

#include "slots.h"
#include "bindless.sh"
#include "reflection_capture_constants.h"

DM_SRV( Texture2D<float>, g_depth, 0 );				// глубина съёмки грани (обратная: 0 — дальняя плоскость)
DM_UAV( RWTexture2DArray<float4>, g_cube, 0 );		// мип 0 кубической карты как массив из 6 граней

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const uint3 texel = uint3( id.xy, g_face );
	g_cube[texel] = g_depth[id.xy] > 0.0f ? float4( g_cube[texel].rgb, 1.0f ) : 0.0f;
}
