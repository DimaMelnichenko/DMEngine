////////////////////////////////////////////////////////////////////////////////
// Плотность леса дальних гор (FarTerrain::bakeForestDensity) — на сетке карты гор, по правилам гор и рельефу после эрозии
// (docs/far_terrain.md, «Лес на горах»): где лес может расти по покрову (farRuleWeights, far_terrain.sh: некрутые склоны без
// скалы, осыпей, снега и льда), к границе леса — островами, гуще в долинах и на пологом, реже выше по склонам и на гребнях,
// поляны пятнами, нет в лавинных лотках и у ледников. По ней слой расстановки far_forest ставит деревья на горах вокруг
// камеры (scatter.cs, far_ground.sh), дальше — тон леса в материале гор (far_terrain.ps): деревья и тон — одна плотность
////////////////////////////////////////////////////////////////////////////////

#include "far_terrain.sh"

DM_UAV( RWTexture2D<float>, g_farForestDensity, 0 );
// Рельеф для леса (FarForestRelief), как карта гор: R — log10 водосбора, м²; G — доля лавинного лотка
DM_SRV( Texture2D<float2>, g_farForestRelief, 3 );

// Пятна шума 0…1, примерно поровну по значениям: farNoise — сумма двух октав сглаженного белого шума, значения собраны у
// нуля (σ ≈ 0,28: билинейный белый шум — 0,38 на октаву), и порог по доле площади (поляны, острова у границы леса) выходил в разы реже — растягиваем ±2σ на 0…1
float farNoise01( float2 worldXZ, float texel )
{
	return saturate( 0.5f + farNoise( worldXZ, texel ) / ( 4.0f * 0.28f ) );
}

[numthreads( 8, 8, 1 )]
void main( uint3 id : SV_DispatchThreadID )
{
	uint width, height;
	g_farHeightMap.GetDimensions( width, height );
	if( any( id.xy >= uint2( width, height ) ) )
		return;
	// Центр текселя карты гор (строки — с севера)
	const float cell = g_farExtent / float( width );
	const float2 worldXZ = g_farOrigin + float2( id.x + 0.5f, float( height - id.y ) - 0.5f ) * cell;
	const float3 position = float3( worldXZ.x, farFineHeight( worldXZ ), worldXZ.y );
	const float3 normal = farNormal( worldXZ, cell );
	// Лёд — мип 0 (farGlacier берёт мип по пикселю — в compute его нет)
	const float4 ice = g_farGlacier.SampleLevel( g_SamplerLinearClamp, farHeightMapUV( worldXZ ), 0.0f );
	FarGlacier glacier;
	glacier.thickness = ice.r * 255.0f;
	glacier.debris = ice.g;
	glacier.downhill = ice.ba * 2.0f - 1.0f;
	float weights[maxLayers];
	float forest;
	float bareIce;
	float glacierShare;
	farRuleWeights( position, normal, glacier, weights, forest, bareIce, glacierShare );
	const float2 uv = farHeightMapUV( worldXZ );

	// Граница леса (полоса «Forest line», гуляет пятнами на 150 м): к ней лес редеет островами — выше доли treeline по полосе
	// остаются только самые «лесные» пятна шума (около 100 м), над ней — ни одного
	const float treeline = heightShare( g_forestLine, position.y + 150.0f * farNoise( worldXZ, 300.0f ) );
	const float islands = smoothstep( 1.2f * treeline - 0.2f, 1.2f * treeline, farNoise01( worldXZ, 40.0f ) );
	// Долины и пологое — гуще: водосбор на мипе 1 (128 м — дно долины и нижние склоны над ним) и уклон положе 6…20°; выше по
	// склонам и на гребнях — доля «Forest upland»
	const float valley = smoothstep( g_forestValleyArea.x, g_forestValleyArea.y, g_farForestRelief.SampleLevel( g_SamplerLinearClamp, uv, 1.0f ).r );
	const float gentle = 1.0f - slopeShare( float2( 0.9945f, 0.9397f ), normal.y );
	const float relief = lerp( g_forestUpland, 1.0f, max( valley, gentle ) );
	// Поляны — пятнами крупного шума (около 600 м) с рваной опушкой (мелкий, около 100 м), доля — «Forest glades» (пятна выше
	// порога; в долинах — вдвое меньше): одним шумом в 300 м поляны выходили россыпью одинаковых дыр
	const float threshold = 1.0f - g_forestGlades * ( 1.0f - 0.5f * valley );
	const float patches = farNoise01( worldXZ + 517.0f, 250.0f ) + 0.25f * ( farNoise01( worldXZ - 211.0f, 40.0f ) - 0.5f );
	const float glade = smoothstep( threshold, threshold + 0.05f, patches );
	// Лавинные лотки и полоса у ледников (лёд на мипе с текселем около «Forest glacier gap»: средняя толщина рядом) — леса нет
	const float avalanche = g_farForestRelief.SampleLevel( g_SamplerLinearClamp, uv, 0.0f ).g;
	const float nearIce = g_farGlacier.SampleLevel( g_SamplerLinearClamp, uv, g_forestGlacierMip ).r * 255.0f;
	const float forefield = smoothstep( 0.5f, 3.0f, nearIce );
	g_farForestDensity[id.xy] = saturate( forest * islands * relief * ( 1.0f - glade ) * ( 1.0f - avalanche ) * ( 1.0f - forefield ) );
}
