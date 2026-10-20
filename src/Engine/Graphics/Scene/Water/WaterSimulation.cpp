#include "WaterSimulation.h"
#include <chrono>
#include <cstdio>
#include <DirectXPackedVector.h>
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Properties\SettingsProperties.h"
#include "D3D\TextureImages.h"
#include "ConstantBuffers.h"

namespace GS
{

namespace
{

constexpr float wetDepth = 0.01f;				// м: ячейка с водой в сводке (logWaterSummary)

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
	if( !m_field.initialize( terrain ) )
		return false;

	// Приток и поверхность, течение, русла — по описаниям полей (waterSimulationSchema, waterFlowSchema, waterChannelsSchema)
	addSettingProperties( m_properties, waterSimulationSchema(), settings, "" );
	addSettingProperties( m_surfaceProperties, waterSimulationSchema(), settings, "surface" );
	m_properties.addSubContainer( &m_surfaceProperties );

	// Течение статичной воды: установившийся расчёт при загрузке, правка — расчёт заново
	m_flowProperties.setName( "Flow" );
	addSettingProperties( m_flowProperties, waterFlowSchema(), settings.flow );
	m_properties.addSubContainer( &m_flowProperties );
	// Подробное течение у камеры: каждый кадр, правка — сразу
	m_nearProperties.setName( "Near flow" );
	addSettingProperties( m_nearProperties, waterNearFlowSchema(), settings.nearFlow );
	m_properties.addSubContainer( &m_nearProperties );

	// Русла и ручьи строит конвейер рельефа: форма русел и кривые — подокна «Channels» и «Streams»
	m_streams.initialize( settings, m_properties );
	// Кривые живут в m_streams: settings() их не копирует (его зовут каждый кадр)
	m_initial.streams.clear();

	if( !m_pipes.initialize( m_field, settings ) || !m_staticWater.initialize( m_field, m_initial ) || !m_surface.initialize( m_field ) )
		return false;

	// Режим static: озёра, ручьи и их вода — из конвейера рельефа (TerrainHydrology), без шагов и налива на GPU
	m_staticMode = settings.staticWater;
	if( !m_staticWater.createTextures( hydrology, m_staticMode ) || !m_pipes.setChannelFlow( hydrology ) )
		return false;
	m_pipes.buildSources( settings );
	// Доли — по времени загрузки (Release, Test: растр русел и источники ~0,16 с — до этих шагов, течение ~0,5 с):
	// течение озёр (static) или налив озёр и прогрев (simulated) — почти всё
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.1f, "Water surface" );
	// Режим simulated: озёра до уровня перелива, затем до установившегося течения — порциями с ожиданием GPU
	if( !m_staticMode )
		task.enterProgressFrame( 0.3f, "Lakes" );
	if( !m_staticMode && !m_pipes.fillLakes( m_initial ) )
		return false;
	if( m_staticMode )
	{
		// Статичная вода: озёра и ручьи — одно поле из растра; шагов нет, течение озёр — установившийся расчёт
		if( !m_staticWater.build( settings ) || !m_staticWater.createFlowResources() || !m_foam.initialize( m_field ) )
			return false;
		// Без окна у камеры вода есть — течение у камеры — детальной сетки
		if( !m_near.initialize() )
			LOG( "Water near flow is not created" );
		task.enterProgressFrame( 0.9f, "Flow" );
		solveFlow( settings );
		m_initialized = true;
		logWaterSummary();
		return true;
	}
	task.enterProgressFrame( 0.6f, "Warm-up" );
	const uint32_t warmupSteps = m_pipes.warmUp( settings );
	buildSurface();
	m_initialized = true;
	const auto end = std::chrono::high_resolution_clock::now();
	LOG( "Water simulation " + std::to_string( m_field.size ) + "x" + std::to_string( m_field.size ) + ", warm-up " +
		 std::to_string( warmupSteps ) + " steps for ms: " +
		 std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0 ) );
	logWaterSummary();
	return true;
}

const WaterSimulation::Settings& WaterSimulation::currentSettings() const
{
	const uint64_t revision = m_properties.revision();
	if( revision != m_currentRevision )
	{
		m_current = settings();
		m_currentRevision = revision;
	}
	return m_current;
}

void WaterSimulation::buildSurface()
{
	m_surface.build( currentSettings() );
	m_staticWater.buildDetailSurface();
}

void WaterSimulation::solveFlow( const Settings& settings )
{
	m_staticWater.solve( settings.flow, [this] { buildSurface(); } );
	m_foam.build( m_staticWater, settings );
}

void WaterSimulation::frameConstants( const RenderView&, FrameParameters& constants )
{
	if( m_initialized && m_staticWater.detailTileCount() > 0 )
		constants.waterDetail = DirectX::XMFLOAT4( m_field.terrain->terrainHeight().worldSize, m_staticWater.detailTileSize(), 0.0f, 0.0f );
	if( m_initialized )
		m_near.frameConstants( constants );
}

void WaterSimulation::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Только главный вид: тени вода не отбрасывает. Полупрозрачные — от дальних к ближним, вода — первой; до них — поверхность
	// для экранных отражений
	if( m_initialized && view.kind == RenderView::Kind::main )
		collector.addCustom( passBit( MeshPass::reflectionPrepass ) | passBit( MeshPass::transparent ), view.farPlane, true );
}

void WaterSimulation::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	m_surface.warmPipelines( states );
}

void WaterSimulation::renderCustom( const RenderContext& context )
{
	if( context.pass == MeshPass::reflectionPrepass )
		m_surface.renderReflectionSurface( currentSettings(), m_staticWater, m_foam, m_near.windowCells() );
	else if( context.pass == MeshPass::transparent )
		m_surface.render( context, currentSettings(), m_staticWater, m_foam, m_near.windowCells() );
}

void WaterSimulation::applyFlowChanges()
{
	if( !m_initialized || !m_staticMode )
		return;
	// Подокно «Flow» не правили с прошлой сверки — сверять нечего
	const uint64_t revision = m_flowProperties.revision();
	if( revision == m_flowRevision )
		return;
	m_flowRevision = revision;
	const Settings current = settings();
	if( settingsEqual( waterFlowSchema(), current.flow, m_staticWater.flowBuilt() ) )
		return;
	// mainStatic заново: у озёр нули течения, ручьи — как в растре; затем течение озёр и ручьёв, поверхность (детальная
	// вода берёт течение сетки 1 м) и течение детальной сетки
	if( m_staticWater.build( current ) )
		solveFlow( current );
}

void WaterSimulation::applyFoamChanges()
{
	if( !m_initialized || !m_staticMode || !m_foam.ready() )
		return;
	// Подокно «Surface» не правили с прошлой сверки — сверять нечего; правили не пену (цвет, рябь) — поле то же
	const uint64_t revision = m_surfaceProperties.revision();
	if( revision == m_foamRevision )
		return;
	m_foamRevision = revision;
	const Settings current = settings();
	if( !m_foam.builtWith( current ) )
		m_foam.build( m_staticWater, current );
}

void WaterSimulation::logWaterSummary()
{
	// Копия глубины на CPU: установилось ли течение к концу прогрева — сравнить с прогревом подольше
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_field.depth, copies, bytes ) || copies.empty() )
		return;
	double volume = 0.0;
	float maxDepth = 0.0f;
	uint32_t wet = 0;
	for( uint32_t y = 0; y < copies[0].rows; ++y )
	{
		const float* row = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + y * copies[0].rowPitch );
		for( uint32_t x = 0; x < m_field.size; ++x )
		{
			volume += row[x];
			maxDepth = std::max( maxDepth, row[x] );
			wet += row[x] > wetDepth ? 1 : 0;
		}
	}
	volume *= m_field.cellSize * m_field.cellSize;
	char text[160];
	snprintf( text, sizeof( text ), "Water: volume %.0f m3, max depth %.2f m, cells deeper than 1 cm %u", volume, maxDepth, wet );
	LOG( text );
}

WaterSimulation::Settings WaterSimulation::settings() const
{
	Settings settings = m_initial;
	readSettingProperties( m_properties, waterSimulationSchema(), settings, "" );
	readSettingProperties( m_surfaceProperties, waterSimulationSchema(), settings, "surface" );
	readSettingProperties( m_flowProperties, waterFlowSchema(), settings.flow );
	readSettingProperties( m_nearProperties, waterNearFlowSchema(), settings.nearFlow );
	settings.channels = m_streams.channels();
	return settings;
}

bool WaterSimulation::rebuild( const TerrainHydrology::Result& hydrology )
{
	const auto start = std::chrono::high_resolution_clock::now();
	m_streams.markBuilt();
	const Settings current = settings();
	if( !m_staticWater.createTextures( hydrology, m_staticMode ) || !m_pipes.setChannelFlow( hydrology ) )
		return false;
	m_pipes.buildSources( current );
	if( m_staticMode )
	{
		if( !m_staticWater.build( current ) )
			return false;
		solveFlow( current );
	}
	else
		m_surface.setDirty();
	LOG( "Water rebuilt for the edited channels, ms: " +
		 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() ) );
	return true;
}

void WaterSimulation::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	const Settings& current = currentSettings();
	// Трубы: приток по настройкам и шаги по времени кадра — поверхность заново
	if( !m_staticMode && m_pipes.advance( frame, current ) )
		m_surface.setDirty();
	if( m_surface.dirty() )
		buildSurface();
	m_surface.cull( frame.view, current, m_staticWater );
	// Вода — ресурс сцены до следующего кадра (террейн, поверхность воды, мокрый берег, расстановка, частицы); приток —
	// подсветке «Show water»
	DMD3D::instance().setSRV( SLOT_WATER, m_field.stateView );
	DMD3D::instance().setSRV( SLOT_WATER_SOURCES, m_pipes.sourcesView() );
	// Уровень поверхности — глубине в точке (урез внутри клетки): мокрая земля, галька дна, трава в воде
	DMD3D::instance().setSRV( SLOT_WATER_LEVEL, m_surface.levelView() );
	// Детальная вода у русел — тем же потребителям (water.sh: по индексу плиток детальной земли)
	if( m_staticWater.detailTileCount() > 0 )
		DMD3D::instance().setSRV( SLOT_WATER_DETAIL, m_staticWater.detailWaterView() );
	// Подробное течение у камеры — по детальной воде, ресурс сцены SLOT_WATER_NEAR
	if( m_staticMode )
		m_near.compute( frame, current, m_field, m_staticWater, m_field.terrain->terrainHeight(), m_foam );
}

bool WaterSimulation::exportState( const std::string& prefix, std::string& reason )
{
	if( !m_initialized )
	{
		reason = "the level has no water simulation";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_field.state, copies, bytes ) || copies.empty() )
	{
		reason = "water state readback failed";
		return false;
	}
	const size_t cells = static_cast<size_t>( m_field.size ) * m_field.size;
	std::vector<float> channels[3] = { std::vector<float>( cells ), std::vector<float>( cells ), std::vector<float>( cells ) };
	for( uint32_t row = 0; row < m_field.size; ++row )
	{
		const auto* texel = reinterpret_cast<const DirectX::PackedVector::HALF*>( bytes.data() + copies[0].offset + row * copies[0].rowPitch );
		for( uint32_t col = 0; col < m_field.size; ++col )
			for( int c = 0; c < 3; ++c )
				channels[c][static_cast<size_t>( row ) * m_field.size + col] = DirectX::PackedVector::XMConvertHalfToFloat( texel[col * 4 + c] );
	}
	const char* names[3] = { "_depth.dds", "_vx.dds", "_vz.dds" };
	for( int c = 0; c < 3; ++c )
	{
		const std::string file = prefix + names[c];
		if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), m_field.size, m_field.size, channels[c].data() ) )
		{
			reason = "cannot write " + file;
			return false;
		}
	}
	LOG( "Water state: " + prefix + "_depth.dds, _vx.dds, _vz.dds" );

	if( !m_staticWater.exportDetail( prefix, reason ) || !m_foam.exportState( m_staticWater, prefix, reason ) )
		return false;
	// Окно у камеры — если есть (вне детальной воды его нет — не ошибка)
	std::string nearReason;
	if( m_near.active() && !m_near.exportState( prefix, nearReason ) )
	{
		reason = nearReason;
		return false;
	}
	return true;
}

bool WaterSimulation::exportDischarge( const std::string& file, std::string& reason )
{
	if( !m_initialized )
	{
		reason = "the level has no water simulation";
		return false;
	}
	return m_pipes.exportDischarge( file, reason );
}

PropertyContainer* WaterSimulation::properties()
{
	return &m_properties;
}

}
