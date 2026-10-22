////////////////////////////////////////////////////////////////////////////////
// Дальние горы вокруг карты (FarTerrain, docs/far_terrain.md): высота и материал. Карта гор — своя грубая карта высот в
// метрах (тексель — квад уровня g_farMipBias); у карты высота переходит в её высоту: h = lerp( карта, горы, w ), w — 0 у края
// карты, 1 — за g_farBlendDistance. На краю w = 0 ровно, и высоту карты вершина считает тем же кодом, что рельеф карты
// (sampleHeight, cdlod_patch.sh): трещин нет. Материал — правила по высоте и уклону на слоях террейна по ролям и ледники наших
// дней по толщине льда, у карты — её слои (splat-карта, край продолжен наружу)
////////////////////////////////////////////////////////////////////////////////

#ifndef FAR_TERRAIN_SH
#define FAR_TERRAIN_SH

#include "cdlod.sh"
#include "terrain_layers.sh"
#include "far_terrain_constants.h"

DM_SRV( Texture2D, g_farHeightMap, 1 );	// карта гор: R32_FLOAT в метрах с полной цепочкой мипов, строки с севера
// Ледники наших дней, как карта гор (RGBA8, FarTerrain::createGlacierTexture): R — толщина льда / 255 м, G — доля линий
// обломков (морены), B, A — течение: уклон поверхности льда вниз по течению по x и z, усреднённый по льду (−1…1 → 0…1)
DM_SRV( Texture2D, g_farGlacier, 2 );

float2 farHeightMapUV( float2 worldXZ )
{
	float2 uv = ( worldXZ - g_farOrigin ) / g_farExtent;
	uv.y = 1.0f - uv.y;
	return uv;
}

float sampleFarHeight( float2 worldXZ, float mip )
{
	return g_farHeightMap.SampleLevel( g_SamplerLinearClamp, farHeightMapUV( worldXZ ), mip ).r;
}

// Расстояние до квадрата карты [0, g_worldSize]², м; внутри — 0
float distanceToMap( float2 worldXZ )
{
	const float2 outside = max( max( -worldXZ, worldXZ - g_worldSize ), 0.0f );
	return length( outside );
}

// Доля гор в высоте: 0 у края карты, 1 — за g_farBlendDistance
float farBlend( float2 worldXZ )
{
	return smoothstep( 0.0f, g_farBlendDistance, distanceToMap( worldXZ ) );
}

// Высота вершины уровня level ≥ 0: карта — её мип level (край продолжен наружу), горы — мип level − g_farMipBias
float farHeight( float2 worldXZ, float level )
{
	precise float map = sampleHeight( worldXZ, level );
	const float far = sampleFarHeight( worldXZ, max( level - g_farMipBias, 0.0f ) );
	precise float height = lerp( map, far, farBlend( worldXZ ) );
	return height;
}

// Самая детальная земля (уровни мельче листа): у карты — её детальная земля, горы — мип 0
float farFineHeight( float2 worldXZ )
{
	precise float map = sampleFineHeight( worldXZ );
	const float far = sampleFarHeight( worldXZ, 0.0f );
	precise float height = lerp( map, far, farBlend( worldXZ ) );
	return height;
}

// Веса Catmull-Rom (интерполирующий кубический сплайн: проходит через значения текселей, гребни не сглаживает) и их
// производные по доле f между текселями 1 и 2 из четырёх
float4 catmullRomWeights( float f )
{
	const float f2 = f * f;
	const float f3 = f2 * f;
	return float4( -0.5f * f3 + f2 - 0.5f * f, 1.5f * f3 - 2.5f * f2 + 1.0f, -1.5f * f3 + 2.0f * f2 + 0.5f * f, 0.5f * f3 - 0.5f * f2 );
}

float4 catmullRomSlopes( float f )
{
	const float f2 = f * f;
	return float4( -1.5f * f2 + 2.0f * f - 0.5f, 4.5f * f2 - 5.0f * f, -4.5f * f2 + 4.0f * f + 0.5f, 1.5f * f2 - f );
}

// Карта гор (мип 0) бикубическим Catmull-Rom по 16 текселям: высота и её наклон по x и z мира (м на м). У билинейной на каждой
// границе текселя излом — тексели 32 м видны сеткой; B-сплайн сглаживает гребни. Мип 0 и вдали: пиксель мельче текселя 32 м
// до ~60 км (1080p)
float farHeightCubic( float2 worldXZ, out float2 slope )
{
	uint width, height;
	g_farHeightMap.GetDimensions( width, height );
	const float2 texel = farHeightMapUV( worldXZ ) * float2( width, height ) - 0.5f;
	const float2 index = floor( texel );
	const float2 f = texel - index;
	const float4 wx = catmullRomWeights( f.x );
	const float4 wy = catmullRomWeights( f.y );
	const float4 dx = catmullRomSlopes( f.x );
	const float4 dy = catmullRomSlopes( f.y );
	const int2 last = int2( width, height ) - 1;
	float value = 0.0f;
	float2 gradient = 0.0f;		// по текселям: x — на восток, y — вниз по текстуре (на юг)
	[unroll] for( int j = 0; j < 4; ++j )
	{
		float4 row;
		[unroll] for( int i = 0; i < 4; ++i )
			row[i] = g_farHeightMap.Load( int3( clamp( int2( index ) + int2( i - 1, j - 1 ), 0, last ), 0 ) ).r;
		value += wy[j] * dot( wx, row );
		gradient.x += wy[j] * dot( dx, row );
		gradient.y += dy[j] * dot( wx, row );
	}
	const float cell = g_farExtent / width;
	slope = float2( gradient.x, -gradient.y ) / cell;
	return value;
}

// Высота поверхности для нормали у края карты: мип карты по шагу step, м, горы — бикубически
float farSurfaceHeight( float2 worldXZ, float step )
{
	float2 unused;
	const float mapMip = max( log2( step / g_texelSize ), 0.0f );
	return lerp( sampleHeight( worldXZ, mapMip ), farHeightCubic( worldXZ, unused ), farBlend( worldXZ ) );
}

// Нормаль поверхности гор: дальше полосы перехода — аналитически по бикубической карте гор; в полосе (смесь с картой) —
// центральные разности через step, м
float3 farNormal( float2 worldXZ, float step )
{
	[branch] if( farBlend( worldXZ ) >= 1.0f )
	{
		float2 slope;
		farHeightCubic( worldXZ, slope );
		return normalize( float3( -slope.x, 1.0f, -slope.y ) );
	}
	const float left  = farSurfaceHeight( worldXZ - float2( step, 0.0f ), step );
	const float right = farSurfaceHeight( worldXZ + float2( step, 0.0f ), step );
	const float back  = farSurfaceHeight( worldXZ - float2( 0.0f, step ), step );
	const float front = farSurfaceHeight( worldXZ + float2( 0.0f, step ), step );
	return normalize( float3( left - right, 2.0f * step, back - front ) );
}

// Доля по высоте: 0 ниже range.x, 1 выше range.y, плавно
float heightShare( float2 range, float height )
{
	return smoothstep( range.x, max( range.y, range.x + 1e-3f ), height );
}

// Доля по уклону: 0 положе range.x, 1 круче range.y (range — косинусы, x > y), slope — косинус нормали с вертикалью
float slopeShare( float2 range, float slope )
{
	return smoothstep( 0.0f, 1.0f, saturate( ( range.x - slope ) / max( range.x - range.y, 1e-5f ) ) );
}

// Крупные пятна (≈ −1…1): шум земли (terrain_layers.sh, 256², мип 0) с текселем texel, м, и вдвое мельче. Тексель — сотни
// метров: мельче мип 0 вдали даёт решётку (алиасинг)
float farNoise( float2 worldXZ, float texel )
{
	const float2 uv = worldXZ / ( 256.0f * texel );
	return g_terrainNoise.SampleLevel( g_SamplerLinearWrap, uv, 0.0f ).r * 0.65f +
		   g_terrainNoise.SampleLevel( g_SamplerLinearWrap, uv * 2.0f + 0.37f, 0.0f ).r * 0.35f;
}

void addRuleWeight( inout float weights[maxLayers], int layer, float weight )
{
	[unroll] for( uint i = 0; i < maxLayers; ++i )
		weights[i] += (int)i == layer ? weight : 0.0f;
}

// Ледник наших дней в точке (мип — по размеру пикселя)
struct FarGlacier
{
	float thickness;	// толщина льда, м
	float debris;		// доля линий обломков
	float2 downhill;	// течение: уклон поверхности льда вниз по течению (x, z мира), м на м
};

FarGlacier farGlacier( float2 worldXZ )
{
	const float4 texel = g_farGlacier.Sample( g_SamplerLinearClamp, farHeightMapUV( worldXZ ) );
	FarGlacier glacier;
	glacier.thickness = texel.r * 255.0f;
	glacier.debris = texel.g;
	glacier.downhill = texel.ba * 2.0f - 1.0f;
	return glacier;
}

// Случайное 0…1 по целой точке (хеш с перемешиванием битов, как у PCG)
float farHash( int2 cell )
{
	uint value = uint( cell.x ) * 1664525u + uint( cell.y ) * 1013904223u + 374761393u;
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	value ^= value >> 16;
	return float( value ) / 4294967295.0f;
}

// Трещины поперёк течения льда — тёмные рваные полосы с шагом g_crevasseSpacing: downhill — течение (FarGlacier), полоса —
// где координата вдоль течения проходит середину шага, отрезки — по шуму вдоль трещины. Координата вдоль течения — в своих
// ячейках по четыре шага со своим началом: от одного начала на километры поворот течения сдвигал бы фазу полос, между
// ячейками — смесь четырёх соседних. footprint — размер пикселя, м: там, где шаг — единицы пикселей, полосы переходят в свою
// среднюю долю (без ряби). Результат — доля трещины 0…1
float farCrevasses( float2 worldXZ, float2 downhill, float footprint )
{
	const float average = 0.08f;	// средняя доля: полоса ~15 % шага, отрезки — около половины длины
	const float horizontal = length( downhill );
	const float scale = footprint / g_crevasseSpacing;
	const float detail = 1.0f - smoothstep( 0.1f, 0.35f, scale );
	[branch] if( detail <= 0.0f || horizontal < 1e-3f )
		return average;
	const float2 along = downhill / horizontal;
	const float2 across = float2( -along.y, along.x );
	const float cellSize = 4.0f * g_crevasseSpacing;
	const float2 grid = worldXZ / cellSize - 0.5f;
	const float2 base = floor( grid );
	const float2 f = grid - base;
	const float edge = max( 0.05f, scale );	// край полосы не тоньше пикселя
	float lines = 0.0f;
	[unroll] for( int k = 0; k < 4; ++k )
	{
		const int2 corner = int2( k & 1, k >> 1 );
		const int2 cell = int2( base ) + corner;
		const float2 local = worldXZ - ( float2( cell ) + 0.5f ) * cellSize;
		const float phase = farHash( cell );
		const float s = dot( local, along ) / g_crevasseSpacing + 7.0f * phase;
		const float t = dot( local, across ) / g_crevasseSpacing;
		const float stripe = floor( s );
		const float gap = g_terrainNoise.SampleLevel( g_SamplerLinearWrap,
			float2( 0.7f * t + 13.7f * stripe, 7.3f * stripe + 256.0f * phase ) / 256.0f, 0.0f ).r;
		const float crack = 1.0f - smoothstep( 0.04f, 0.04f + edge, abs( frac( s ) - 0.5f ) );
		const float2 corners = lerp( 1.0f - f, f, float2( corner ) );
		lines += corners.x * corners.y * crack * smoothstep( -0.1f, 0.2f, gap );
	}
	return lerp( average, lines, detail );
}

// Веса восьми слоёв по правилам гор: трава долины → сухой луг по высоте, поверх — осыпи по уклону выше их высоты, скала по
// уклону, снег по высоте (к северу ниже, граница пятнами) там, где он держится; поверх всего — ледник (ice —
// farGlacier): слой снега на любом уклоне, ниже линии фирна на нём — морены слоем осыпей (выше линии обломки погребены
// снегом). forest — где лес может расти по покрову: на некрутых склонах, без скалы, осыпей, снега и льда (граница леса,
// долины, поляны и лавинные лотки — в плотности леса, far_forest_density.cs); bareIce —
// доля голого льда (оттенок g_glacierIceTint): ниже линии фирна и там, где снег не держится, без снега по правилу и морен;
// glacier — доля ледника
void farRuleWeights( float3 position, float3 normal, FarGlacier ice, out float weights[maxLayers], out float forest,
					 out float bareIce, out float glacier )
{
	[unroll] for( uint i = 0; i < maxLayers; ++i )
		weights[i] = 0.0f;
	const float height = position.y;
	const float slope = normal.y;
	const float horizontal = length( normal.xz );
	const float northFacing = saturate( normal.z / max( horizontal, 1e-4f ) ) * saturate( horizontal * 5.0f );

	const float meadow = heightShare( g_meadowHeight, height );
	const float scree = slopeShare( g_screeSlope, slope ) * heightShare( g_screeHeight, height );
	const float rock = slopeShare( g_rockSlope, slope );
	const float snowNoise = farNoise( position.xz, 500.0f );
	const float snowHolds = 1.0f - slopeShare( g_snowSlope, slope );
	const float snowLine = g_snowHeight.x - g_snowNorth * northFacing + g_snowNoise * snowNoise;
	const float snow = heightShare( float2( snowLine, snowLine + g_snowHeight.y - g_snowHeight.x ), height ) * snowHolds;
	// Ледник: снег на нём — выше линии фирна (граница — пятнами вполовину снеговой) или по правилу снега, на крутом — голый лёд;
	// ниже линии фирна — морены
	glacier = heightShare( g_glacierCover, ice.thickness );
	const float firnLine = heightShare( float2( g_glacierFirnLine - 50.0f, g_glacierFirnLine + 50.0f ), height + 0.5f * g_snowNoise * snowNoise );
	const float firn = firnLine * snowHolds;
	const float moraine = heightShare( g_moraineCover, ice.debris ) * ( 1.0f - firnLine );
	bareIce = glacier * ( 1.0f - max( snow, firn ) ) * ( 1.0f - moraine );

	// Каждый следующий — поверх: остальные уступают ему свою долю
	float grassWet = 1.0f - meadow;
	float grassDry = meadow;
	grassWet *= 1.0f - scree;
	grassDry *= 1.0f - scree;
	const float keepRock = 1.0f - rock;
	const float keepSnow = 1.0f - snow;
	const float keepIce = 1.0f - glacier;
	addRuleWeight( weights, g_grassWetLayer, grassWet * keepRock * keepSnow * keepIce );
	addRuleWeight( weights, g_grassDryLayer, grassDry * keepRock * keepSnow * keepIce );
	addRuleWeight( weights, g_talusLayer, scree * keepRock * keepSnow * keepIce + glacier * moraine );
	addRuleWeight( weights, g_rockLayer, rock * keepSnow * keepIce );
	addRuleWeight( weights, g_snowLayer, snow * keepIce + glacier * ( 1.0f - moraine ) );

	forest = ( 1.0f - slopeShare( g_forestSlope, slope ) ) * ( 1.0f - scree ) * keepRock * keepSnow * keepIce;
}

#endif
