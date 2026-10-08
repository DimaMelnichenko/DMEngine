////////////////////////////////////////////////////////////////////////////////
// Вода по рельефу — модель виртуальных труб (O'Brien, Hodgins 1995; Mei, Decaudin, Hu 2007, «Fast Hydraulic Erosion
// Simulation and Visualization on GPU»), как вода в From Dust. Сетка — тексели карты высот: в ячейке высота воды d и
// потоки в четырёх соседей (м³/с). Шаг — два прохода:
//   mainFlux  — вода с притоком за шаг d1 = d + dt · (дождь + источник); поток в соседа растёт на dt · g · l · Δh
//               (труба сечением l², длиной l), где Δh — разность уровней воды (рельеф + d1), и гасится трением дна
//               по Маннингу (в модели Mei трения нет: вода на склоне разгоняется без предела); отток ячейки ограничен
//               её водой (множитель K ≤ 1) — глубина не уходит ниже нуля. За краем карты уровень — рельеф ячейки
//               без воды: вода уходит с карты;
//   mainWater — глубина d1 + dt · (приток − отток) / l², скорость — по средним потокам через грани, минус испарение.
// Обе стадии пишут только свою ячейку и читают соседей из того, что другая стадия уже дописала, поэтому текстуры не
// удваиваются. Источники (mainSources) — один раз при загрузке и при смене их настроек: приток по карте водосбора,
// размытый вокруг линий стока. Класс WaterSimulation, docs/water.md
////////////////////////////////////////////////////////////////////////////////

#include "water_simulation.sh"

DM_SRV( Texture2D<float>, g_flowMap, 1 );		// водосбор, м² (mainSources)
DM_SRV( Texture2D<float>, g_sources, 2 );		// приток ячейки, м/с слоя воды
DM_SRV( StructuredBuffer<float4>, g_helpers, 4 );	// источники-помощники: x, z мира, расход м³/с, σ м (mainSources)
DM_SRV( Texture2D<float4>, g_staticWater, 3 );		// mainStatic: уровень воды ручья, м (−1e9 — нет); скорость X, Z мира

DM_UAV( RWTexture2D<float>, g_water, 0 );		// глубина воды, м
DM_UAV( RWTexture2D<float4>, g_flux, 1 );		// потоки к соседям −X, +X, −Y, +Y (по текселям), м³/с
DM_UAV( RWTexture2D<float4>, g_output, 2 );		// для шейдеров (SLOT_WATER): глубина, скорость по X и Z мира, глубина с памятью
DM_UAV( RWTexture2D<float>, g_sourcesOut, 3 );	// mainSources
DM_UAV( RWTexture2D<float>, g_fill, 4 );		// уровень заполненных низин, м (mainFill*, при загрузке)
DM_UAV( RWTexture2D<float>, g_lake, 5 );		// 1 — низина с водой при загрузке (mainLake*)
DM_UAV( RWTexture2D<float>, g_memory, 6 );		// глубина с памятью, м (mainWater): вода ушла — гаснет за wetMemoryTime

static const float minVelocityDepth = 1e-3f;	// мельче — скорость не считается: делить поток не на что
static const float minFrictionDepth = 1e-4f;	// глубина в трении — не ниже: деление на ноль у сухих ячеек
// Память воды, с: где вода была недавно, трава не растёт и земля мокрая — перекатывающийся край мелкой воды не
// заставляет траву исчезать и вырастать каждые несколько секунд
static const float wetMemoryTime = 30.0f;

// Вода ячейки с притоком этого шага
float waterWithInflow( int2 cell )
{
	return g_water[cell] + g_timeStep * ( g_rain + g_sources[cell] );
}

// Приток при загрузке: доля «русла» по водосбору (плавно от flowStart до flowFull в логарифме), размытая гауссом
// с нормированными весами — сумма притока вдоль линии стока не меняется, а ручей начинается из мокрого пятна
[numthreads( 8, 8, 1 )]
void mainSources( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	uint flowWidth, flowHeight;
	g_flowMap.GetDimensions( flowWidth, flowHeight );
	const float sigma = max( g_sourceRadius * 0.5f, 0.5f );
	float sum = 0.0f;
	float weights = 0.0f;
	for( int y = -g_sourceRadius; y <= g_sourceRadius; ++y )
	{
		for( int x = -g_sourceRadius; x <= g_sourceRadius; ++x )
		{
			const int2 cell = clamp( int2( id.xy ) + int2( x, y ), 0, (int)g_size - 1 );
			// Карта водосбора может быть другого размера, чем сетка
			const uint2 texel = min( uint2( ( cell + 0.5f ) * float2( flowWidth, flowHeight ) / g_size ), uint2( flowWidth, flowHeight ) - 1 );
			const float logFlow = log10( max( g_flowMap.Load( int3( texel, 0 ) ), 1.0f ) );
			const float weight = exp( -( x * x + y * y ) / ( 2.0f * sigma * sigma ) );
			sum += weight * smoothstep( g_logFlowStart, g_logFlowFull, logFlow );
			weights += weight;
		}
	}
	// Источники-помощники (родник, ледниковое озеро — WaterSources): гауссово пятно плотности расход / (2πσ²), м/с слоя,
	// обрезанное за 3σ — иначе хвост дал бы приток (и метку озера при загрузке) во всех низинах карты
	const float2 world = float2( ( id.x + 0.5f ) * g_cellSize, ( g_size - id.y - 0.5f ) * g_cellSize );
	float helpers = 0.0f;
	for( uint h = 0; h < g_helperCount; ++h )
	{
		const float4 helper = g_helpers[h];
		const float2 offset = world - helper.xy;
		const float distanceSq = dot( offset, offset ) / ( helper.w * helper.w );
		if( distanceSq < 9.0f )
			helpers += helper.z / ( 2.0f * 3.14159265f * helper.w * helper.w ) * exp( -0.5f * distanceSq );
	}
	g_sourcesOut[id.xy] = g_sourceRate * sum / weights + helpers;
}

[numthreads( 8, 8, 1 )]
void mainFlux( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	static const int2 offsets[4] = { int2( -1, 0 ), int2( 1, 0 ), int2( 0, -1 ), int2( 0, 1 ) };

	const float ground = terrain( cell );
	const float water = waterWithInflow( cell );
	const float level = ground + water;
	const float pipe = g_timeStep * g_gravity * g_cellSize;	// dt · g · A / l при сечении A = l²
	float4 flux = g_flux[cell];
	[unroll] for( int i = 0; i < 4; ++i )
	{
		const int2 neighbor = cell + offsets[i];
		const bool neighborInside = inside( neighbor );
		const float neighborWater = neighborInside ? waterWithInflow( neighbor ) : 0.0f;
		const float neighborLevel = neighborInside ? terrain( neighbor ) + neighborWater : ground;
		flux[i] = max( flux[i] + pipe * ( level - neighborLevel ), 0.0f );
		// Трение дна по Маннингу, неявно: без него вода на склоне только разгоняется и растекается тонкой быстрой
		// плёнкой. Установившаяся скорость — d^(2/3) · √уклон / n: мелкая вода тормозит сильнее и копится в русле
		const float faceDepth = max( 0.5f * ( water + neighborWater ), minFrictionDepth );
		const float speed = flux[i] / ( g_cellSize * faceDepth );
		flux[i] /= 1.0f + g_timeStep * g_gravity * g_manning * g_manning * speed / pow( faceDepth, 4.0f / 3.0f );
	}
	// Отток за шаг не больше воды в ячейке
	const float outflow = dot( flux, 1.0f ) * g_timeStep;
	const float volume = water * g_cellSize * g_cellSize;
	if( outflow > volume )
		flux *= volume / outflow;
	g_flux[cell] = flux;
}

// Озёра при загрузке: без них низина наполняется часами времени симуляции. Уровень заполненных низин — Planchon,
// Darboux 2001 («A fast, simple and versatile algorithm to fill the depressions of digital elevation models»): у края
// карты — рельеф, внутри сначала «бесконечность», затем шагами опускается до max(рельеф, наименьший уровень соседа);
// запись на месте — значения только убывают, поэтому гонки потоков лишь ускоряют сходимость
static const float lakeMinDepth = 0.05f;	// мельче — не озеро: ямки рельефа остаются сухими
static const float lakeLevelTolerance = 1e-3f;
static const int2 neighbors4[4] = { int2( -1, 0 ), int2( 1, 0 ), int2( 0, -1 ), int2( 0, 1 ) };

[numthreads( 8, 8, 1 )]
void mainFillInit( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const bool edge = any( id.xy == 0 ) || any( id.xy == g_size - 1 );
	g_fill[id.xy] = edge ? terrain( int2( id.xy ) ) : 1e9f;
}

[numthreads( 8, 8, 1 )]
void mainFill( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float ground = terrain( cell );
	const float level = g_fill[cell];
	if( level <= ground )
		return;
	float lowest = level;
	[unroll] for( int i = 0; i < 4; ++i )
	{
		const int2 neighbor = cell + neighbors4[i];
		if( inside( neighbor ) )
			lowest = min( lowest, g_fill[neighbor] );
	}
	g_fill[cell] = max( ground, lowest );
}

// Вода только в тех низинах, куда впадает источник (приток есть внутри озера: линия стока проходит через него). Режим
// simulated; в режиме static озёра считает конвейер рельефа (TerrainHydrology) — mainStatic
[numthreads( 8, 8, 1 )]
void mainLakeInit( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	g_lake[cell] = g_fill[cell] - terrain( cell ) > lakeMinDepth && g_sources[cell] > 0.0f ? 1.0f : 0.0f;
}

// Метка озера растекается по ячейкам низины с тем же уровнем
[numthreads( 8, 8, 1 )]
void mainLakeGrow( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	if( g_lake[cell] > 0.0f || g_fill[cell] - terrain( cell ) <= 0.0f )
		return;
	const float level = g_fill[cell];
	[unroll] for( int i = 0; i < 4; ++i )
	{
		const int2 neighbor = cell + neighbors4[i];
		if( inside( neighbor ) && g_lake[neighbor] > 0.0f && abs( g_fill[neighbor] - level ) < lakeLevelTolerance )
		{
			g_lake[cell] = 1.0f;
			return;
		}
	}
}

[numthreads( 8, 8, 1 )]
void mainLakeApply( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	g_water[cell] = g_lake[cell] > 0.0f ? g_fill[cell] - terrain( cell ) : 0.0f;
}

float4 neighborFlux( int2 cell )
{
	return inside( cell ) ? g_flux[cell] : 0.0f;
}

[numthreads( 8, 8, 1 )]
void mainWater( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float4 own = g_flux[cell];
	const float4 left = neighborFlux( cell + int2( -1, 0 ) );
	const float4 right = neighborFlux( cell + int2( 1, 0 ) );
	const float4 up = neighborFlux( cell + int2( 0, -1 ) );
	const float4 down = neighborFlux( cell + int2( 0, 1 ) );

	const float before = waterWithInflow( cell );
	const float inflow = left.y + right.x + up.w + down.z;
	const float outflow = dot( own, 1.0f );
	const float after = max( before + g_timeStep * ( inflow - outflow ) / ( g_cellSize * g_cellSize ), 0.0f );

	// Скорость — средний поток через ячейку по оси / (ширина · средняя глубина шага)
	const float depth = 0.5f * ( before + after );
	float2 velocity = 0.0f;
	if( depth > minVelocityDepth )
	{
		const float2 throughput = 0.5f * float2( left.y - own.x + own.y - right.x, up.w - own.z + own.w - down.z );
		velocity = throughput / ( g_cellSize * depth );
	}
	const float water = max( after - g_timeStep * g_evaporation, 0.0f );
	g_water[cell] = water;
	const float memory = max( water, g_memory[cell] * exp( -g_timeStep / wetMemoryTime ) );
	g_memory[cell] = memory;
	// Ось Y текселей идёт против Z мира (v = 1 − z / worldSize)
	g_output[cell] = float4( water, velocity.x, -velocity.y, memory );
}

// Статичная вода (режим static) — один раз при загрузке: озёра и ручьи из растра конвейера рельефа (TerrainHydrology:
// уровни воды абсолютные, глубина — по итоговому рельефу с правками) в текстуру для шейдеров. Там, где ручей глубже
// озера, — его течение
[numthreads( 8, 8, 1 )]
void mainStatic( uint3 id : SV_DispatchThreadID )
{
	if( any( id.xy >= g_size ) )
		return;
	const int2 cell = int2( id.xy );
	const float4 stream = g_staticWater.Load( int3( cell, 0 ) );
	// Озеро — уровень из конвейера рельефа (канал w; −1e9 — не озеро); глубина — до итогового рельефа, она же — поверхность
	// озёр (mainSurface читает g_water)
	const float lake = stream.w > -1e8f ? max( stream.w - terrain( cell ), 0.0f ) : 0.0f;
	g_water[cell] = lake;
	const float streamDepth = max( stream.x - terrain( cell ), 0.0f );
	const bool flowing = streamDepth > lake;
	const float depth = max( lake, streamDepth );
	g_output[cell] = float4( depth, flowing ? stream.yz : 0.0f, depth );
}
