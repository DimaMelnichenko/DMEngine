////////////////////////////////////////////////////////////////////////////////
// Трава расступается (FoliageInteraction): поле следов вокруг камеры — в клетке вектор XZ длиной 0…1, куда и насколько
// наклонить растение (его пишет Shaders/foliage_interaction.cs). Растение читает поле в корне и наклоняется вокруг него
// целиком, как придавленный пучок: длина стеблей та же, базис вершины поворачивается вместе с ней. Зовёт
// vertexWorldPosition (Shaders/LightShader.vs) у растений с WindWeight > 0, кроме деревьев, — одинаково во всех проходах
////////////////////////////////////////////////////////////////////////////////

#ifndef FOLIAGE_INTERACTION_SH
#define FOLIAGE_INTERACTION_SH

#include "common.vs"
#include "samplers.sh"
#include "bindless.sh"
#include "wind.sh"

DM_SRV( Texture2D<float2>, g_foliageInteraction, SLOT_FOLIAGE_INTERACTION );

// Ширина полосы у края окна поля, где след сходит на нет, м: окно сдвигается с камерой, и растение у края не распрямляется
// рывком
static const float foliageInteractionEdge = 2.0f;

// Наклон растения с корнем root по полю: вектор XZ длиной 0…1; поля нет (cb_foliageInteraction.z 0) или корень вне окна — 0.
// Адрес — мир по модулю окна (сэмплер с повтором): клетка мира лежит в той же ячейке, куда её пишет compute
float2 foliagePushAt( float3 root )
{
	[branch] if( cb_foliageInteraction.z <= 0.0f )
		return 0.0f;
	const float size = 1.0f / cb_foliageInteraction.z;
	const float2 local = root.xz - cb_foliageInteraction.xy;
	const float edge = min( min( local.x, local.y ), min( size - local.x, size - local.y ) );
	[branch] if( edge <= 0.0f )
		return 0.0f;
	const float2 push = g_foliageInteraction.SampleLevel( g_SamplerLinearWrap, root.xz * cb_foliageInteraction.z, 0.0f );
	return push * saturate( edge / foliageInteractionEdge );
}

// Наклон точки p растения вокруг корня root (up — куда растёт, единичный) по вектору push (foliagePushAt): к его
// направлению на «Max angle» × |push|, у каждого растения своё отклонение (phase 0…1 — его случайное число, постоянное:
// ±20° направления, 85–100 % угла) — трава под ногами ложится не строем. Поворот вокруг оси поперёк up: стебли не
// удлиняются, базис — тем же поворотом
float3 foliageTrample( float3 p, float3 root, float3 up, float2 push, float phase, inout WindFrame frame )
{
	const float amount = length( push );
	float turnSin, turnCos;
	sincos( ( phase - 0.5f ) * 0.7f, turnSin, turnCos );
	const float2 direction = push / amount;
	const float3 toward = float3( direction.x * turnCos - direction.y * turnSin, 0.0f, direction.x * turnSin + direction.y * turnCos );
	float3 axis = cross( up, toward );
	const float axisLength = length( axis );
	[branch] if( axisLength < 1e-4f )
		return p;
	axis /= axisLength;
	const float angle = cb_foliageInteraction.w * amount * ( 0.85f + 0.15f * frac( phase * 7.13f ) );
	float s, c;
	sincos( angle, s, c );
	frame.normal = windRotate( frame.normal, axis, s, c );
	frame.tangent = windRotate( frame.tangent, axis, s, c );
	frame.binormal = windRotate( frame.binormal, axis, s, c );
	return root + windRotate( p - root, axis, s, c );
}

#endif
