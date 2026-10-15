////////////////////////////////////////////////////////////////////////////////
// Поле следов в траве (FoliageInteraction): в клетке окна вокруг камеры — куда и насколько наклонить растение, вектор XZ
// длиной 0…1. Клетка окна — клетка мира по модулю размера поля (тороидальная адресация): при сдвиге окна клетка остаётся на
// своём месте в текстуре, а ушедшая из окна начинается с нуля. Каждый кадр прежний след гаснет (растения распрямляются),
// поверх ложатся капсулы тех, кто идёт по траве: от оси капсулы наружу, под движущимися ногами — ещё и вперёд по ходу.
// Сильнее — заменяет, слабее — остаётся прежний: след не копится от кадра к кадру. Растения читают поле в
// Shaders/foliage_interaction.sh
////////////////////////////////////////////////////////////////////////////////

#include "bindless.sh"


#include "foliage_interaction_constants.h"

DM_SRV( Texture2D<float2>, g_previousField, 0 );
DM_UAV( RWTexture2D<float2>, g_field, 0 );

// Клетка мира, которую ячейка texel хранит при окне с началом origin: единственная клетка окна, равная texel по модулю
int2 windowCell( int2 texel, int2 origin )
{
	const int2 size = int2( g_resolution, g_resolution );
	return origin + ( ( texel - origin ) % size + size ) % size;
}

// Своё направление клетки 0…2π: под стоящими ногами трава ложится в разные стороны, а не в одну
float cellAngle( int2 cell )
{
	return frac( sin( dot( float2( cell ), float2( 12.9898f, 78.233f ) ) ) * 43758.5453f ) * 6.2831853f;
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_resolution ) )
		return;
	const int2 texel = int2( id.xy );
	const int2 cell = windowCell( texel, g_origin );

	// Прежний след той же клетки мира гаснет; ячейка, где была другая клетка (ушла из окна), — с нуля
	float2 push = 0.0f;
	if( all( windowCell( texel, g_previousOrigin ) == cell ) )
		push = g_previousField.Load( int3( texel, 0 ) ) * g_keep;

	const float2 position = ( float2( cell ) + 0.5f ) * g_cellSize;
	[loop] for( uint i = 0; i < g_interactorCount; ++i )
	{
		const float2 from = g_segments[i].xy;
		const float2 axis = g_segments[i].zw - from;
		const float t = saturate( dot( position - from, axis ) / max( dot( axis, axis ), 1e-6f ) );
		const float2 away = position - ( from + axis * t );
		const float distance = length( away );
		const float radius = g_shapes[i].x;
		// Полный наклон до радиуса («Trail width» / 2), за ним на ширине края — на нет
		const float strength = g_shapes[i].y * ( 1.0f - smoothstep( radius, radius + max( g_edges[i].x, 1e-3f ), distance ) );
		if( strength <= length( push ) )
			continue;
		// От оси наружу; у самой оси — по ходу или в свою сторону клетки
		float angleSin, angleCos;
		sincos( cellAngle( cell ), angleSin, angleCos );
		float2 direction = distance > 1e-3f ? away / distance : float2( angleCos, angleSin );
		direction = direction * saturate( distance / ( 0.5f * radius ) ) + g_shapes[i].zw;
		const float directionLength = length( direction );
		direction = directionLength > 1e-3f ? direction / directionLength : float2( angleCos, angleSin );
		push = direction * strength;
	}
	g_field[texel] = push;
}
