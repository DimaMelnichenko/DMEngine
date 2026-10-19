////////////////////////////////////////////////////////////////////////////////
// Ветер уровня для растительности — как Wind Directional Source и SimpleGrassWind в UE. Поле ветра одно на уровень
// (класс Wind, константы cb_wind* кадра в Shaders/common.vs): направление, сила и порывы — волны, бегущие по миру со
// скоростью ветра (волны по лугу). Растение гнётся от корня: сдвиг верхушки растёт как квадрат высоты над корнем,
// как у консоли под нагрузкой; у каждого растения ещё своя дрожь. Отклик — параметр материала WindWeight.
// Сдвиг считается в vertexWorldPosition (Shaders/LightShader.vs) — у варианта «только глубина» тот же: depth prepass
// (EQUAL), тени и цвет совпадают. Деревья (материал PBRTree, WIND_TREE) гнутся по схеме Games wind SpeedTree —
// treeWindPosition. Нормаль и касательные гнутся вместе с вершиной (WindFrame): освещение согнутого растения — как у
// согнутого, а не у прямого
////////////////////////////////////////////////////////////////////////////////

#ifndef WIND_SH
#define WIND_SH

#include "common.vs"
#include "wind_field.sh"

// Касательный базис вершины в мире: ветер поворачивает его вместе с вершиной
struct WindFrame
{
	float3 normal;
	float3 tangent;
	float3 binormal;
};

// Изгиб растения с корнем root и откликом weight — вектор s, 1/м (по ветру): вершина на высоте h над корнем сдвигается на
// (s h², −½ |s|² h³) — как консоль под нагрузкой, верх не удлиняется: вместе со сдвигом вбок опускается (дуга). Дрожь ±25 %
// с частотой 1,5–2,5 Гц, phase 0…1 — своя у каждого растения (vertexWorldPosition: у экземпляра расстановки — по его
// повороту, не по корню: корень на земле с дисплейсментом сдвигается с камерой). Направление и сила — поле ветра в корне
// (windAt): в лесу и за гребнем тише, на гребне сильнее; порывисто — дрожь сильнее и направление мечется
float2 windBendVector( float3 root, float weight, float phase )
{
	const WindSample wind = windAt( root.xz );
	const float flutter = 0.25f * ( 1.0f + 2.0f * wind.turbulence ) * sin( windTwoPi * ( cb_gameTime * ( 1.5f + phase ) + phase ) );
	const float bend = cb_windStrength * weight * windGust( root.xz ) * windLoad( wind ) * ( 1.0f + flutter );	// 1/м
	return windSwirl( wind, phase ) * bend;
}

// Сдвиг вершины на высоте height, м, над корнем при изгибе bend (windBendVector)
float3 windOffset( float2 bend, float height )
{
	const float2 side = bend * ( height * height );
	const float drop = 0.5f * dot( side, side ) / max( height, 0.01f );
	return float3( side.x, -drop, side.y );
}

// Базис вершины под изгибом травы: градиент деформации J = I + slope ⊗ up (slope — производная сдвига по высоте, up — куда
// растёт высота, единичный): касательные × J, нормаль × J⁻ᵀ = n − up (slope · n) / (1 + up · slope) (обращение по
// Шерману — Моррисону). Точно для этого изгиба, без лишних выборок ветра
void windBendFrame( float2 bend, float height, float3 up, inout WindFrame frame )
{
	const float3 slope = float3( 2.0f * bend.x * height, -1.5f * dot( bend, bend ) * height * height, 2.0f * bend.y * height );
	frame.normal = normalize( frame.normal - up * ( dot( slope, frame.normal ) / max( 1.0f + dot( up, slope ), 0.1f ) ) );
	frame.tangent = normalize( frame.tangent + slope * dot( up, frame.tangent ) );
	frame.binormal = normalize( frame.binormal + slope * dot( up, frame.binormal ) );
}

// Случайная фаза 0…1 точки мира: у каждой ветви и веточки своя, одна и та же во всех LOD (начала ветвей общие)
float windPhase( float3 position )
{
	return frac( sin( dot( position, float3( 12.9898f, 78.233f, 37.719f ) ) ) * 43758.5453f );
}

// Поворот направления d вокруг единичной оси axis (формула Родрига)
float3 windRotate( float3 d, float3 axis, float s, float c )
{
	return d * c + cross( axis, d ) * s + axis * ( dot( axis, d ) * ( 1.0f - c ) );
}

// Поворот точки p вокруг pivot, склоняющий отрезок pivot → p к направлению toward на угол angle (формула Родрига для оси,
// перпендикулярной отрезку): длина отрезка сохраняется — ветвь не удлиняется; базис вершины — тем же поворотом
float3 windBend( float3 p, float3 pivot, float3 toward, float angle, inout WindFrame frame )
{
	const float3 v = p - pivot;
	float3 axis = cross( v, toward );
	const float axisLength = length( axis );
	[branch] if( axisLength < 1e-5f )
		return p;
	axis /= axisLength;
	float s, c;
	sincos( angle, s, c );
	frame.normal = windRotate( frame.normal, axis, s, c );
	frame.tangent = windRotate( frame.tangent, axis, s, c );
	frame.binormal = windRotate( frame.binormal, axis, s, c );
	return pivot + v * c + cross( axis, v ) * s;
}

// Направление изгиба ветви: по ветру direction и чуть поперёк — колебание со своей фазой, чтобы ветви не качались в одной
// плоскости
float3 windToward( float2 direction, float phase, float frequency )
{
	const float2 across = float2( -direction.y, direction.x );
	const float2 d = direction + across * ( 0.35f * sin( windTwoPi * ( cb_gameTime * frequency * 1.37f + phase ) ) );
	return normalize( float3( d.x, 0.0f, d.y ) );
}

// Ветер дерева — только у шейдеров с константами материала PBR (углы и частоты слоёв); импостер (impostor.vs) берёт
// из этого файла общие части
#ifdef DM_PBR_MATERIAL_CONSTANTS_H

// Ветер дерева по схеме Games wind SpeedTree — слои от дочерних к родительским (поворот ребёнка вокруг его начала, затем
// вместе с ним — вокруг начала родителя), все в мире:
//   рябь листвы вдоль нормали → веточка вокруг branch2 → ветвь вокруг branch1 → всё дерево вокруг корня.
// Угол слоя — угол материала × вес × сила ветра × порыв × (0,7 + 0,5 колебания со своей частотой и фазой): под нагрузкой
// ветвь склонена по ветру и качается около этого положения. Порыв ветви — волна уровня в её начале: по кроне бегут волны
// (rolling у SpeedTree). weights: x — доля высоты дерева, y — вес ряби. Поле ветра — в корне (windAt): направление
// и нагрузка у всего дерева одни, порывистость качает направление. normal — для ряби (одна и та же у вариантов «цвет» и
// «только глубина»), frame — базис вершины: повороты слоёв поворачивают и его
float3 treeWindPosition( float3 p, float3 root, float3 branch1Origin, float branch1Weight, float3 branch2Origin,
						 float branch2Weight, float2 weights, float3 normal, float response, inout WindFrame frame )
{
	const WindSample wind = windAt( root.xz );
	const float strength = cb_windStrength * response * windLoad( wind );
	const float2 direction = windSwirl( wind, windPhase( root ) );

	[branch] if( weights.y > 0.0f )
	{
		const float phase = windPhase( branch2Origin );
		const float ripple = sin( windTwoPi * ( cb_gameTime * g_windRippleFrequency + phase ) + dot( p, float3( 3.1f, 2.3f, 2.9f ) ) );
		p += normal * ( g_windRippleAmplitude * weights.y * strength * windGust( branch2Origin.xz ) * ripple );
	}
	[branch] if( branch2Weight > 0.0f )
	{
		const float phase = windPhase( branch2Origin );
		const float swing = 0.7f + 0.5f * sin( windTwoPi * ( cb_gameTime * g_windTwigFrequency + phase ) );
		const float angle = g_windTwigAngle * branch2Weight * strength * windGust( branch2Origin.xz ) * swing;
		p = windBend( p, branch2Origin, windToward( direction, phase, g_windTwigFrequency ), angle, frame );
	}
	[branch] if( branch1Weight > 0.0f )
	{
		const float phase = windPhase( branch1Origin );
		const float swing = 0.7f + 0.5f * sin( windTwoPi * ( cb_gameTime * g_windBranchFrequency + phase ) );
		const float angle = g_windBranchAngle * branch1Weight * strength * windGust( branch1Origin.xz ) * swing;
		p = windBend( p, branch1Origin, windToward( direction, phase, g_windBranchFrequency ), angle, frame );
	}
	[branch] if( weights.x > 0.0f )
	{
		// Всё дерево: наклон растёт с долей высоты (выше — сильнее), медленное колебание со своей фазой у экземпляра
		const float phase = windPhase( root );
		const float swing = 0.8f + 0.3f * sin( windTwoPi * ( cb_gameTime * g_windGlobalFrequency + phase ) );
		const float angle = g_windGlobalAngle * pow( weights.x, g_windGlobalExponent ) * strength * windGust( root.xz ) * swing;
		p = windBend( p, root, float3( direction.x, 0.0f, direction.y ), angle, frame );
	}
	return p;
}

#endif

#endif
