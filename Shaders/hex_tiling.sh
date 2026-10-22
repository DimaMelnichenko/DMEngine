////////////////////////////////////////////////////////////////////////////////
// Стохастический повтор текстуры — hex-tiling (L. Mikkelsen, «Practical Real-Time Hex-Tiling», JCGT 11(3), 2022; исходник —
// mmikk/hextile-demo; треугольная сетка — Heitz, Neyret 2018): плоскость текстурных координат поделена на шестиугольные
// ячейки, у каждой свой случайный сдвиг и поворот UV; точку дают три ближайшие ячейки (вершины треугольной сетки) с весами
// по барицентрам в седьмой степени, поправкой на высоту выборки и кривой контраста — повтор не складывается в сетку ни
// вблизи, ни издали. Производные UV поворачиваются вместе с ячейкой (SampleGrad — мипы без швов), отклонение нормали из
// текстуры — обратно (hexToUV). Ячейки и веса — здесь, выборки — у того, кто читает текстуру (terrain_material.sh,
// layerHeight в terrain_layers.sh)
////////////////////////////////////////////////////////////////////////////////

#ifndef HEX_TILING_SH
#define HEX_TILING_SH

struct HexCells
{
	float2 uv[3];			// координаты выборки в ячейке, повторы текстуры
	float2 rotation[3];		// cos и sin поворота ячейки
	float3 weight;			// доли ячеек по барицентрам (сумма 1; ячейка, которую не читать, — 0)
};

// Целочисленный хеш ячейки (PCG2D, Jarzynski, Olano, «Hash Functions for GPU Rendering», 2020): синусный хеш порта Mikkelsen
// теряет точность на координатах дальних гор
uint2 hexHash( int2 cell )
{
	uint2 v = asuint( cell ) * 1664525u + 1013904223u;
	v.x += v.y * 1664525u;
	v.y += v.x * 1664525u;
	v ^= v >> 16u;
	v.x += v.y * 1664525u;
	v.y += v.x * 1664525u;
	v ^= v >> 16u;
	return v;
}

// Повернуть вектор поворотом ячейки (из UV в текстуру ячейки)
float2 hexRotate( float2 v, float2 rotation )
{
	return float2( rotation.x * v.x - rotation.y * v.y, rotation.y * v.x + rotation.x * v.y );
}

// Обратно: вектор из текстуры ячейки (отклонение нормали) — в UV
float2 hexToUV( float2 v, float2 rotation )
{
	return float2( rotation.x * v.x + rotation.y * v.y, -rotation.y * v.x + rotation.x * v.y );
}

// Доля барицентрического веса, с которой ячейку можно не читать: после поправки на высоту (не больше чем в 1 / ( 1 −
// heightWeight ) раз) и кривой контраста с показателем gain её вклад меньше 1/200. С кривой порог выше — у стыков чаще
// читается одна-две ячейки вместо трёх
float hexSkipWeight( float heightWeight, float gain )
{
	return ( 1.0f - heightWeight ) * 0.5f * exp2( log2( 0.01f ) / gain );
}

// uv — в повторах текстуры; cell — расстояние между центрами соседних ячеек, повторов; rotation — доля полного случайного
// поворота (0 — только сдвиг: у слоистой скалы слои не поворачиваются); seed — чтобы у слоёв с одним повтором ячейки не совпадали;
// skip — доля веса, с которой ячейку не читать (hexSkipWeight)
HexCells hexCells( float2 uv, float cell, float rotation, uint seed, float skip )
{
	// Косой базис, в котором вершины треугольной сетки — целые точки, сторона треугольника — cell
	const float2 grid = uv / cell;
	const float2 skewed = float2( grid.x - 0.57735027f * grid.y, 1.15470054f * grid.y );
	const int2 base = int2( floor( skewed ) );
	float3 t = float3( frac( skewed ), 0.0f );
	t.z = 1.0f - t.x - t.y;
	// Нижний или верхний треугольник ромба
	const int s = t.z < 0.0f ? 1 : 0;
	const float sign = s ? 1.0f : -1.0f;
	const float3 barycentric = float3( -t.z * sign, s - t.y * sign, s - t.x * sign );
	const int2 vertex[3] = { base + int2( s, s ), base + int2( s, 1 - s ), base + int2( 1 - s, s ) };

	HexCells cells;
	float3 weight = pow( saturate( barycentric ), 7.0f );
	weight /= max( weight.x + weight.y + weight.z, 1e-6f );
	weight = select( weight < skip, 0.0f, weight );
	cells.weight = weight / max( weight.x + weight.y + weight.z, 1e-6f );
	[unroll] for( uint k = 0; k < 3; ++k )
	{
		const uint2 hash = hexHash( vertex[k] + int2( seed * 7919u, seed * 104729u ) );
		const float angle = ( ( hash.x >> 8 ) * ( 2.0f / 16777216.0f ) - 1.0f ) * 3.14159265f * rotation;
		float sine, cosine;
		sincos( angle, sine, cosine );
		cells.rotation[k] = float2( cosine, sine );
		// Центр ячейки — из косого базиса обратно; поворот — вокруг него, сдвиг — 16 бит на ось. Центр обратно не прибавляется:
		// текстура повторяется, а малые координаты точнее
		const float2 center = float2( vertex[k].x + 0.5f * vertex[k].y, 0.8660254f * vertex[k].y ) * cell;
		cells.uv[k] = hexRotate( uv - center, cells.rotation[k] ) + float2( hash.y & 0xffffu, hash.y >> 16 ) * ( 1.0f / 65536.0f );
	}
	return cells;
}

// Веса трёх ячеек (сумма 1): доли по барицентрам, у ячейки с более высокой выборкой (heights, 0…1) — больше на heightWeight
// (камень над песком — на стыке ячеек виден камень, у Mikkelsen — яркость цвета), затем кривая контраста с показателем
// gain (1 — без неё, больше — стыки уже; Mikkelsen, Gain3)
float3 hexWeights( HexCells cells, float3 heights, float heightWeight, float gain )
{
	float3 w = cells.weight * ( lerp( 1.0f, saturate( heights ), heightWeight ) + 1e-3f );
	w /= max( w.x + w.y + w.z, 1e-6f );
	w = select( w < 0.5f, 0.5f * pow( 2.0f * w, gain ), 1.0f - 0.5f * pow( max( 2.0f - 2.0f * w, 0.0f ), gain ) );
	return w / max( w.x + w.y + w.z, 1e-6f );
}

#endif
