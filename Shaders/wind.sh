////////////////////////////////////////////////////////////////////////////////
// Ветер уровня для растительности — как Wind Directional Source и SimpleGrassWind в UE. Поле ветра одно на уровень
// (класс Wind, константы cb_wind* кадра в Shaders/common.vs): направление, сила и порывы — волны, бегущие по миру со
// скоростью ветра (волны по лугу). Растение гнётся от корня: сдвиг верхушки растёт как квадрат высоты над корнем,
// как у консоли под нагрузкой; у каждого растения ещё своя дрожь. Отклик — параметр материала WindWeight.
// Сдвиг считается в vertexWorldPosition (Shaders/LightShader.vs) — у варианта «только глубина» тот же: depth prepass
// (EQUAL), тени и цвет совпадают. Деревья (материал PBRTree, WIND_TREE) гнутся по схеме Games wind SpeedTree —
// treeWindPosition
////////////////////////////////////////////////////////////////////////////////

#ifndef WIND_SH
#define WIND_SH

#include "common.vs"
#include "wind_field.sh"

// Сдвиг вершины на высоте height, м, над корнем root растения с откликом weight. Верх не удлиняется: вместе со сдвигом
// вбок вершина опускается (дуга). Дрожь ±25 % с частотой 1,5–2,5 Гц, фаза — своя у каждого растения (хеш корня)
float3 windOffset( float3 root, float height, float weight )
{
	const float phase = frac( sin( dot( root.xz, float2( 12.9898f, 78.233f ) ) ) * 43758.5453f );
	const float flutter = 0.25f * sin( windTwoPi * ( cb_gameTime * ( 1.5f + phase ) + phase ) );
	const float bend = cb_windStrength * weight * windGust( root.xz ) * ( 1.0f + flutter );	// 1/м
	const float2 side = cb_windDirection * ( bend * height * height );
	const float drop = 0.5f * dot( side, side ) / max( height, 0.01f );
	return float3( side.x, -drop, side.y );
}

// Случайная фаза 0…1 точки мира: у каждой ветви и веточки своя, одна и та же во всех LOD (начала ветвей общие)
float windPhase( float3 position )
{
	return frac( sin( dot( position, float3( 12.9898f, 78.233f, 37.719f ) ) ) * 43758.5453f );
}

// Поворот точки p вокруг pivot, склоняющий отрезок pivot → p к направлению toward на угол angle (формула Родрига для оси,
// перпендикулярной отрезку): длина отрезка сохраняется — ветвь не удлиняется
float3 windBend( float3 p, float3 pivot, float3 toward, float angle )
{
	const float3 v = p - pivot;
	float3 axis = cross( v, toward );
	const float axisLength = length( axis );
	[branch] if( axisLength < 1e-5f )
		return p;
	axis /= axisLength;
	float s, c;
	sincos( angle, s, c );
	return pivot + v * c + cross( axis, v ) * s;
}

// Направление изгиба уровня: по ветру и чуть поперёк — колебание со своей фазой, чтобы ветви не качались в одной плоскости
float3 windToward( float phase, float frequency )
{
	const float2 across = float2( -cb_windDirection.y, cb_windDirection.x );
	const float2 d = cb_windDirection + across * ( 0.35f * sin( windTwoPi * ( cb_gameTime * frequency * 1.37f + phase ) ) );
	return normalize( float3( d.x, 0.0f, d.y ) );
}

// Ветер дерева по схеме Games wind SpeedTree — слои от дочерних к родительским (поворот ребёнка вокруг его начала, затем
// вместе с ним — вокруг начала родителя), все в мире:
//   рябь листвы вдоль нормали → веточка вокруг branch2 → ветвь вокруг branch1 → всё дерево вокруг корня.
// Угол слоя — угол материала × вес × сила ветра × порыв × (0,7 + 0,5 колебания со своей частотой и фазой): под нагрузкой
// ветвь склонена по ветру и качается около этого положения. Порыв ветви — волна уровня в её начале: по кроне бегут волны
// (rolling у SpeedTree). weights: x — доля высоты дерева, y — вес ряби
float3 treeWindPosition( float3 p, float3 root, float3 branch1Origin, float branch1Weight, float3 branch2Origin,
						 float branch2Weight, float2 weights, float3 normal, float response )
{
	const float strength = cb_windStrength * response;

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
		p = windBend( p, branch2Origin, windToward( phase, g_windTwigFrequency ), angle );
	}
	[branch] if( branch1Weight > 0.0f )
	{
		const float phase = windPhase( branch1Origin );
		const float swing = 0.7f + 0.5f * sin( windTwoPi * ( cb_gameTime * g_windBranchFrequency + phase ) );
		const float angle = g_windBranchAngle * branch1Weight * strength * windGust( branch1Origin.xz ) * swing;
		p = windBend( p, branch1Origin, windToward( phase, g_windBranchFrequency ), angle );
	}
	[branch] if( weights.x > 0.0f )
	{
		// Всё дерево: наклон растёт с долей высоты (выше — сильнее), медленное колебание со своей фазой у экземпляра
		const float phase = windPhase( root );
		const float swing = 0.8f + 0.3f * sin( windTwoPi * ( cb_gameTime * g_windGlobalFrequency + phase ) );
		const float angle = g_windGlobalAngle * pow( weights.x, g_windGlobalExponent ) * strength * windGust( root.xz ) * swing;
		p = windBend( p, root, float3( cb_windDirection.x, 0.0f, cb_windDirection.y ), angle );
	}
	return p;
}

#endif
