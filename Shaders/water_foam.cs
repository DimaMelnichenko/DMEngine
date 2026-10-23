////////////////////////////////////////////////////////////////////////////////
// Поле пены (WaterFoam, docs/water.md, «Пена»): пена, которую несёт течение. Течение статичной воды установившееся, поэтому
// и поле пены считается при загрузке — после течения и при правке настроек пены, — а поверхность берёт его одной выборкой.
// Источник — где вода быстрая, где скорость резко меняется (норма тензора скоростей деформации, Neyret 2003, «Advected
// Textures»: вращение воды целиком пены не даёт), на нижней части падения со ступени и в котле под ним (верх падения —
// чистый) и где камень русла выходит к поверхности (вода над ним тоньше stoneFoamDepth). Пена у источника
// растёт к нему, переносится течением (назад по течению, полулагранжево, Stam 1999) и тает за время жизни (в медленной
// воде — дольше: в бурной пузыри лопаются и пену перемешивает, в заводях и плёсах она держится плотами); где течение
// сходится (∇·u < 0: ручей входит в плёс и замедляется), она густеет, где расходится — редеет. Итераций — на ~5 времён
// жизни: дальше поле не меняется. Сначала детальная сетка у русел (0,25 м, плитки детальной воды), затем сетка 1 м: в
// клетках у русел её источник — пена детальной сетки, и шлейф ручья продолжается в озеро за краем плиток.
//   mainDetailSource / mainCoarseSource — источник пены;
//   mainDetailStep / mainCoarseStep     — шаг: перенос, таяние, сжатие, рост к источнику.
// Правила источника и шага — water_foam.sh (их же берёт пена окна у камеры, water_near_foam.cs)
// Группа потоков, ячейки и тексели — water_detail_grid.sh
////////////////////////////////////////////////////////////////////////////////

#include "water_detail_grid.sh"

DM_SRV( Texture2DArray<float>, g_detailFoam, 5 );			// пена детальной сетки: прошлый шаг (mainDetailStep), итог (mainCoarseSource)
DM_SRV( Texture2D<float>, g_coarseFoam, 6 );				// пена сетки 1 м: прошлый шаг
DM_SRV( Texture2DArray<float>, g_detailSource, 7 );
DM_SRV( Texture2D<float>, g_coarseSource, 8 );
DM_SRV( Texture2DArray<float2>, g_detailStones, 9 );	// верх над ячейкой детальной сетки, м (−1e9 — нет): x — камня русла, y — валуна
DM_SRV( Texture2D<float2>, g_stoneArea, 10 );				// камни русла сетки 1 м: площадь поперёк потока на м² дна

DM_UAV( RWTexture2DArray<float>, g_detailOutput, 0 );
DM_UAV( RWTexture2D<float>, g_coarseOutput, 1 );

#include "water_foam.sh"

// Падение — шагами по текселю детальной сетки (0,25 м), до метра в каждую сторону
static const uint fallSteps = 4;

// Уровень детальной воды в точке мира (тексель под ней); сухо или плитки нет — fallback (water_foam.sh, traceFall)
float fallLevelAt( float2 world, float fallback )
{
	float3 texel;
	if( !detailTexelAt( world, texel ) )
		return fallback;
	const float4 water = g_detailWater.Load( int4( int2( texel.xy ), texel.z, 0 ) );
	return wetDetail( water ) ? water.x : fallback;
}

// Пена прошлого шага в точке мира; вне плиток (озеро вдали от русел) пены детальной сетки нет
float detailFoamAt( float2 world )
{
	float3 texel;
	return detailTexelAt( world, texel ) ? g_detailFoam.SampleLevel( g_SamplerLinearClamp, detailUV( texel ), 0.0f ) : 0.0f;
}

// Соседи текселя детальной воды через тексель: +X, −X, +Z, −Z мира (строки — по Z); сухой — как сам тексель
void detailNeighbors( uint3 texel, float4 here, out float4 neighbors[4] )
{
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = k == 0 ? int2( 1, 0 ) : k == 1 ? int2( -1, 0 ) : k == 2 ? int2( 0, 1 ) : int2( 0, -1 );
		const float4 neighbor = g_detailWater.Load( int4( int2( texel.xy ) + offset, texel.z, 0 ) );
		neighbors[k] = wetDetail( neighbor ) ? neighbor : here;
	}
}

[numthreads( 8, 8, 1 )]
void mainDetailSource( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	uint3 texel;
	int2 cell;
	float2 world;
	if( !detailCell( group, thread, texel, cell, world ) )
		return;
	const float4 water = g_detailWater.Load( int4( texel, 0 ) );
	float source = 0.0f;
	if( wetDetail( water ) )
	{
		float4 n[4];
		detailNeighbors( texel, water, n );
		// Скачок скорости через тексель — на 2 клетки сетки 1 м, как у неё: градиент на 0,25 м круче (профиль поперёк узкого
		// ручья — 1 м/с на 30–40 см), и пена с тем же порогом шла бы по всему ручью
		const float2 dvdx = ( n[0].yz - n[1].yz ) / ( 2.0f * g_gridCellSize );
		const float2 dvdz = ( n[2].yz - n[3].yz ) / ( 2.0f * g_gridCellSize );
		const float speed = length( water.yz );
		source = foamSource( speed, strainRate( dvdx, dvdz ) );
		// Падение со ступени: ближе foamFallStart к кромке — чистая вода (и быстрая), ниже — пена, у подошвы и в котле — пена
		// падения
		if( speed > 1e-3f )
		{
			const Fall fall = traceFall( world, water.yz / speed, water.x, detailStep(), fallSteps );
			source = source * fallFoamShare( fall ) + fallFoam( fall );
		}
		// Камень у поверхности: уровень воды против верха камня (уровень поля не знает о камнях — над камнем вода тоньше)
		const float2 stones = g_detailStones.Load( int4( texel, 0 ) );
		const float stoneTop = max( stones.x, stones.y );
		if( stoneTop > -1e8f )
			source += saturate( 1.0f - ( water.x - stoneTop ) / stoneFoamDepth ) * saturate( length( water.yz ) / stoneFoamSpeed );
	}
	g_detailOutput[texel] = source;
}

[numthreads( 8, 8, 1 )]
void mainDetailStep( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	uint3 texel;
	int2 cell;
	float2 world;
	if( !detailCell( group, thread, texel, cell, world ) )
		return;
	const float4 water = g_detailWater.Load( int4( texel, 0 ) );
	float foam = 0.0f;
	if( wetDetail( water ) )
	{
		// Назад по течению — по скорости в середине шага
		const float2 middle = detailVelocityAt( world - 0.5f * g_foamTimeStep * water.yz );
		const float carried = detailFoamAt( world - g_foamTimeStep * middle );
		float4 n[4];
		detailNeighbors( texel, water, n );
		const float divergence = ( n[0].y - n[1].y + n[2].z - n[3].z ) / ( 2.0f * detailStep() );
		foam = foamStep( carried, divergence, g_detailSource.Load( int4( texel, 0 ) ), length( water.yz ) );
	}
	uint3 copies[4];
	const uint count = detailCopies( cell, copies );
	for( uint c = 0; c < count; ++c )
		g_detailOutput[copies[c]] = foam;
}

// Соседи клетки сетки 1 м: +X, −X, +Z, −Z мира (строки — против Z); сухая — как сама клетка
void coarseNeighbors( int2 cell, float4 here, out float4 neighbors[4] )
{
	[unroll] for( uint k = 0; k < 4; ++k )
	{
		const int2 offset = k == 0 ? int2( 1, 0 ) : k == 1 ? int2( -1, 0 ) : k == 2 ? int2( 0, -1 ) : int2( 0, 1 );
		const int2 at = clamp( cell + offset, 0, (int)g_gridSize - 1 );
		const float4 neighbor = g_coarseWater.Load( int3( at, 0 ) );
		neighbors[k] = neighbor.x >= g_gridWetDepth ? neighbor : here;
	}
}

[numthreads( 8, 8, 1 )]
void mainCoarseSource( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	float2 world;
	if( !coarseCell( group, thread, cell, world ) )
		return;
	const float4 water = g_coarseWater.Load( int3( cell, 0 ) );
	float source = 0.0f;
	if( water.x >= g_gridWetDepth )
	{
		// У русел (клетка в плитках детальной воды) — пена детальной сетки, средняя по четырём точкам клетки: шлейф ручья
		// продолжается в озеро за краем плиток; дальше — свои правила
		float detail = 0.0f;
		uint count = 0;
		if( g_gridDetail != 0 )
		{
			[unroll] for( uint k = 0; k < 4; ++k )
			{
				const float2 at = world + ( float2( k & 1, k >> 1 ) - 0.5f ) * 0.5f * g_gridCellSize;
				float3 texel;
				if( detailTexelAt( at, texel ) )
				{
					detail += g_detailFoam.SampleLevel( g_SamplerLinearClamp, detailUV( texel ), 0.0f );
					++count;
				}
			}
		}
		if( count > 0 )
			source = detail / count;
		else
		{
			float4 n[4];
			coarseNeighbors( cell, water, n );
			const float2 dvdx = ( n[0].yz - n[1].yz ) / ( 2.0f * g_gridCellSize );
			const float2 dvdz = ( n[2].yz - n[3].yz ) / ( 2.0f * g_gridCellSize );
			source = foamSource( length( water.yz ), strainRate( dvdx, dvdz ) );
			const float stones = saturate( g_stoneArea.Load( int3( cell, 0 ) ).x / stoneFoamArea );
			source += stones * saturate( 1.0f - water.x / stoneFoamDepth ) * saturate( length( water.yz ) / stoneFoamSpeed );
		}
	}
	g_coarseOutput[cell] = source;
}

[numthreads( 8, 8, 1 )]
void mainCoarseStep( uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID )
{
	int2 cell;
	float2 world;
	if( !coarseCell( group, thread, cell, world ) )
		return;
	const float4 water = g_coarseWater.Load( int3( cell, 0 ) );
	float foam = 0.0f;
	if( water.x >= g_gridWetDepth )
	{
		const float2 middle = g_coarseWater.SampleLevel( g_SamplerLinearClamp, coarseUV( world - 0.5f * g_foamTimeStep * water.yz ), 0.0f ).yz;
		const float carried = g_coarseFoam.SampleLevel( g_SamplerLinearClamp, coarseUV( world - g_foamTimeStep * middle ), 0.0f );
		float4 n[4];
		coarseNeighbors( cell, water, n );
		const float divergence = ( n[0].y - n[1].y + n[2].z - n[3].z ) / ( 2.0f * g_gridCellSize );
		foam = foamStep( carried, divergence, g_coarseSource.Load( int3( cell, 0 ) ), length( water.yz ) );
	}
	g_coarseOutput[cell] = foam;
}
