////////////////////////////////////////////////////////////////////////////////
// Материал воды — поверхности тайлов WaterSimulation (water.ps: озёра и ручьи одним полем, вода симуляции), как Single
// Layer Water в UE. Под поверхностью — цвет сцены за водой (копия буфера, SLOT_SCENE_COLOR,
// со сдвигом по нормали — преломление) с поглощением по пути луча в воде (глубина сцены, SLOT_SCENE_DEPTH) и рассеянием
// в толще. Сверху — отражение: блик солнца с тенью и искрами, отражённое небо и экранные отражения — точка попадания луча
// от поверхности без ряби (свой проход, ScreenSpaceReflections), сдвинутая по ряби (сама вода в глубину сцены не пишет и
// себя не отражает). Рябь — гладкий полосовой узор (карта WaterRipples) на плитках по
// клеткам сетки воды (1 м; в детальной воде у русел — 0,5 м): в плитке узор повёрнут по течению в её центре, вытянут
// вдоль него и плывёт с его скоростью, соседние
// плитки смешиваются без потери контраста (Tiled Directional Flow, van Hoesel 2011; смешение — Heitz, Neyret 2018): узор
// не сбрасывается фазами и показывает, куда течёт вода. Пена — где течение быстрое, неоднородное или ручей круто падает;
// её узор несёт то же течение. Тонкая вода у берега прозрачнее и без отражения — кромка мягкая. Константы —
// Shaders/water_surface.sh
////////////////////////////////////////////////////////////////////////////////

#ifndef WATER_SHADING_SH
#define WATER_SHADING_SH

#include "lighting.sh"
#include "screen_reflections.sh"
#include "reflection_capture.sh"
#include "water_surface.sh"
#include "water.sh"
#include "wind_field.sh"
#include "bindless.sh"

DM_SRV( Texture2D<float4>, g_ripple, 1 );	// карта ряби (WaterRipples): наклон крупной и мелкой октавы узора по U и V
DM_SRV( Texture2D<float4>, g_foam, 4 );		// пена (g_foamTextureRepeat > 0): R — маска 0…1, GB — нормаль по ней (0,5 + 0,5 · n),
												// A — шум растворения 0…1
DM_SRV( Texture2D<float>, g_sceneDepth, SLOT_SCENE_DEPTH );
DM_SRV( Texture2D<float4>, g_sceneColor, SLOT_SCENE_COLOR );
// Экранное отражение (ScreenSpaceReflections): uv попадания луча от поверхности без ряби, вес, доля пути луча в глубине
DM_SRV( Texture2D<float4>, g_screenReflection, SLOT_SCREEN_REFLECTION );

static const float waterF0 = 0.02f;		// отражение воды по нормали (показатель преломления 1,33)
static const float edgeDepth = 0.02f;	// мельче — кромка: отражение и рассеяние гаснут к урезу
static const float3 foamAlbedo = 0.8f;
static const float foamRoughness = 0.7f;

// Рябь: наклон в карте ÷ 4 (WaterRipples::slopeRange); наклон при силе 1 (среднеквадратичный) и доля мелкой октавы; узор
// вытянут вдоль течения в полную силу (g_rippleStretch) с этой скорости
static const float rippleSlopeRange = 4.0f;
static const float rippleSlope = 0.35f;
static const float rippleFineWeight = 0.7f;
static const float rippleStretchSpeed = 0.3f;	// м/с
static const float rippleMinSlope = 0.005f;		// рябь течения слабее не видна — плитки не считаются (почти стоячее озеро)
// Течение рябит поверхность только с этой скорости (турбулентность у дна доходит до поверхности), полная рябь течения — с
// rippleOnsetFull, м/с: озеро с течением в сантиметры в секунду без ветра — зеркало, а струя ручья в нём рябит
static const float rippleOnsetStart = 0.05f;
static const float rippleOnsetFull = 0.15f;
// Ветровая рябь мельче повтора g_rippleScale во столько раз и бежит по ветру с этой скоростью
static const float calmOctave = 2.0f;
static const float calmSpeed = 0.5f;			// м/с
// Узор пены — наклон крупной октавы по U (разброс 1/√2) втрое мельче ряби, приведённый к разбросу 0,43 доли пены
static const float foamFrequency = 3.0f;
static const float foamPatternScale = 0.43f * 1.41421356f;
// Пена из текстуры: крутизна её нормали и белизна тонкой пены (толстая — foamAlbedo); край узора по ряби
static const float foamBump = 1.0f;
static const float foamThinShade = 0.7f;
static const float foamPatternSoftness = 0.2f;
// Плитки ряби в детальной воде у русел — две ячейки её сетки: течение там мельче метра (обход камня, профиль поперёк
// русла), а ячейка 0,25 м меньше волны ряби (~20 см) — узор рассыпался бы. У края набора детальных плиток рябь переходит
// в плитки 1 м на этой полосе — без шва
static const float detailRippleCell = 0.5f;
// Плитка в окне подробного течения у камеры берёт его направление в полную силу с этой скорости течения окна, м/с
static const float nearDirectionSpeed = 0.05f;
static const float detailRippleFade = 1.0f;		// м
// Стоячие волны над перекатами: волна стоит на месте, когда её скорость против течения равна скорости воды. Узор — крупная
// октава карты ряби, повёрнутая по течению, как рябь, но неподвижная и вытянутая поперёк течения (гребни поперёк); длина —
// из дисперсии волн на воде, сила — по числу Фруда (на глубоком медленном плёсе волн нет, на мелком быстром перекате — есть)
static const float standingSlope = 0.18f;			// наклон при силе 1 (среднеквадратичный)
static const float standingFroudeStart = 0.5f;		// число Фруда v / √(g h): волны с этого, полные — с standingFroudeFull
static const float standingFroudeFull = 0.9f;
static const float standingCrest = 5.0f;			// гребень длиннее волны во столько раз
static const float standingRepeat = 12.8f;			// повторов карты ряби на длину волны её крупной октавы (~20 текселей из 256)
static const float standingMinWavelength = 0.1f;	// м
static const float standingMinDepth = 0.03f;		// мельче — волн нет, м
static const float standingMinSpeed = 0.3f;			// м/с: медленнее — числа Фруда для волн нет и на мелкой воде
static const float gravityAcceleration = 9.81f;
// Искры: светило в гранях ряби мельче пикселя (sparkleWeight)
static const float lightSolidAngle = 6.8e-5f;	// телесный угол диска солнца, ср (радиус 0,27°; у луны почти такой же)
static const float sparkleLifetime = 0.2f;		// с: грань отражает светило столько, затем гаснет, и блестит другая

// Плитка ряби — клетка сетки воды: узор в ней повёрнут по течению в её центре, вытянут вдоль него и плывёт с его
// скоростью (Tiled Directional Flow, van Hoesel 2011). Поворот в плитке один, поэтому узор не закручивается и не
// растягивается, сколько бы ни шла вода; у пикселя — четыре ближайшие плитки с билинейными весами
struct RippleTile
{
	float2 center;		// центр клетки, м мира
	float2 along;		// направление течения
	float  rate;		// сдвиг узора вдоль течения, повторов в секунду
	float  stretch;		// узор вдоль течения длиннее во столько раз
	float2 offset;		// случайный сдвиг узора: соседние плитки не повторяют друг друга
	float  weight;
};

struct RippleTiles
{
	RippleTile tiles[4];
	// 1 / √Σw²: несвязанные узоры плиток смешиваются без потери контраста между центрами (Heitz, Neyret 2018) — узор
	// с нулевым средним, его разброс сохраняется
	float normalization;
};

// Волновое число стоячей волны на течении, 1/м: скорость волны против течения равна скорости воды — дисперсия волн на воде
// конечной глубины v² = g · th(k h) / k (Finch, GPU Gems 1, гл. 1: скорость волны — от её длины); у почти критического
// течения (Фруд → 1) решение уходит в длинные волны — не длиннее 2π глубины (k h ≥ 1); не короче standingMinWavelength
float standingWavenumber( float speed, float depth )
{
	const float v2 = max( speed * speed, 1e-4f );
	const float h = max( depth, 0.01f );
	float k = gravityAcceleration / v2;
	[unroll] for( int i = 0; i < 3; ++i )
		k = max( gravityAcceleration * tanh( k * h ) / v2, 1.0f / h );
	return min( k, 6.2831853f / standingMinWavelength );
}

// PCG (Jarzynski, Olano 2020)
uint rippleHash( uint value )
{
	const uint state = value * 747796405u + 2891336453u;
	const uint word = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

// Плитка без воды (за урезом или клетка грубой сетки, где тонкий слой по крутому видит только детальная вода) берёт
// течение пикселя: оно разное у каждого пикселя, и снос узора со временем растащил бы его в нити (узкий жёлоб у берега).
// Поэтому у сухой плитки узор повёрнут по течению пикселя, но не плывёт (rate 0), а если есть плитки с водой — она узора не
// даёт: вес 0, смешение по плиткам с водой
void dropDryTiles( inout RippleTiles result, bool wet[4] )
{
	const bool anyWet = wet[0] || wet[1] || wet[2] || wet[3];
	float weights = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		if( anyWet && !wet[k] )
			result.tiles[k].weight = 0.0f;
		weights += result.tiles[k].weight * result.tiles[k].weight;
	}
	result.normalization = rsqrt( max( weights, 1e-4f ) );
}

// Плитки вокруг точки xz; velocity — течение в точке: у плитки без воды (за урезом) — оно
RippleTiles rippleTiles( float2 xz, float2 velocity )
{
	// Клетки текстуры воды: столбцы — по X мира, строки — против Z (water.vs)
	const float2 position = float2( xz.x, g_worldSize - xz.y ) / g_cellSize - 0.5f;
	const int2 base = (int2)floor( position );
	const float2 f = position - (float2)base;
	RippleTiles result;
	bool wet[4];
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = clamp( base + corner, 0, (int)g_size - 1 );
		const float4 state = g_waterState.Load( int3( cell, 0 ) );
		wet[k] = state.x > 0.0f;
		const float2 flow = wet[k] ? state.yz : velocity;
		const float speed = length( flow );
		RippleTile tile;
		tile.center = float2( ( cell.x + 0.5f ) * g_cellSize, g_worldSize - ( cell.y + 0.5f ) * g_cellSize );
		tile.along = speed > 1e-4f ? flow / speed : float2( 1.0f, 0.0f );
		tile.stretch = lerp( 1.0f, g_rippleStretch, saturate( speed / rippleStretchSpeed ) );
		tile.rate = wet[k] ? speed / ( g_rippleScale * tile.stretch ) : 0.0f;
		const uint hash = rippleHash( (uint)cell.x + rippleHash( (uint)cell.y ) );
		tile.offset = float2( hash & 0xffffu, hash >> 16u ) / 65536.0f;
		tile.weight = ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
		result.tiles[k] = tile;
	}
	dropDryTiles( result, wet );
	return result;
}

// Плитки детальной воды вокруг точки xz: клетка detailRippleCell по сетке мира, течение — детальной воды у центра плитки
// (ячейка у угла центра: выборка RGBA32F с фильтрацией вдвое дороже). texel — тексель точки (water.sh, waterDetailTexel:
// x, y — в текселях плитки, z — срез): центр соседней плитки берётся из той же плитки — из её поля в тексель, за ним —
// с края (поле — копия ячеек соседа).
// В окне подробного течения у камеры направление плитки — по течению окна у её центра (обход камней, повороты, вихри; к
// краю окна — детальное, nearWaterWeight), а сдвиг узора и растяжение — по детальной сетке: она неподвижна, и сдвиг
// «время × скорость» не скачет. Течение окна меняется каждый кадр, и сдвиг по нему при времени игры в сотни секунд
// перематывал бы узор на много повторов; поворот направления сдвигает узор лишь на расстояние от центра плитки × угол
RippleTiles detailRippleTiles( float2 xz, float2 velocity, float3 texel )
{
	const bool nearWindow = cb_waterNear.w > 0.0f;
	uint width, height, slices;
	g_waterDetail.GetDimensions( width, height, slices );
	const float2 size = float2( width, height );
	const float step = cb_waterDetail.y / ( width - 2 );
	const float2 position = xz / detailRippleCell - 0.5f;
	const int2 base = (int2)floor( position );
	const float2 f = position - (float2)base;
	RippleTiles result;
	bool wet[4];
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = base + corner;
		RippleTile tile;
		tile.center = ( cell + 0.5f ) * detailRippleCell;
		const int2 at = clamp( int2( floor( texel.xy + ( tile.center - xz ) / step - 0.5f ) ), 0, int2( size ) - 1 );
		const float4 state = g_waterDetail.Load( int4( at, texel.z, 0 ) );
		wet[k] = state.w > 0.0f;
		const float2 flow = wet[k] ? state.yz : velocity;
		const float speed = length( flow );
		tile.along = speed > 1e-4f ? flow / speed : float2( 1.0f, 0.0f );
		tile.stretch = lerp( 1.0f, g_rippleStretch, saturate( speed / rippleStretchSpeed ) );
		tile.rate = wet[k] ? speed / ( g_rippleScale * tile.stretch ) : 0.0f;
		[branch] if( nearWindow && wet[k] )
		{
			const float weight = nearWaterWeight( tile.center );
			const float4 near = sampleNearWater( tile.center );
			// Камень выше воды — направление детальное: течение сквозь него — просачивание. Почти стоячая вода окна направления
			// не задаёт: оно там шумит от кадра к кадру, и узор вертелся бы вокруг центра плитки
			const float share = weight * saturate( length( near.yz ) / nearDirectionSpeed );
			[branch] if( share > 0.0f && near.w > nearWaterOpen )
			{
				const float2 blended = lerp( tile.along, normalize( near.yz ), share );
				if( dot( blended, blended ) > 1e-8f )
					tile.along = normalize( blended );
			}
		}
		const uint hash = rippleHash( (uint)cell.x + rippleHash( (uint)cell.y + 0x9e3779b9u ) );
		tile.offset = float2( hash & 0xffffu, hash >> 16u ) / 65536.0f;
		tile.weight = ( corner.x ? f.x : 1.0f - f.x ) * ( corner.y ? f.y : 1.0f - f.y );
		result.tiles[k] = tile;
	}
	dropDryTiles( result, wet );
	return result;
}

// Доля ряби по плиткам детальной воды в точке xz детальной плитки: 1, к краю набора плиток (за краем плитки нет) — до 0
// на полосе detailRippleFade
float detailRippleWeight( float2 xz )
{
	const float tileSize = cb_waterDetail.y;
	uint tilesX, tilesZ;
	g_terrainDetailIndex.GetDimensions( tilesX, tilesZ );
	const int2 tile = int2( floor( xz / tileSize ) );
	const float2 local = xz - tile * tileSize;
	float weight = 1.0f;
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const int2 side = k == 0 ? int2( -1, 0 ) : k == 1 ? int2( 1, 0 ) : k == 2 ? int2( 0, -1 ) : int2( 0, 1 );
		const float distance = k == 0 ? local.x : k == 1 ? tileSize - local.x : k == 2 ? local.y : tileSize - local.y;
		if( distance >= detailRippleFade )
			continue;
		const int2 neighbor = tile + side;
		const bool present = all( neighbor >= 0 ) && neighbor.x < (int)tilesX && neighbor.y < (int)tilesZ &&
							 g_terrainDetailIndex.Load( int3( neighbor, 0 ) ) != 0;
		if( !present )
			weight = min( weight, distance / detailRippleFade );
	}
	return saturate( weight );
}

// Во сколько раз поверхность длиннее своей проекции вдоль течения и поперёк: на крутом (вода по жёлобу, стенка ступени)
// узор, положенный сверху по X и Z, вытянулся бы по склону в нити — его повторы сжимаются во столько же раз. Считается раз
// на пиксель по течению в нём (у соседних плиток оно почти то же): в цикле плиток каждая лишняя величина — регистры, а
// этот шейдер к ним чувствителен. Узор не растягивается, но плывёт по горизонтальной скорости — на крутом медленнее воды
float2 surfaceScale( float2 gradient, float2 velocity )
{
	const float speed = length( velocity );
	const float2 along = speed > 1e-4f ? velocity / speed : float2( 1.0f, 0.0f );
	const float2 slope = float2( dot( gradient, along ), dot( gradient, float2( -along.y, along.x ) ) );
	return sqrt( 1.0f + slope * slope );
}

// Рябь течения: наклон крупной (xy) и мелкой (zw) октавы по X и Z мира; frequency — во сколько раз узор мельче повтора
// g_rippleScale; dX, dY — экранные производные xz: мипы — вдали рябь сглаживается, а не искрит. pattern — наклон крупной
// октавы по U в узоре: величина с нулевым средним для узора пены. surface — во сколько раз поверхность длиннее проекции
// вдоль течения и поперёк (surfaceScale): узор ложится по длине вдоль неё, а не проекцией сверху
float4 flowRipple( RippleTiles tiles, float2 xz, float2 surface, float frequency, float2 dX, float2 dY, out float pattern )
{
	float4 result = 0.0f;
	pattern = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const RippleTile tile = tiles.tiles[k];
		const float2 across = float2( -tile.along.y, tile.along.x );
		const float2 scale = frequency * surface / ( g_rippleScale * float2( tile.stretch, 1.0f ) );
		const float2 local = xz - tile.center;
		float2 uv = float2( dot( local, tile.along ), dot( local, across ) ) * scale + tile.offset;
		uv.x -= frac( cb_gameTime * tile.rate * frequency );
		const float2 uvDX = float2( dot( dX, tile.along ), dot( dX, across ) ) * scale;
		const float2 uvDY = float2( dot( dY, tile.along ), dot( dY, across ) ) * scale;
		const float4 ripple = g_ripple.SampleGrad( g_SamplerLinearWrap, uv, uvDX, uvDY ) * rippleSlopeRange;
		// Наклон узора — в мир: вдоль течения узор длиннее и во столько же раз положе
		result += tile.weight * float4( tile.along * ( ripple.x / tile.stretch ) + across * ripple.y,
										tile.along * ( ripple.z / tile.stretch ) + across * ripple.w );
		pattern += tile.weight * ripple.x;
	}
	pattern *= tiles.normalization;
	return result * tiles.normalization;
}

// Стоячие волны по тем же плиткам, что рябь: узор повёрнут по течению, но не плывёт; вдоль течения — длина стоячей волны
// (scale — повторов узора на метр), поперёк — гребень в standingCrest раз длиннее; наклон по X и Z мира. Сдвиг узора — свой
// (не как у ряби). Длина волны и сила — по пикселю, а не по плитке: код волн в плитках поднимал число регистров всего шейдера,
// и вода была дороже и без волн
float2 standingWaves( RippleTiles tiles, float2 xz, float2 surface, float scale, float2 dX, float2 dY )
{
	float2 slope = 0.0f;
	const float2 scale2 = surface * scale * float2( 1.0f, 1.0f / standingCrest );
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const RippleTile tile = tiles.tiles[k];
		const float2 across = float2( -tile.along.y, tile.along.x );
		const float2 local = xz - tile.center;
		const float2 uv = float2( dot( local, tile.along ), dot( local, across ) ) * scale2 + tile.offset.yx;
		const float2 uvDX = float2( dot( dX, tile.along ), dot( dX, across ) ) * scale2;
		const float2 uvDY = float2( dot( dY, tile.along ), dot( dY, across ) ) * scale2;
		const float2 wave = g_ripple.SampleGrad( g_SamplerLinearWrap, uv, uvDX, uvDY ).xy * rippleSlopeRange;
		// Наклон узора — в мир: поперёк узор длиннее и во столько же раз положе
		slope += tile.weight * ( tile.along * wave.x + across * ( wave.y / standingCrest ) );
	}
	return slope * tiles.normalization;
}

// Пена из текстуры по тем же плиткам, что рябь: течение несёт узор, вдоль течения он вытянут, на крутом — по длине вдоль
// поверхности (surface). Маска и шум растворения (dissolve) — вокруг середины 0,5 (смешение плиток с делением на √Σw², как
// у ряби: узор не тускнеет и не густеет), slope — наклон пены по её нормали в мире
float foamTexturePattern( RippleTiles tiles, float2 xz, float2 surface, float2 dX, float2 dY, out float2 slope, out float dissolve )
{
	float mask = 0.0f;
	slope = 0.0f;
	dissolve = 0.0f;
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const RippleTile tile = tiles.tiles[k];
		const float2 across = float2( -tile.along.y, tile.along.x );
		const float2 scale = surface / ( g_foamTextureRepeat * float2( tile.stretch, 1.0f ) );
		const float2 local = xz - tile.center;
		float2 uv = float2( dot( local, tile.along ), dot( local, across ) ) * scale + tile.offset;
		uv.x -= frac( cb_gameTime * tile.rate * g_rippleScale / g_foamTextureRepeat );
		const float2 uvDX = float2( dot( dX, tile.along ), dot( dX, across ) ) * scale;
		const float2 uvDY = float2( dot( dY, tile.along ), dot( dY, across ) ) * scale;
		const float4 foam = g_foam.SampleGrad( g_SamplerLinearWrap, uv, uvDX, uvDY );
		mask += tile.weight * ( foam.r - 0.5f );
		dissolve += tile.weight * ( foam.a - 0.5f );
		// Наклон по U и V — минус xy нормали; в мир — как у ряби (вдоль течения узор длиннее и во столько же раз положе)
		const float2 gradient = -( foam.gb * 2.0f - 1.0f ) * foamBump;
		slope += tile.weight * ( tile.along * ( gradient.x / tile.stretch ) + across * gradient.y );
	}
	slope *= tiles.normalization;
	dissolve = 0.5f + dissolve * tiles.normalization;
	return mask * tiles.normalization;
}

// Ветровая рябь: тот же узор мельче, без поворота, бежит по ветру; наклон крупной (xy) и мелкой (zw) октавы
float4 calmRipple( float2 xz, float frequency, float2 dX, float2 dY )
{
	const float scale = frequency / g_rippleScale;
	const float2 uv = ( xz - cb_windDirection * ( cb_gameTime * calmSpeed ) ) * scale;
	return g_ripple.SampleGrad( g_SamplerLinearWrap, uv, dX * scale, dY * scale ) * rippleSlopeRange;
}

// Расстояние вдоль оси взгляда по глубине буфера (обратной): z вида = P₃₂ / (глубина − P₂₂)
float viewDepth( float deviceDepth )
{
	return cb_projectionMatrix[3][2] / ( deviceDepth - cb_projectionMatrix[2][2] );
}

// Экранное отражение (ScreenSpaceReflections, Shaders/screen_reflections.cs): точка попадания луча от поверхности без
// ряби, сдвинутая по ряби — на столько, на сколько рябь поворачивает отражённое направление на экране (точка «на
// бесконечности»), × доля пути луча в глубине попадания (близкое к воде сдвигается меньше). Сдвиг, который попал на то, что
// ближе воды (берег перед ней), не берётся — как у преломления. rgb — цвет сцены, a — вес (0 — промах: небо)
float4 screenReflection( float4 position, float3 view, float3 surfaceNormal, float3 rippleNormal )
{
	const float4 hit = g_screenReflection[uint2( position.xy )];
	[branch] if( hit.z <= 0.0f )
		return 0.0f;
	float2 uv = hit.xy;
	const float4 flatClip = mul( float4( upwardReflection( reflect( -view, surfaceNormal ) ), 0.0f ), cb_viewProjectionMatrix );
	const float4 rippleClip = mul( float4( upwardReflection( reflect( -view, rippleNormal ) ), 0.0f ), cb_viewProjectionMatrix );
	if( flatClip.w > 1e-3f && rippleClip.w > 1e-3f )
	{
		const float2 shifted = hit.xy + ( rippleClip.xy / rippleClip.w - flatClip.xy / flatClip.w ) * float2( 0.5f, -0.5f ) * hit.w;
		uint width, height;
		g_sceneColor.GetDimensions( width, height );
		const uint2 pixel = min( uint2( saturate( shifted ) * float2( width, height ) ), uint2( width, height ) - 1 );
		if( g_sceneDepth[pixel] <= position.z )
			uv = saturate( shifted );
	}
	// Гаснет к краям экрана: там, где отражение обрывается, видно небо, а не шов
	const float2 edge = saturate( min( uv, 1.0f - uv ) * 10.0f );
	return float4( g_sceneColor.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).rgb, hit.z * edge.x * edge.y );
}

// Цвет сцены за водой (с экспозицией) — без тумана и воздуха между камерой и поверхностью: вода считается до них, а
// дымка ложится на её итоговый цвет один раз. Иначе дымка оказалась бы за водой и гасла в её толще — вода вдали
// темнела бы в тумане
float3 withoutFogging( float3 sceneColor, float3 position )
{
	const float3 inscatter = applyFogging( 0.0f, position );
	const float3 transmittance = applyFogging( 1.0f, position ) - inscatter;
	return max( sceneColor / preExposure() - inscatter, 0.0f ) / max( transmittance, 1e-4f );
}

// Что материалу нужно о точке поверхности
struct WaterShading
{
	float4 position;		// SV_POSITION пикселя
	float3 worldPosition;
	float3 normal;			// нормаль поверхности без ряби
	float2 velocity;		// течение по X и Z мира, м/с
	float  depth;			// глубина воды в точке, м: у уреза — кромка
	float  foam;			// доля пены 0…1 до кромки и узора (быстро, сдвиг скорости, перекат)
	float  ripple;			// сила ряби течения не меньше этой доли (ручей неспокоен и при малой скорости); 0 — по скорости
	// Детальная вода у русел: тексель точки (waterDetailTexel) и доля ряби по её плиткам 0,5 м (detailRippleWeight;
	// 0 — плитки 1 м)
	float3 detailTexel;
	float  detailRipple;
};

// Рябь (flow), стоячие волны (standing: если standingScale > 0) и узор пены (foamNoise, если foam; у текстуры пены — ещё её
// наклон foamSlope) по набору плиток — с весом weight
void addRipple( RippleTiles tiles, float2 xz, float2 surface, float2 dX, float2 dY, bool foam, float weight, float standingScale,
				inout float4 flow, inout float2 standing, inout float foamNoise, inout float2 foamSlope, inout float foamDissolve )
{
	float pattern;
	flow += weight * flowRipple( tiles, xz, surface, 1.0f, dX, dY, pattern );
	[branch] if( standingScale > 0.0f )
		standing += weight * standingWaves( tiles, xz, surface, standingScale, dX, dY );
	[branch] if( foam )
	{
		[branch] if( g_foamTextureRepeat > 0.0f )
		{
			float2 slope;
			float dissolve;
			foamNoise += weight * foamTexturePattern( tiles, xz, surface, dX, dY, slope, dissolve );
			foamSlope += weight * slope;
			foamDissolve += weight * ( dissolve - 0.5f );
		}
		else
		{
			flowRipple( tiles, xz, surface, foamFrequency, dX, dY, pattern );
			foamNoise += weight * pattern;
		}
	}
}

// Искры: рябь мельче пикселя — грани размером sparkleSize, наклон каждой — по распределению GGX с sparkleRoughness.
// Светило отражают в камеру грани, нормаль которых попала в конус диска вокруг h, — доля probability = D(h)·(n·h)·Ω/(4·(v·h));
// в пятне пикселя (или в грани, если она больше пикселя) таких в среднем λ = probability × граней в пятне. При λ < 1 пятно
// либо блестит (с вероятностью λ), либо нет, и блестящее ярче среднего в 1/λ раз — в среднем тот же блик, а вблизи и на
// краю россыпи он рассыпается на точки (дискретные грани, как у Zirr, Kaplanyan 2016 и Bowles, Wang 2015); при λ ≥ 1 —
// почти ровный блик с колебанием ~1/√λ. Случайное число — у ячейки поверхности, а не пикселя: при движении камеры искры на
// месте; ячейки — по ширине пятна пикселя (поперёк его длинной оси), два соседних уровня вперемешку, как мипы. Грань
// блестит sparkleLifetime, разгораясь и гаснув, затем ячейка берёт новое число — искры мерцают. Возвращает множитель блика
// с распределением граней (в среднем 1)
float sparkleWeight( float2 xz, float2 dX, float2 dY, float probability )
{
	const float area = max( abs( dX.x * dY.y - dX.y * dY.x ), 1e-12f );
	const float width = area / max( max( length( dX ), length( dY ) ), 1e-6f );
	const float level = max( log2( width / g_sparkleSize ), 0.0f );
	const float base = floor( level );
	const float facetArea = g_sparkleSize * g_sparkleSize;
	float weight = 0.0f;
	[unroll] for( int k = 0; k < 2; ++k )
	{
		const float cellSize = g_sparkleSize * exp2( base + k );
		const float lambda = probability * max( area, cellSize * cellSize ) / facetArea;
		const int2 cell = (int2)floor( xz / cellSize );
		const uint seed = rippleHash( (uint)cell.x + rippleHash( (uint)cell.y + rippleHash( (uint)base + (uint)k ) ) );
		const float phase = cb_gameTime / sparkleLifetime + ( seed & 0xffffu ) / 65536.0f;
		const float chance = ( rippleHash( seed + (uint)phase ) >> 8u ) / 16777216.0f;
		const float glow = sin( 3.14159265f * frac( phase ) );
		const float flash = 2.0f * glow * glow;
		const float sample = lambda < 1.0f ? ( chance < lambda ? flash / lambda : 0.0f ) : 1.0f + ( flash - 1.0f ) * rsqrt( lambda );
		weight += ( k == 0 ? 1.0f - ( level - base ) : level - base ) * sample;
	}
	return weight;
}

// Расстояние вдоль оси взгляда до сцены за пикселем; над небом — далеко
float sceneViewDepthAt( float4 position )
{
	const float sceneDepth = g_sceneDepth[uint2( position.xy )];
	return sceneDepth > 0.0f ? viewDepth( sceneDepth ) : 1e4f;
}

// Яркость поверхности воды с экспозицией прошлого кадра — в буфер сцены
float4 shadeWater( WaterShading input )
{
	const float speed = length( input.velocity );
	const float3 toCamera = cb_cameraPosition - input.worldPosition;
	const float surfaceDistance = length( toCamera );
	const float3 view = toCamera / surfaceDistance;

	// Нормаль: наклон поверхности по уровню (перекаты) и рябь — течение несёт её и тянет вдоль себя, стоячую воду рябит
	// ветер
	const float2 dX = ddx( input.worldPosition.xz );
	const float2 dY = ddy( input.worldPosition.xz );
	const float flowStrength = rippleSlope * g_rippleStrength * max( saturate( speed / 1.5f ), input.ripple ) *
							   smoothstep( rippleOnsetStart, rippleOnsetFull, speed );
	// Пена: узор — тот же, который несёт течение, порогом по доле пены. Вместе с рябью, до отражения: плитки ряби дальше не
	// нужны и не занимают регистры в цикле луча (иначе вода на пол-экрана — на 0,1 мс дольше)
	const float edge = saturate( input.depth / edgeDepth );
	const float foamAmount = saturate( input.foam ) * edge;
	float4 flow = 0.0f;
	float foam = 0.0f;
	// Стоячие волны над перекатами: сила по числу Фруда и глубине пикселя, длина — из дисперсии; на медленной воде их нет
	float standingStrength = 0.0f;
	float standingScale = 0.0f;
	[branch] if( g_standingWaves > 0.0f && speed > standingMinSpeed )
	{
		const float froude = speed / sqrt( gravityAcceleration * max( input.depth, 0.01f ) );
		standingStrength = smoothstep( standingFroudeStart, standingFroudeFull, froude ) * saturate( input.depth / standingMinDepth - 1.0f ) *
						   standingSlope * g_standingWaves;
		if( standingStrength > 0.0f )
			standingScale = standingWavenumber( speed, input.depth ) / ( 6.2831853f * standingRepeat );
	}
	float2 standing = 0.0f;
	float2 foamSlope = 0.0f;
	float foamShade = 1.0f;
	float foamDissolve = 0.0f;
	// Уклон поверхности без ряби: узор ряби и пены — по длине вдоль неё
	const float2 surfaceStretch = surfaceScale( -input.normal.xz / max( input.normal.y, 0.2f ), input.velocity );
	[branch] if( flowStrength > rippleMinSlope || foamAmount > 0.0f )
	{
		// Плитки 0,5 м детальной воды и 1 м — у края набора детальных плиток вместе: узоры несвязаны, смешение — с делением
		// на √Σw², как у плиток
		const float w = input.detailRipple;
		const bool needFoam = foamAmount > 0.0f;
		float foamNoise = 0.0f;
		[branch] if( w > 0.0f )
			addRipple( detailRippleTiles( input.worldPosition.xz, input.velocity, input.detailTexel ), input.worldPosition.xz, surfaceStretch,
					   dX, dY, needFoam, w, standingScale, flow, standing, foamNoise, foamSlope, foamDissolve );
		[branch] if( w < 1.0f )
			addRipple( rippleTiles( input.worldPosition.xz, input.velocity ), input.worldPosition.xz, surfaceStretch, dX, dY, needFoam, 1.0f - w,
					   standingScale, flow, standing, foamNoise, foamSlope, foamDissolve );
		const float normalization = rsqrt( w * w + ( 1.0f - w ) * ( 1.0f - w ) );
		flow *= normalization * flowStrength;
		// Стоячие волны — не по силе ряби течения, а свои: длинные, отражение берега по ним тоже колышется
		flow.xy += standing * normalization * standingStrength;
		[branch] if( needFoam )
		{
			// Текстура: маска — толщина пены, край шире (тонкие нити полупрозрачны), тонкая пена сероватее
			const bool textured = g_foamTextureRepeat > 0.0f;
			const float pattern = textured ? 0.5f + foamNoise * normalization : 0.5f + 0.5f * foamPatternScale * foamNoise * normalization;
			foamSlope *= normalization;
			if( textured )
				foamShade = lerp( foamThinShade, 1.0f, saturate( pattern ) );
			// Растворение у края: порог сдвинут шумом из текстуры (он плывёт с пеной) — где доля пены сходит на нет, пена тает
			// кусками на разных расстояниях, а не по линии
			const float threshold = 1.0f - foamAmount + ( textured ? g_foamDissolve * foamDissolve * normalization : 0.0f );
			// На тонкой воде пена реже и прозрачнее: в пару сантиметров ей не на чем держаться
			const float softness = textured ? g_foamSoftness : foamPatternSoftness;
			foam = smoothstep( threshold, threshold + softness, pattern ) * foamAmount * saturate( input.depth / 0.05f );
		}
	}
	// Ветровая рябь — пятнами порывов поля ветра («кошачьи лапы» бегут по воде); в штиль (сила 0: флажок ветра, -nowind)
	// её нет, рябь течения остаётся. Сила — по полю ветра по рельефу (× множитель²): у подветренного леса вода гладкая,
	// на открытом озере рябь сильнее; узор бежит по ветру уровня (поле над водой почти однородно, а направление, меняющееся
	// от точки к точке, со временем растянуло бы узор)
	const float wind = cb_windStrength > 0.0f ? windGust( input.worldPosition.xz ) * windLoad( windAt( input.worldPosition.xz ) ) : 0.0f;
	const float calmStrength = rippleSlope * g_calmRipple * wind;
	float4 calm = 0.0f;
	[branch] if( calmStrength > 0.0f )
		calm = calmRipple( input.worldPosition.xz, calmOctave, dX, dY ) * calmStrength;
	const float2 slope = flow.xy + calm.xy + ( flow.zw + calm.zw ) * rippleFineWeight;
	const float3 normal = normalize( input.normal - float3( slope.x, 0.0f, slope.y ) );
	// Луч экранного отражения — по крупной октаве ряби: оно берёт цвет сцены в одной точке, шероховатость его не размывает,
	// и мелкая рябь разносила бы отражение берега в тонкие кольца
	const float2 smoothSlope = flow.xy + calm.xy;
	const float3 reflectionNormal = normalize( input.normal - float3( smoothSlope.x, 0.0f, smoothSlope.y ) );
	const float NV = saturate( dot( normal, view ) );

	// Путь луча в воде — от поверхности до того, что за ней (глубина сцены); над небом — далеко
	const float surfaceViewZ = mul( float4( input.worldPosition, 1.0f ), cb_viewMatrix ).z;
	const float behindViewZ = sceneViewDepthAt( input.position );
	const float path = max( behindViewZ - surfaceViewZ, 0.0f ) * surfaceDistance / max( surfaceViewZ, 1e-3f );

	// Преломление: выборка цвета сцены сдвинута по нормали — если там то, что ближе воды, без сдвига
	uint width, height;
	g_sceneColor.GetDimensions( width, height );
	float2 refractedUV = ( input.position.xy + normal.xz * g_refraction * saturate( path ) * height ) / float2( width, height );
	const uint2 refractedPixel = min( uint2( refractedUV * float2( width, height ) ), uint2( width, height ) - 1 );
	if( g_sceneDepth[refractedPixel] > input.position.z )
		refractedUV = input.position.xy / float2( width, height );
	const float3 behind = withoutFogging( g_sceneColor.SampleLevel( g_SamplerLinearClamp, refractedUV, 0.0f ).rgb, input.worldPosition );

	// Толща: свет из-за воды гаснет по пути, а рассеянный в воде свет неба его заменяет
	const float3 transmittance = exp( -g_absorption * path );
	const float3 inscatter = g_scatterColor * ambientIrradiance( float3( 0.0f, 1.0f, 0.0f ) );
	const float3 body = behind * transmittance + inscatter * ( 1.0f - transmittance ) * edge;

	// Отражение: блик солнца с тенью и отражённое окружение — сцена лучом по экрану, где он попал, иначе небо
	Surface surface;
	surface.position = input.worldPosition;
	surface.normal = normal;
	surface.geometricNormal = input.normal;
	surface.baseColor = 0.0f;
	surface.metallic = 0.0f;
	surface.specular = 1.0f;
	surface.roughness = g_roughness;
	surface.occlusion = 1.0f;
	surface.microShadows = 0.0f;
	surface.selfShadow = 1.0f;
	surface.screenOcclusion = 1.0f;
	surface.emissive = 0.0f;
	surface.transmission = 0.0f;
	surface.transmissionColor = 0.0f;
	surface.transmissionForward = 0.0f;
	surface.transmissionForwardRoughness = 1.0f;
	const float roughness = clamp( g_roughness, minRoughness, 1.0f );
	float lightShadow;
	float3 sun = evaluateDirectLighting( surface, view, waterF0, 0.0f, 0.0f, roughness, lightShadow );
	// Искры: доля блика светила (солнца, ночью — луны) — от граней ряби мельче пикселя с распределением шире, по точкам
	[branch] if( g_sparkle > 0.0f && g_shadowSunIndex >= 0 && lightShadow > 0.0f )
	{
		const Light light = g_lights[g_shadowSunIndex];
		const float3 toLight = -light.direction;
		const float3 h = normalize( toLight + view );
		const float NH = saturate( dot( normal, h ) );
		const float sparkleRoughness = max( g_sparkleRoughness, minRoughness );
		const float probability = GGX_Distribution( NH, sparkleRoughness * sparkleRoughness ) * NH * lightSolidAngle /
								  ( 4.0f * max( dot( view, h ), 1e-3f ) );
		const float3 smoothHighlight = CookTorrance_GGX( normal, toLight, view, waterF0, roughness, 0.0f );
		const float3 facetHighlight = CookTorrance_GGX( normal, toLight, view, waterF0, sparkleRoughness, 0.0f ) *
									  sparkleWeight( input.worldPosition.xz, dX, dY, probability );
		sun += g_sparkle * ( facetHighlight - smoothHighlight ) * light.color * lightShadow;
	}
	const float3 r = upwardReflection( reflect( -view, normal ) );
	const float2 environment = environmentBRDF( NV, roughness );
	const float environmentWeight = waterF0 * environment.x + environment.y;
	const float4 screen = screenReflection( input.position, view, normalize( input.normal ), reflectionNormal );
	// Где луч по экрану не попал — луч по карте отражений места (рельеф, лес, модели, горы вокруг), за её радиусом — небо
	float3 sky = 0.0f;
	[branch] if( screen.a < 1.0f )
		sky = environmentSpecular( input.worldPosition, r, roughness ) * environmentWeight * ( 1.0f - screen.a );
	const float3 reflection = sun + sky + withoutFogging( screen.rgb, input.worldPosition ) * environmentWeight * screen.a;

	const float fresnel = waterF0 + ( 1.0f - waterF0 ) * pow( 1.0f - NV, 5.0f );
	float3 color = body * ( 1.0f - fresnel * edge ) + reflection * edge;

	// Пена — белая шероховатая поверхность с прямым светом и светом неба (у текстуры — рельеф по её нормали)
	[branch] if( foam > 0.0f )
	{
		const float3 albedo = foamAlbedo * foamShade;
		surface.baseColor = albedo;
		surface.roughness = foamRoughness;
		surface.normal = normalize( float3( -foamSlope.x, 1.0f, -foamSlope.y ) );	// у узора по ряби — плоская
		const float3 foamLight = evaluateDirectLighting( surface, view, 0.04f, albedo, 0.0f, foamRoughness ) +
								 albedo * ambientIrradiance( surface.normal );
		color = lerp( color, foamLight, foam );
	}
	// Яркость, кд/м², — за туманом и воздухом, в буфер сцены с экспозицией прошлого кадра
	return float4( applyFogging( color, input.worldPosition ) * preExposure(), 1.0f );
}

#endif
