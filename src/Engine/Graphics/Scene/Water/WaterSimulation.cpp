#include "WaterSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <tuple>
#include "System.h"
#include "ConstantBuffers.h"
#include "Texture\DMTextureStorage.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "D3D\TextureImages.h"

namespace GS
{

namespace
{

constexpr uint32_t groupSize = 8;				// numthreads в water_simulation.cs
constexpr uint32_t maxStepsPerFrame = 16;		// больше за кадр не догоняем: симуляция отстаёт, но кадр не растёт
constexpr uint32_t warmupStepsPerSubmit = 500;
// Итераций заполнения низин и растекания метки озера: уровень и метка проходят за итерацию хотя бы ячейку, обычно —
// много (запись на месте); с запасом на путь от края карты до середины и через озеро
constexpr uint32_t fillIterations = 2048;
constexpr uint32_t lakeGrowIterations = 512;
constexpr uint32_t iterationsPerSubmit = 512;
constexpr uint32_t validationWarmupSteps = 600;	// при загрузке — порциями, чтобы одна отправка GPU не шла секундами
constexpr int32_t maxSourceRadius = 8;			// ячеек: размытие источников — цикл (2r + 1)² в шейдере
constexpr float gravity = 9.81f;
constexpr uint32_t tileCells = 32;				// WATER_TILE в water_surface.cs / water_surface.sh
constexpr float visibleDepth = 0.01f;			// м: мельче воды не видно — только мокрая земля
constexpr float wetDepth = 0.01f;				// м: ячейка с водой в сводке (logWaterSummary)
constexpr float mmPerHour = 0.001f / 3600.0f;	// мм/ч → м/с

Property* addSlider( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit = "" )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( std::max( high, value ) );
	property->setControlType( GUIControlType::SLIDER );
	property->setUnit( unit );
	return property;
}

// Русла конвейера (WaterChannelsSettings) в подокне «Channels»: имя свойства, поле, границы, единицы, подсказка
struct ChannelParameter
{
	const char* name;
	float WaterChannelsSettings::* field;
	float low;
	float high;
	const char* unit;
	const char* tooltip;
};

const ChannelParameter channelParameters[] = {
	{ "Min discharge", &WaterChannelsSettings::minDischarge, 0.0005f, 0.05f, "m3/s", "A channel starts where the discharge is larger" },
	{ "Width coef", &WaterChannelsSettings::widthCoef, 0.5f, 10.0f, "", "Bed width w = a * Q^0.5, m" },
	{ "Min width", &WaterChannelsSettings::minWidth, 0.5f, 10.0f, "m", "Narrowest bed" },
	{ "Depth coef", &WaterChannelsSettings::depthCoef, 0.1f, 5.0f, "", "Incision d = c * Q^0.4, m" },
	{ "Min incision", &WaterChannelsSettings::minIncision, 0.0f, 3.0f, "m", "Shallowest incision" },
	{ "Bank slope", &WaterChannelsSettings::bankSlope, 0.2f, 5.0f, "", "Bank: horizontal metres per metre of incision" },
	{ "Meander length", &WaterChannelsSettings::meanderLength, 5.0f, 200.0f, "m", "Meander wavelength along the stream" },
	{ "Meander amplitude", &WaterChannelsSettings::meanderAmplitude, 0.0f, 10.0f, "m", "Lateral shift at Q >= 0.1 m3/s" },
	{ "Meander max slope", &WaterChannelsSettings::meanderMaxSlope, 0.005f, 0.3f, "", "Steeper terrain has no meanders, m per m" },
	{ "Min slope", &WaterChannelsSettings::minSlope, 0.0f, 0.05f, "", "Bed drop downstream at least, m per m" },
	{ "Lake depth", &WaterChannelsSettings::lakeDepth, 0.01f, 1.0f, "m", "Deeper filled depressions are lakes: not carved, streams end there" },
	{ "Min lake area", &WaterChannelsSettings::minLakeArea, 0.0f, 1000.0f, "m2", "Smaller depressions are a part of the stream bed, not lakes" },
	{ "Thalweg", &WaterChannelsSettings::thalweg, 0.0f, 1.0f, "", "Extra incision in the bed middle, share of the incision" },
	{ "Manning", &WaterChannelsSettings::manning, 0.02f, 0.15f, "s/m^(1/3)", "Stream bed roughness: water depth of the ribbons" },
	{ "Min water slope", &WaterChannelsSettings::minWaterSlope, 0.001f, 0.05f, "", "Slope in the water depth at least, m per m" },
	{ "Min speed", &WaterChannelsSettings::minSpeed, 0.0f, 2.0f, "m/s", "Ribbon flow at least" },
	{ "Min water depth", &WaterChannelsSettings::minWaterDepth, 0.05f, 1.0f, "m", "Water over the bed at least" },
	{ "Foam slope", &WaterChannelsSettings::foamSlope, 0.02f, 0.5f, "", "Steeper streams foam (fully at twice), m per m" },
	{ "Ribbon overlap", &WaterChannelsSettings::ribbonOverlap, 0.0f, 2.0f, "m", "Ribbon beyond the trough edge, under the bank" },
	{ "Freeboard", &WaterChannelsSettings::freeboard, 0.0f, 2.0f, "m", "Water stands this much below the bank top: the bed is cut for floods" },
	{ "Lake depth ratio", &WaterChannelsSettings::lakeDepthRatio, 0.0f, 0.3f, "", "Deepest lake point per square root of the lake area" },
	{ "Lake min depth", &WaterChannelsSettings::lakeMinDepth, 0.0f, 10.0f, "m", "Deepest point of a small lake at least" },
	{ "Lake max depth", &WaterChannelsSettings::lakeMaxDepth, 0.0f, 50.0f, "m", "Deepest point of a large lake at most" },
	{ "Lake shelf width", &WaterChannelsSettings::lakeShelfWidth, 0.0f, 20.0f, "m", "Shallow shelf along the shore" },
	{ "Lake shelf depth", &WaterChannelsSettings::lakeShelfDepth, 0.0f, 3.0f, "m", "Water depth at the shelf edge" },
	{ "Lake drop slope", &WaterChannelsSettings::lakeDropSlope, 0.05f, 2.0f, "", "Drop-off beyond the shelf, m per m" },
};

// Множители кривой русла в подокне «Streams»: имя свойства, поле, подсказка
struct StreamScale
{
	const char* name;
	float StreamCurve::* field;
	const char* tooltip;
};

const StreamScale streamScales[] = {
	{ "Width scale", &StreamCurve::widthScale, "Bed width of this stream, times the common one" },
	{ "Depth scale", &StreamCurve::depthScale, "Incision of this stream, times the common one" },
	{ "Freeboard scale", &StreamCurve::freeboardScale, "Bank above the water, times the common one" },
	{ "Bank slope scale", &StreamCurve::bankSlopeScale, "Gentler (> 1) or steeper (< 1) banks" },
	{ "Thalweg scale", &StreamCurve::thalwegScale, "Deeper middle of the bed, times the common one" },
	{ "Roughness scale", &StreamCurve::roughnessScale, "Bed roughness (Manning): deeper, slower water at > 1" },
	{ "Discharge scale", &StreamCurve::dischargeScale, "Water of this stream: wider, deeper bed at > 1" },
};

bool sameSources( const WaterSimulationSettings& a, const WaterSimulationSettings& b )
{
	return a.sourceRate == b.sourceRate && a.flowStart == b.flowStart && a.flowFull == b.flowFull && a.sourceRadius == b.sourceRadius;
}

}

WaterSimulation::WaterSimulation() : SceneObject( "Water simulation" )
{
	m_properties.setName( "Water simulation" );
	m_surfaceProperties.setName( "Surface" );
}

bool WaterSimulation::initialize( const Settings& settings, const TerrainHeightSource& terrain, const TerrainHydrology::Result& hydrology )
{
	const auto start = std::chrono::high_resolution_clock::now();
	m_initial = settings;
	m_terrain = &terrain;
	const TerrainHeight height = terrain.terrainHeight();
	m_size = height.mapSize;
	m_cellSize = height.worldSize / std::max( m_size, 1u );
	if( m_size == 0 || !height.heightMap )
	{
		LOG( "Water simulation needs a terrain height map" );
		return false;
	}

	addSlider( m_properties, "Time scale", settings.timeScale, 0.0f, 100.0f );
	addSlider( m_properties, "Source rate", settings.sourceRate, 0.0f, 2.0f, "l/s" );
	addSlider( m_properties, "Flow start", settings.flowStart, 100.0f, 100000.0f, "m2" );
	addSlider( m_properties, "Flow full", settings.flowFull, 100.0f, 1000000.0f, "m2" );
	addSlider( m_properties, "Source radius", settings.sourceRadius, 0.0f, 16.0f, "m" );
	addSlider( m_properties, "Rain", settings.rain, 0.0f, 50.0f, "mm/h" );
	addSlider( m_properties, "Evaporation", settings.evaporation, 0.0f, 50.0f, "mm/h" );
	addSlider( m_properties, "Manning roughness", settings.manning, 0.0f, 0.2f );

	Property* property = m_surfaceProperties.insert( "Absorption", settings.absorption );
	property->setLow( 0.0f );
	property->setHigh( 5.0f );
	property->setControlType( GUIControlType::DRAG );
	property->setUnit( "1/m" )->setTooltip( "Absorption per channel R, G, B" );
	m_surfaceProperties.insert( "Scatter color", settings.scatterColor )->setControlType( GUIControlType::COLOR );
	addSlider( m_surfaceProperties, "Scatter strength", settings.scatterStrength, 0.0f, 0.1f );
	addSlider( m_surfaceProperties, "Roughness", settings.roughness, 0.0f, 0.5f );
	addSlider( m_surfaceProperties, "Ripple scale", settings.rippleScale, 0.5f, 20.0f, "m" );
	addSlider( m_surfaceProperties, "Ripple strength", settings.rippleStrength, 0.0f, 4.0f );
	addSlider( m_surfaceProperties, "Calm ripple", settings.calmRipple, 0.0f, 2.0f );
	addSlider( m_surfaceProperties, "Refraction", settings.refraction, 0.0f, 0.1f );
	addSlider( m_surfaceProperties, "Flow period", settings.flowPeriod, 0.2f, 5.0f, "s" );
	addSlider( m_surfaceProperties, "Foam speed", settings.foamSpeed, 0.1f, 5.0f, "m/s" );
	addSlider( m_surfaceProperties, "Foam shear", settings.foamShear, 0.1f, 10.0f, "1/s" );
	m_properties.addSubContainer( &m_surfaceProperties );

	// Русла и ручьи строит конвейер рельефа при загрузке — правки применяются при следующей загрузке уровня
	m_channelsProperties.setName( "Channels" );
	for( const ChannelParameter& parameter : channelParameters )
		addSlider( m_channelsProperties, parameter.name, settings.channels.*parameter.field, parameter.low, parameter.high, parameter.unit )
			->setTooltip( std::string( parameter.tooltip ) + "; applied at the next level load" );
	Property* smooth = m_channelsProperties.insert( "Smooth passes", settings.channels.smooth );
	smooth->setLow( 0.0f );
	smooth->setHigh( 20.0f );
	smooth->setTooltip( "Smoothing passes of the channel axis along the flow; applied at the next level load" );
	Property* paint = m_channelsProperties.insert( "Paint layer", settings.channels.paintLayer );
	paint->setLow( 0.0f );
	paint->setHigh( 7.0f );
	paint->setTooltip( "Terrain material layer of the bed and banks (TerrainLayers.layer); applied at the next level load" );
	m_properties.addSubContainer( &m_channelsProperties );

	// Кривые русел: подокно на кривую — включение и множители; правка помечает кривую правленной («Save level»):
	// новая генерация её не заменит
	m_streamsProperties.setName( "Streams" );
	m_streamProperties.clear();
	for( const StreamCurve& curve : settings.streams )
	{
		auto container = std::make_unique<PropertyContainer>();
		container->setName( curve.name + ( curve.generated ? "" : " (manual)" ) + ( curve.edited ? " (edited)" : "" ) + " #" +
							std::to_string( curve.id ) );
		container->insert( "Enabled", curve.enabled )->setTooltip( "Off - no channel and no water; applied at the next level load" );
		for( const StreamScale& scale : streamScales )
			addSlider( *container, scale.name, curve.*scale.field, 0.1f, 4.0f )
				->setTooltip( std::string( scale.tooltip ) + "; applied at the next level load" );
		m_streamsProperties.addSubContainer( container.get() );
		m_streamProperties.push_back( std::move( container ) );
	}
	m_properties.addSubContainer( &m_streamsProperties );

	DMD3D& d3d = DMD3D::instance();
	if( !m_sourcesShader.Initialize( "Shaders\\water_simulation.cs", "mainSources" ) ||
		!m_fluxShader.Initialize( "Shaders\\water_simulation.cs", "mainFlux" ) ||
		!m_waterShader.Initialize( "Shaders\\water_simulation.cs", "mainWater" ) ||
		!m_fillInitShader.Initialize( "Shaders\\water_simulation.cs", "mainFillInit" ) ||
		!m_fillShader.Initialize( "Shaders\\water_simulation.cs", "mainFill" ) ||
		!m_lakeInitShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeInit" ) ||
		!m_lakeGrowShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeGrow" ) ||
		!m_lakeApplyShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeApply" ) ||
		!m_staticShader.Initialize( "Shaders\\water_simulation.cs", "mainStatic" ) ||
		!d3d.createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) )
		return false;

	auto create = [&]( DXGI_FORMAT format, const char* name, Texture& texture, StorageView& uav, ShaderView* srv )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createStorageView( texture, {}, uav ) ||
			( srv && !d3d.createShaderView( texture, {}, *srv ) ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		// Без начальных данных память не обнулена: первая операция — очистка
		d3d.clearStorageView( uav );
		return true;
	};
	if( !create( DXGI_FORMAT_R32_FLOAT, "Water depth", m_water, m_waterUAV, nullptr ) ||
		!create( DXGI_FORMAT_R32G32B32A32_FLOAT, "Water flux", m_flux, m_fluxUAV, nullptr ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water sources", m_sources, m_sourcesUAV, &m_sourcesView ) ||
		!create( DXGI_FORMAT_R16G16B16A16_FLOAT, "Water state", m_output, m_outputUAV, &m_outputView ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water memory", m_memory, m_memoryUAV, nullptr ) ||
		!d3d.createShaderView( m_water, {}, m_waterView ) || !createSurface() )
		return false;

	// Источники-помощники — гауссовы пятна притока в mainSources: x, z, расход м³/с, σ (не меньше ячейки)
	if( !settings.sources.empty() )
	{
		std::vector<DirectX::XMFLOAT4> helpers;
		for( const WaterSource& source : settings.sources )
			helpers.emplace_back( source.position.x, source.position.y, source.rate * 0.001f, std::max( source.radius * 0.5f, m_cellSize ) );
		BufferDesc desc;
		desc.size = static_cast<uint32_t>( helpers.size() * sizeof( DirectX::XMFLOAT4 ) );
		desc.stride = sizeof( DirectX::XMFLOAT4 );
		desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, helpers.data(), m_helpers ) || !d3d.createShaderView( m_helpers, {}, m_helpersView ) )
		{
			LOG( "Failed to create water source helpers" );
			return false;
		}
		d3d.setName( m_helpers, "Water source helpers" );
		m_helperCount = static_cast<uint32_t>( helpers.size() );
		for( const WaterSource& source : settings.sources )
			LOG( "Water source " + source.name + ": " + std::to_string( source.rate ) + " l/s at " +
				 std::to_string( source.position.x ) + ", " + std::to_string( source.position.y ) );
	}

	// Режим static: озёра, ручьи и их вода — из конвейера рельефа (TerrainHydrology), без шагов и налива на GPU
	m_static = settings.staticWater;
	if( !createHydrologyTextures( hydrology ) )
		return false;
	if( m_static )
		m_staticWaterMap = &m_staticWaterView;
	buildSources( settings );
	// Режим simulated: озёра до уровня перелива, затем до установившегося течения — порциями с ожиданием GPU
	if( !m_static && !fillLakes() )
		return false;
	if( m_static )
	{
		// Статичная вода: озёра налиты, ручьи — лентами и растром; шагов нет
		if( !buildStaticWater( settings ) || !m_streams.initialize( hydrology.streams ) )
			return false;
		buildSurface();
		m_surfaceDirty = false;
		m_initialized = true;
		logWaterSummary();
		return true;
	}
	uint32_t warmupSteps = settings.timeStep > 0.0f ? static_cast<uint32_t>( settings.warmupTime / settings.timeStep ) : 0;
	if( d3d.gpuValidation() && warmupSteps > validationWarmupSteps )
	{
		// Под GPU-based validation шаг в десятки раз дольше: озёра уже налиты, ручьи досчитаются в игре
		LOG( "Water simulation: warm-up is cut to " + std::to_string( validationWarmupSteps ) + " steps under GPU-based validation" );
		warmupSteps = validationWarmupSteps;
	}
	for( uint32_t done = 0; done < warmupSteps; done += warmupStepsPerSubmit )
	{
		setParameters( settings );
		step( std::min( warmupStepsPerSubmit, warmupSteps - done ) );
		d3d.waitForGpu();
	}
	buildSurface();
	m_initialized = true;
	const auto end = std::chrono::high_resolution_clock::now();
	LOG( "Water simulation " + std::to_string( m_size ) + "x" + std::to_string( m_size ) + ", warm-up " +
		 std::to_string( warmupSteps ) + " steps for ms: " +
		 std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0 ) );
	logWaterSummary();
	return true;
}

bool WaterSimulation::createSurface()
{
	DMD3D& d3d = DMD3D::instance();
	m_tilesPerSide = ( m_size + tileCells - 1 ) / tileCells;
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	if( !m_surfaceShader.Initialize( "Shaders\\water_surface.cs", "mainSurface" ) ||
		!m_tilesResetShader.Initialize( "Shaders\\water_surface.cs", "mainTilesReset" ) ||
		!m_tilesShader.Initialize( "Shaders\\water_surface.cs", "mainTiles" ) ||
		!d3d.createShaderConstantBuffer( sizeof( TilesParameters ), m_tilesBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( SurfaceParameters ), m_surfaceBuffer ) )
		return false;

	TextureDesc levelDesc;
	levelDesc.width = m_size;
	levelDesc.height = m_size;
	levelDesc.format = DXGI_FORMAT_R32_FLOAT;
	levelDesc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	BufferDesc boundsDesc;
	boundsDesc.size = tileCount * sizeof( DirectX::XMFLOAT2 );
	boundsDesc.stride = sizeof( DirectX::XMFLOAT2 );
	boundsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc listDesc;
	listDesc.size = tileCount * sizeof( uint32_t );
	listDesc.stride = sizeof( uint32_t );
	listDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc argsDesc;
	argsDesc.size = 32;
	argsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !d3d.createTexture( levelDesc, nullptr, m_level ) || !d3d.createStorageView( m_level, {}, m_levelUAV ) ||
		!d3d.createShaderView( m_level, {}, m_levelView ) ||
		!d3d.createBuffer( boundsDesc, nullptr, m_tileBounds ) || !d3d.createStorageView( m_tileBounds, {}, m_tileBoundsUAV ) ||
		!d3d.createShaderView( m_tileBounds, {}, m_tileBoundsView ) ||
		!d3d.createBuffer( listDesc, nullptr, m_tileList ) || !d3d.createStorageView( m_tileList, {}, m_tileListUAV ) ||
		!d3d.createShaderView( m_tileList, {}, m_tileListView ) ||
		!d3d.createBuffer( argsDesc, nullptr, m_drawArgs ) || !d3d.createStorageView( m_drawArgs, rawView, m_drawArgsUAV ) )
	{
		LOG( "Failed to create water surface resources" );
		return false;
	}
	d3d.setName( m_level, "Water surface level" );
	d3d.setName( m_tileBounds, "Water tile bounds" );
	d3d.setName( m_tileList, "Water visible tiles" );
	d3d.setName( m_drawArgs, "Water indirect command" );

	m_tileMesh.initialize( tileCells + 1, tileCells + 1 );
	m_surfaceProgram.setLayoutDesc( { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } } );
	if( !m_surfaceProgram.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\water.vs" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\water.ps" ) )
	{
		LOG( "Water surface: shader compilation failed" );
		return false;
	}
	m_surfacePhase = m_surfaceProgram.createPhase( 0, 0 );
	return m_surfacePhase >= 0;
}

void WaterSimulation::buildSurface()
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water surface";
	pass.reads = { { &heightMap, "height map" }, { &m_waterView, "water depth" } };
	pass.writes = { { &m_levelUAV, "water level" }, { &m_tileBoundsUAV, "water tile bounds" } };
	d3d.beginPass( pass );
	setParameters( settings() );
	TilesParameters tiles = {};
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, m_waterView );
	d3d.setUAV( 0, m_levelUAV );
	d3d.setUAV( 1, m_tileBoundsUAV );
	m_surfaceShader.dispatchGroups( m_tilesPerSide, m_tilesPerSide, 1 );
	m_surfaceDirty = false;
}

void WaterSimulation::cullTiles( const RenderView& view )
{
	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water tiles";
	pass.reads = { { &m_tileBoundsView, "water tile bounds" } };
	pass.writes = { { &m_drawArgsUAV, "water indirect command" }, { &m_tileListUAV, "water visible tiles" } };
	d3d.beginPass( pass );
	TilesParameters tiles = {};
	for( int i = 0; i < 6; ++i )
		DirectX::XMStoreFloat4( &tiles.planes[i], view.frustum.planes()[i] );
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	setParameters( settings() );
	d3d.setUAV( 2, m_drawArgsUAV );
	m_tilesResetShader.dispatchGroups( 1, 1, 1 );
	d3d.setSRV( 4, m_tileBoundsView );
	d3d.setUAV( 2, m_drawArgsUAV );
	d3d.setUAV( 3, m_tileListUAV );
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	m_tilesShader.dispatchGroups( ( tileCount + 63 ) / 64, 1, 1 );
}

void WaterSimulation::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Только главный вид: тени вода не отбрасывает. Полупрозрачные — от дальних к ближним, вода — первой
	if( m_initialized && view.index == 0 )
		collector.addCustom( passBit( MeshPass::transparent ), view.farPlane, true );
}

void WaterSimulation::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	m_surfaceProgram.warmPipelines( { { RasterState::solid, DepthState::readOnly, BlendState::alpha },
									  { RasterState::wireframe, DepthState::readOnly, BlendState::alpha } },
									states.scene, { m_surfacePhase } );
	m_streams.warmPipelines( states );
}

void WaterSimulation::renderCustom( const RenderContext& context )
{
	if( context.pass != MeshPass::transparent )
		return;
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_terrain->terrainHeight();
	SurfaceParameters params = {};
	params.size = m_size;
	params.cellSize = m_cellSize;
	params.worldSize = height.worldSize;
	params.tilesPerSide = m_tilesPerSide;
	const Settings current = settings();
	params.absorption = current.absorption;
	params.flowPeriod = std::max( current.flowPeriod, 0.01f );
	params.scatterColor = DirectX::XMFLOAT3( current.scatterColor.x * current.scatterStrength, current.scatterColor.y * current.scatterStrength,
											 current.scatterColor.z * current.scatterStrength );
	params.rippleScale = std::max( current.rippleScale, 0.01f );
	params.rippleStrength = current.rippleStrength;
	params.calmRipple = current.calmRipple;
	params.refraction = current.refraction;
	params.roughness = current.roughness;
	params.foamSpeed = std::max( current.foamSpeed, 0.01f );
	params.foamShear = std::max( current.foamShear, 0.01f );
	Device::updateResourceData( m_surfaceBuffer, params );

	ScopedRenderState state( context.frameRaster );
	d3d.setVertexBuffer( m_tileMesh.vertexBuffer(), sizeof( DirectX::XMFLOAT3 ) );
	d3d.setIndexBuffer( m_tileMesh.indexBuffer(), DXGI_FORMAT_R32_UINT );
	m_surfaceProgram.setPass( m_surfacePhase );
	d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_surfaceBuffer );
	d3d.setSRV( 0, m_levelView );
	d3d.setSRV( 1, System::textures().get( DMTextureStorage::noiseId )->srv() );
	d3d.setSRV( SLOT_INSTANCE_DATA, m_tileListView );
	d3d.drawIndexedInstancedIndirectCount( m_drawArgs, 0, 1, m_drawArgs, 24 );
	// Ручьи — с теми же константами материала и шумом
	m_streams.render( context );
}

bool WaterSimulation::createHydrologyTextures( const TerrainHydrology::Result& hydrology )
{
	if( hydrology.size != m_size || hydrology.channelFlow.size() != static_cast<size_t>( m_size ) * m_size )
	{
		LOG( "Water simulation: water channels differ from the height map in size" );
		return false;
	}
	DMD3D& d3d = DMD3D::instance();
	auto create = [&]( DXGI_FORMAT format, const void* data, uint32_t texelBytes, const char* name, Texture& texture, ShaderView& view )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = format;
		TextureData initial;
		initial.data = data;
		initial.rowPitch = m_size * texelBytes;
		initial.slicePitch = initial.rowPitch * m_size;
		if( !d3d.createTexture( desc, &initial, texture ) || !d3d.createShaderView( texture, {}, view ) )
		{
			LOG( std::string( "Failed to create water texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		return true;
	};
	return create( DXGI_FORMAT_R32_FLOAT, hydrology.channelFlow.data(), sizeof( float ), "Water channel flow", m_flowTexture, m_flowView ) &&
		   create( DXGI_FORMAT_R32G32B32A32_FLOAT, hydrology.staticWater.data(), sizeof( DirectX::XMFLOAT4 ), "Water static streams",
				   m_staticWaterTexture, m_staticWaterView );
}

bool WaterSimulation::buildStaticWater( const Settings& settings )
{
	const ShaderView& staticWater = m_staticWaterView;
	DMD3D& d3d = DMD3D::instance();
	setParameters( settings );
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water static";
	pass.reads = { { &heightMap, "height map" }, { &staticWater, "static water" } };
	pass.writes = { { &m_waterUAV, "water depth" }, { &m_outputUAV, "water state" } };
	d3d.beginPass( pass );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, staticWater );
	d3d.setUAV( 0, m_waterUAV );
	d3d.setUAV( 2, m_outputUAV );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	m_staticShader.dispatchGroups( groups, groups, 1 );
	return true;
}

bool WaterSimulation::fillLakes()
{
	DMD3D& d3d = DMD3D::instance();
	Texture fill, lake;
	StorageView fillUAV, lakeUAV;
	for( auto [texture, uav, name] : { std::tuple{ &fill, &fillUAV, "Water lake level" }, std::tuple{ &lake, &lakeUAV, "Water lake mask" } } )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = DXGI_FORMAT_R32_FLOAT;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
	}

	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	auto run = [&]( DMComputeShader& shader, uint32_t iterations )
	{
		for( uint32_t i = 0; i < iterations; ++i )
		{
			if( i % iterationsPerSubmit == 0 )
			{
				if( i > 0 )
					d3d.waitForGpu();
				PassDesc pass;
				pass.name = "Water lakes";
				pass.reads = { { &heightMap, "height map" }, { &m_sourcesView, "water sources" } };
				pass.writes = { { &fillUAV, "lake level" }, { &lakeUAV, "lake mask" }, { &m_waterUAV, "water depth" } };
				d3d.beginPass( pass );
				setParameters( m_initial );
				d3d.setSRV( 0, heightMap );
				d3d.setSRV( 2, m_sourcesView );
			}
			d3d.setUAV( 0, m_waterUAV );
			d3d.setUAV( 4, fillUAV );
			d3d.setUAV( 5, lakeUAV );
			shader.dispatchGroups( groups, groups, 1 );
		}
	};
	run( m_fillInitShader, 1 );
	run( m_fillShader, fillIterations );
	run( m_lakeInitShader, 1 );
	run( m_lakeGrowShader, lakeGrowIterations );
	run( m_lakeApplyShader, 1 );
	d3d.waitForGpu();
	return true;
}

void WaterSimulation::logWaterSummary()
{
	// Копия глубины на CPU: установилось ли течение к концу прогрева — сравнить с прогревом подольше
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_water, copies, bytes ) || copies.empty() )
		return;
	double volume = 0.0;
	float maxDepth = 0.0f;
	uint32_t wet = 0;
	for( uint32_t y = 0; y < copies[0].rows; ++y )
	{
		const float* row = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + y * copies[0].rowPitch );
		for( uint32_t x = 0; x < m_size; ++x )
		{
			volume += row[x];
			maxDepth = std::max( maxDepth, row[x] );
			wet += row[x] > wetDepth ? 1 : 0;
		}
	}
	volume *= m_cellSize * m_cellSize;
	char text[160];
	snprintf( text, sizeof( text ), "Water: volume %.0f m3, max depth %.2f m, cells deeper than 1 cm %u", volume, maxDepth, wet );
	LOG( text );
}

WaterSimulation::Settings WaterSimulation::settings() const
{
	Settings settings = m_initial;
	settings.timeScale = m_properties["Time scale"].data<float>();
	settings.sourceRate = m_properties["Source rate"].data<float>();
	settings.flowStart = m_properties["Flow start"].data<float>();
	settings.flowFull = m_properties["Flow full"].data<float>();
	settings.sourceRadius = m_properties["Source radius"].data<float>();
	settings.rain = m_properties["Rain"].data<float>();
	settings.evaporation = m_properties["Evaporation"].data<float>();
	settings.manning = m_properties["Manning roughness"].data<float>();
	settings.absorption = m_surfaceProperties["Absorption"].data<DirectX::XMFLOAT3>();
	settings.scatterColor = m_surfaceProperties["Scatter color"].data<DirectX::XMFLOAT3>();
	settings.scatterStrength = m_surfaceProperties["Scatter strength"].data<float>();
	settings.roughness = m_surfaceProperties["Roughness"].data<float>();
	settings.rippleScale = m_surfaceProperties["Ripple scale"].data<float>();
	settings.rippleStrength = m_surfaceProperties["Ripple strength"].data<float>();
	settings.calmRipple = m_surfaceProperties["Calm ripple"].data<float>();
	settings.refraction = m_surfaceProperties["Refraction"].data<float>();
	settings.flowPeriod = m_surfaceProperties["Flow period"].data<float>();
	settings.foamSpeed = m_surfaceProperties["Foam speed"].data<float>();
	settings.foamShear = m_surfaceProperties["Foam shear"].data<float>();
	for( const ChannelParameter& parameter : channelParameters )
		settings.channels.*parameter.field = m_channelsProperties[parameter.name].data<float>();
	settings.channels.smooth = m_channelsProperties["Smooth passes"].data<int32_t>();
	settings.channels.paintLayer = m_channelsProperties["Paint layer"].data<int32_t>();
	for( size_t i = 0; i < settings.streams.size() && i < m_streamProperties.size(); ++i )
	{
		StreamCurve& curve = settings.streams[i];
		const StreamCurve& initial = m_initial.streams[i];
		const PropertyContainer& properties = *m_streamProperties[i];
		curve.enabled = properties["Enabled"].data<bool>();
		for( const StreamScale& scale : streamScales )
			curve.*scale.field = properties[scale.name].data<float>();
		// Правка в окне — кривая правленная: новая генерация её не заменит
		bool changed = curve.enabled != initial.enabled;
		for( const StreamScale& scale : streamScales )
			changed = changed || curve.*scale.field != initial.*scale.field;
		curve.edited = initial.edited || changed;
	}
	return settings;
}

void WaterSimulation::setParameters( const Settings& settings )
{
	const TerrainHeight height = m_terrain->terrainHeight();
	Parameters params = {};
	params.size = m_size;
	params.cellSize = m_cellSize;
	params.timeStep = settings.timeStep;
	params.gravity = gravity;
	params.rain = settings.rain * mmPerHour;
	params.evaporation = settings.evaporation * mmPerHour;
	// л/с на ячейку → м/с слоя воды на её площади
	params.sourceRate = settings.sourceRate * 0.001f / ( m_cellSize * m_cellSize );
	params.heightMultiplier = height.heightMultiplier;
	params.heightOffset = height.heightOffset;
	params.logFlowStart = std::log10( std::max( settings.flowStart, 1.0f ) );
	params.logFlowFull = std::max( std::log10( std::max( settings.flowFull, 1.0f ) ), params.logFlowStart + 0.01f );
	params.sourceRadius = std::clamp( static_cast<int32_t>( std::lround( settings.sourceRadius / m_cellSize ) ), 0, maxSourceRadius );
	params.manning = settings.manning;
	params.helperCount = m_helperCount;
	Device::updateResourceData( m_constantBuffer, params );
	DMD3D::instance().setConstantBuffer( 4, m_constantBuffer );
}

void WaterSimulation::buildSources( const Settings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& flowMap = m_flowView;
	PassDesc pass;
	pass.name = "Water sources";
	pass.reads = { { &flowMap, "flow map" } };
	if( m_helperCount > 0 )
		pass.reads.push_back( { &m_helpersView, "water source helpers" } );
	pass.writes = { { &m_sourcesUAV, "water sources" } };
	d3d.beginPass( pass );
	setParameters( settings );
	d3d.setSRV( 1, flowMap );
	if( m_helperCount > 0 )
		d3d.setSRV( 4, m_helpersView );
	d3d.setUAV( 3, m_sourcesUAV );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	m_sourcesShader.dispatchGroups( groups, groups, 1 );
	m_sourcesBuilt = settings;
}

void WaterSimulation::step( uint32_t steps )
{
	if( steps == 0 )
		return;
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water simulation";
	pass.reads = { { &heightMap, "height map" }, { &m_sourcesView, "water sources" } };
	pass.writes = { { &m_waterUAV, "water depth" }, { &m_fluxUAV, "water flux" }, { &m_outputUAV, "water state" },
					{ &m_memoryUAV, "water memory" } };
	d3d.beginPass( pass );
	// Константы привязаны setParameters до прохода; beginPass очищает только таблицу привязок
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 2, m_sourcesView );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	for( uint32_t i = 0; i < steps; ++i )
	{
		// Привязка UAV после dispatch ставит барьер UAV → UAV: следующая стадия видит записанное предыдущей
		d3d.setUAV( 0, m_waterUAV );
		d3d.setUAV( 1, m_fluxUAV );
		m_fluxShader.dispatchGroups( groups, groups, 1 );
		d3d.setUAV( 0, m_waterUAV );
		d3d.setUAV( 1, m_fluxUAV );
		d3d.setUAV( 2, m_outputUAV );
		d3d.setUAV( 6, m_memoryUAV );
		m_waterShader.dispatchGroups( groups, groups, 1 );
	}
}

void WaterSimulation::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	const Settings current = settings();
	if( !m_static && !sameSources( current, m_sourcesBuilt ) )
		buildSources( current );

	// Время симуляции — по времени кадра (с timestep — фиксированному), не больше maxStepsPerFrame шагов за кадр
	if( !m_static && current.timeStep > 0.0f )
	{
		m_accumulated = std::min( m_accumulated + frame.elapsedTime * 0.001f * current.timeScale,
								  current.timeStep * maxStepsPerFrame );
		const uint32_t steps = static_cast<uint32_t>( m_accumulated / current.timeStep );
		m_accumulated -= steps * current.timeStep;
		if( steps > 0 )
		{
			setParameters( current );
			step( steps );
			m_surfaceDirty = true;
		}
	}
	if( m_surfaceDirty )
		buildSurface();
	cullTiles( frame.view );
	// Вода — ресурс сцены до следующего кадра (террейн, поверхность воды, мокрый берег, расстановка, частицы); приток —
	// подсветке «Show water»
	DMD3D::instance().setSRV( SLOT_WATER, m_outputView );
	DMD3D::instance().setSRV( SLOT_WATER_SOURCES, m_sourcesView );
}

bool WaterSimulation::exportDischarge( const std::string& file, std::string& reason )
{
	if( !m_initialized )
	{
		reason = "the level has no water simulation";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_flux, copies, bytes ) || copies.empty() )
	{
		reason = "water flux readback failed";
		return false;
	}
	std::vector<float> discharge( static_cast<size_t>( m_size ) * m_size );
	double total = 0.0;
	float peak = 0.0f;
	for( uint32_t row = 0; row < m_size; ++row )
	{
		const float* flux = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + row * copies[0].rowPitch );
		for( uint32_t col = 0; col < m_size; ++col )
		{
			const float* out = flux + col * 4;
			const float q = out[0] + out[1] + out[2] + out[3];
			discharge[static_cast<size_t>( row ) * m_size + col] = q;
			peak = std::max( peak, q );
			total += q;
		}
	}
	if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), m_size, m_size, discharge.data() ) )
	{
		reason = "cannot write " + file;
		return false;
	}
	char text[160];
	snprintf( text, sizeof( text ), "Water discharge: %ux%u, peak %.3f m3/s -> ", m_size, m_size, peak );
	LOG( text + file );
	return true;
}

PropertyContainer* WaterSimulation::properties()
{
	return &m_properties;
}

}
