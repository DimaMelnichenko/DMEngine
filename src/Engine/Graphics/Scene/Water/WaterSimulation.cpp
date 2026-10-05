#include "WaterSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <tuple>
#include "System.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"

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
constexpr float wetDepth = 0.01f;				// м: ячейка с водой в сводке (logWaterSummary)
constexpr float mmPerHour = 0.001f / 3600.0f;	// мм/ч → м/с

void addSlider( PropertyContainer& properties, const char* name, float value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
}

bool sameSources( const WaterSimulationSettings& a, const WaterSimulationSettings& b )
{
	return a.sourceRate == b.sourceRate && a.flowStart == b.flowStart && a.flowFull == b.flowFull && a.sourceRadius == b.sourceRadius;
}

}

WaterSimulation::WaterSimulation() : SceneObject( "Water simulation" )
{
	m_properties.setName( "Water simulation" );
}

bool WaterSimulation::initialize( const Settings& settings, const TerrainHeightSource& terrain )
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
	addSlider( m_properties, "Source rate (l/s)", settings.sourceRate, 0.0f, 2.0f );
	addSlider( m_properties, "Flow start (m2)", settings.flowStart, 100.0f, 100000.0f );
	addSlider( m_properties, "Flow full (m2)", settings.flowFull, 100.0f, 1000000.0f );
	addSlider( m_properties, "Source radius (m)", settings.sourceRadius, 0.0f, 16.0f );
	addSlider( m_properties, "Rain (mm/h)", settings.rain, 0.0f, 50.0f );
	addSlider( m_properties, "Evaporation (mm/h)", settings.evaporation, 0.0f, 50.0f );
	addSlider( m_properties, "Manning roughness", settings.manning, 0.0f, 0.2f );

	DMD3D& d3d = DMD3D::instance();
	if( !m_sourcesShader.Initialize( "Shaders\\water_simulation.cs", "mainSources" ) ||
		!m_fluxShader.Initialize( "Shaders\\water_simulation.cs", "mainFlux" ) ||
		!m_waterShader.Initialize( "Shaders\\water_simulation.cs", "mainWater" ) ||
		!m_fillInitShader.Initialize( "Shaders\\water_simulation.cs", "mainFillInit" ) ||
		!m_fillShader.Initialize( "Shaders\\water_simulation.cs", "mainFill" ) ||
		!m_lakeInitShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeInit" ) ||
		!m_lakeGrowShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeGrow" ) ||
		!m_lakeApplyShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeApply" ) ||
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
		!create( DXGI_FORMAT_R16G16B16A16_FLOAT, "Water state", m_output, m_outputUAV, &m_outputView ) )
		return false;

	// Источники, озёра до уровня перелива, затем до установившегося течения — порциями с ожиданием GPU
	buildSources( settings );
	if( !fillLakes() )
		return false;
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
	m_initialized = true;
	const auto end = std::chrono::high_resolution_clock::now();
	LOG( "Water simulation " + std::to_string( m_size ) + "x" + std::to_string( m_size ) + ", warm-up " +
		 std::to_string( warmupSteps ) + " steps for ms: " +
		 std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0 ) );
	logWaterSummary();
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
	settings.sourceRate = m_properties["Source rate (l/s)"].data<float>();
	settings.flowStart = m_properties["Flow start (m2)"].data<float>();
	settings.flowFull = m_properties["Flow full (m2)"].data<float>();
	settings.sourceRadius = m_properties["Source radius (m)"].data<float>();
	settings.rain = m_properties["Rain (mm/h)"].data<float>();
	settings.evaporation = m_properties["Evaporation (mm/h)"].data<float>();
	settings.manning = m_properties["Manning roughness"].data<float>();
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
	Device::updateResourceData( m_constantBuffer, params );
	DMD3D::instance().setConstantBuffer( 4, m_constantBuffer );
}

void WaterSimulation::buildSources( const Settings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& flowMap = System::textures().get( settings.flowMap )->srv();
	PassDesc pass;
	pass.name = "Water sources";
	pass.reads = { { &flowMap, "flow map" } };
	pass.writes = { { &m_sourcesUAV, "water sources" } };
	d3d.beginPass( pass );
	setParameters( settings );
	d3d.setSRV( 1, flowMap );
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
	pass.writes = { { &m_waterUAV, "water depth" }, { &m_fluxUAV, "water flux" }, { &m_outputUAV, "water state" } };
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
		m_waterShader.dispatchGroups( groups, groups, 1 );
	}
}

void WaterSimulation::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	const Settings current = settings();
	if( !sameSources( current, m_sourcesBuilt ) )
		buildSources( current );

	// Время симуляции — по времени кадра (с timestep — фиксированному), не больше maxStepsPerFrame шагов за кадр
	if( current.timeStep > 0.0f )
	{
		m_accumulated = std::min( m_accumulated + frame.elapsedTime * 0.001f * current.timeScale,
								  current.timeStep * maxStepsPerFrame );
		const uint32_t steps = static_cast<uint32_t>( m_accumulated / current.timeStep );
		m_accumulated -= steps * current.timeStep;
		if( steps > 0 )
		{
			setParameters( current );
			step( steps );
		}
	}
	// Вода — ресурс сцены до следующего кадра (террейн, позже — поверхность воды и мокрый берег)
	DMD3D::instance().setSRV( SLOT_WATER, m_outputView );
}

PropertyContainer* WaterSimulation::properties()
{
	return &m_properties;
}

}
