#include "ParticleSystem.h"
#include <algorithm>
#include <cmath>
#include "System.h"
#include "ConstantBuffers.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr uint32_t groupSize = 64;				// numthreads в Shaders/particles.cs
constexpr float maxTimeStep = 0.1f;			// с: после долгого кадра (загрузка, отладчик) — не больше
constexpr float spawnShare = 0.25f;			// за кадр рождается не больше этой доли пула

Property* addSlider( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit = "" )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( std::max( high, value ) );
	property->setControlType( GUIControlType::SLIDER );
	property->setUnit( unit );
	return property;
}

uint32_t groups( uint32_t threads )
{
	return ( threads + groupSize - 1 ) / groupSize;
}

}

ParticleSystem::ParticleSystem() : SceneObject( "Particles" )
{
	m_properties.setName( "Particles" );
}

bool ParticleSystem::initialize( const std::vector<Settings>& emitters, const TerrainHeightSource* terrain )
{
	m_terrain = terrain;
	DMD3D& d3d = DMD3D::instance();
	if( !m_initShader.Initialize( "Shaders\\particles.cs", "mainInit" ) ||
		!m_resetShader.Initialize( "Shaders\\particles.cs", "mainReset" ) ||
		!m_emitShader.Initialize( "Shaders\\particles.cs", "mainEmit" ) ||
		!m_updateShader.Initialize( "Shaders\\particles.cs", "mainUpdate" ) ||
		!d3d.createShaderConstantBuffer( sizeof( TerrainParameters ), m_terrainBuffer ) )
		return false;

	m_quad.initialize( 2, 2 );
	m_program.setLayoutDesc( { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } } );
	if( !m_program.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\particles.vs" ) ||
		!m_program.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\particles.ps" ) )
	{
		LOG( "Particles: shader compilation failed" );
		return false;
	}
	m_phase = m_program.createPhase( 0, 0 );
	if( m_phase < 0 )
		return false;

	for( const Settings& settings : emitters )
	{
		const bool fieldSpawn = settings.spawn == Settings::Spawn::camera || settings.spawn == Settings::Spawn::water;
		if( fieldSpawn && !terrain )
		{
			LOG( "Particle emitter " + settings.name + " needs a terrain, skipped" );
			continue;
		}
		auto emitter = std::make_unique<Emitter>();
		emitter->settings = settings;
		if( !createEmitter( *emitter, static_cast<uint32_t>( m_emitters.size() ) ) )
			return false;
		m_emitters.push_back( std::move( emitter ) );
	}
	m_initialized = !m_emitters.empty();
	LOG( "Particles: " + std::to_string( m_emitters.size() ) + " emitters" );
	return true;
}

bool ParticleSystem::createEmitter( Emitter& emitter, uint32_t index )
{
	DMD3D& d3d = DMD3D::instance();
	const Settings& settings = emitter.settings;
	emitter.capacity = std::max( settings.maxParticles, 64u );

	auto structured = [&]( uint32_t stride, const char* name, Buffer& buffer, StorageView& uav, ShaderView* srv )
	{
		BufferDesc desc;
		desc.size = emitter.capacity * stride;
		desc.stride = stride;
		desc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, nullptr, buffer ) || !d3d.createStorageView( buffer, {}, uav ) ||
			( srv && !d3d.createShaderView( buffer, {}, *srv ) ) )
			return false;
		d3d.setName( buffer, "Particles " + settings.name + " " + name );
		return true;
	};
	BufferDesc stateDesc;
	stateDesc.size = stateBytes;
	stateDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !structured( particleBytes, "pool", emitter.particles, emitter.particlesUAV, &emitter.particlesView ) ||
		!structured( sizeof( uint32_t ), "dead", emitter.dead, emitter.deadUAV, nullptr ) ||
		!structured( sizeof( uint32_t ), "alive", emitter.alive, emitter.aliveUAV, &emitter.aliveView ) ||
		!d3d.createBuffer( stateDesc, nullptr, emitter.state ) || !d3d.createStorageView( emitter.state, rawView, emitter.stateUAV ) ||
		!d3d.createShaderConstantBuffer( sizeof( EmitterParameters ), emitter.constants ) )
	{
		LOG( "Failed to create particle emitter " + settings.name );
		return false;
	}
	d3d.setName( emitter.state, "Particles " + settings.name + " state" );

	if( !settings.mask.empty() )
	{
		if( System::textures().exists( settings.mask ) )
			emitter.mask = &System::textures().get( settings.mask )->srv();
		else
			LOG( "Particle emitter " + settings.name + ": mask " + settings.mask + " is not found, spawning everywhere" );
	}

	// Окно GUI: подокно на эмиттер
	PropertyContainer& properties = emitter.properties;
	properties.setName( settings.name );
	// Всё, кроме способа рождения, формы, маски и ёмкости пула (они задают буферы и ресурсы эмиттера)
	properties.insert( "Enabled", true )->setTooltip( "Off - the emitter stops; not saved with the level" );
	const bool field = settings.spawn == Settings::Spawn::camera || settings.spawn == Settings::Spawn::water;
	addSlider( properties, "Rate", settings.rate, 0.0f, std::max( settings.rate * 4.0f, 10.0f ), field ? "1/s per 100 m2" : "1/s" );
	addSlider( properties, "Radius", settings.radius, 0.0f, std::max( settings.radius * 4.0f, 10.0f ), "m" )
		->setTooltip( field ? "Field around the camera; farther particles die" : "Spawn sphere" );
	addSlider( properties, "Height min", settings.heightMin, -10.0f, 50.0f, "m" )->setTooltip( "Above the terrain or the water surface" );
	addSlider( properties, "Height max", settings.heightMax, -10.0f, 50.0f, "m" );
	addSlider( properties, "Lifetime min", settings.lifetimeMin, 0.0f, std::max( settings.lifetimeMin * 4.0f, 10.0f ), "s" );
	addSlider( properties, "Lifetime max", settings.lifetimeMax, 0.0f, std::max( settings.lifetimeMax * 4.0f, 10.0f ), "s" );
	addSlider( properties, "Size start", settings.sizeStart, 0.001f, std::max( settings.sizeStart * 4.0f, 0.1f ), "m" );
	addSlider( properties, "Size end", settings.sizeEnd, 0.001f, std::max( settings.sizeEnd * 4.0f, 0.1f ), "m" );
	properties.insert( "Color", settings.color )->setControlType( GUIControlType::COLOR );
	addSlider( properties, "Alpha", settings.alpha, 0.0f, 1.0f );
	addSlider( properties, "Fade in", settings.fadeIn, 0.0f, 1.0f )->setTooltip( "Share of the life" );
	addSlider( properties, "Fade out", settings.fadeOut, 0.0f, 1.0f )->setTooltip( "Share of the life" );
	Property* velocity = properties.insert( "Velocity", settings.velocity );
	velocity->setLow( -20.0f );
	velocity->setHigh( 20.0f );
	velocity->setControlType( GUIControlType::DRAG );
	velocity->setUnit( "m/s" )->setTooltip( "Initial velocity" );
	addSlider( properties, "Velocity spread", settings.velocitySpread, 0.0f, 10.0f, "m/s" );
	addSlider( properties, "Gravity", settings.gravity, 0.0f, 20.0f, "m/s2" );
	addSlider( properties, "Drag", settings.drag, 0.0f, 20.0f, "1/s" )->setTooltip( "Velocity tends to the air (wind) and water velocity" );
	addSlider( properties, "Wind", settings.wind, 0.0f, 2.0f )->setTooltip( "Share of the level wind" );
	addSlider( properties, "Curl", settings.curl, 0.0f, 5.0f, "m/s" )->setTooltip( "Strength of the curl noise vortices" );
	addSlider( properties, "Curl scale", settings.curlScale, 0.1f, 50.0f, "m" )->setLogarithmic()->setTooltip( "Size of the vortices" );
	addSlider( properties, "Water flow", settings.waterFlow, 0.0f, 2.0f )->setTooltip( "Share of the water velocity under the particle" );
	addSlider( properties, "Water speed", settings.waterSpeed, 0.0f, 5.0f, "m/s" )->setTooltip( "Spawn on water faster than this" );
	properties.insert( "Collide", settings.collide )->setTooltip( "With the terrain: needles lie down, dots die" );
	addSlider( properties, "Transmission", settings.transmission, 0.0f, 4.0f )->setTooltip( "Glow against the sun (pollen, fluff)" );
	addSlider( properties, "Emissive", settings.emissive, 0.0f, std::max( settings.emissive * 4.0f, 10.0f ), "cd/m2" );
	m_properties.addSubContainer( &properties );

	// Все частицы мертвы, стек мёртвых — весь пул
	PassDesc pass;
	pass.name = "Particles init";
	pass.writes = { { &emitter.particlesUAV, "particles" }, { &emitter.deadUAV, "dead list" }, { &emitter.stateUAV, "state" } };
	d3d.beginPass( pass );
	setParameters( emitter, index, 0, 0.0f );
	d3d.setUAV( 0, emitter.particlesUAV );
	d3d.setUAV( 1, emitter.deadUAV );
	d3d.setUAV( 2, emitter.stateUAV );
	m_initShader.dispatchGroups( groups( emitter.capacity ), 1, 1 );
	return true;
}

ParticleSystem::Settings ParticleSystem::current( const Emitter& emitter ) const
{
	Settings settings = emitter.settings;
	const PropertyContainer& properties = emitter.properties;
	auto value = [&properties]( const char* name ) { return properties[name].data<float>(); };
	settings.rate = std::max( value( "Rate" ), 0.0f );
	settings.radius = std::max( value( "Radius" ), 0.0f );
	settings.heightMin = value( "Height min" );
	settings.heightMax = value( "Height max" );
	settings.lifetimeMin = std::max( value( "Lifetime min" ), 0.0f );
	settings.lifetimeMax = std::max( value( "Lifetime max" ), 0.0f );
	settings.sizeStart = value( "Size start" );
	settings.sizeEnd = value( "Size end" );
	settings.color = properties["Color"].data<XMFLOAT3>();
	settings.alpha = value( "Alpha" );
	settings.fadeIn = value( "Fade in" );
	settings.fadeOut = value( "Fade out" );
	settings.velocity = properties["Velocity"].data<XMFLOAT3>();
	settings.velocitySpread = value( "Velocity spread" );
	settings.gravity = value( "Gravity" );
	settings.drag = value( "Drag" );
	settings.wind = value( "Wind" );
	settings.curl = value( "Curl" );
	settings.curlScale = std::max( value( "Curl scale" ), 0.01f );
	settings.waterFlow = value( "Water flow" );
	settings.waterSpeed = value( "Water speed" );
	settings.collide = properties["Collide"].data<bool>();
	settings.transmission = value( "Transmission" );
	settings.emissive = value( "Emissive" );
	return settings;
}

std::vector<ParticleSystem::Settings> ParticleSystem::emitterSettings() const
{
	std::vector<Settings> result;
	for( const auto& emitter : m_emitters )
		result.push_back( current( *emitter ) );
	return result;
}

bool ParticleSystem::enabled( const Emitter& emitter ) const
{
	return emitter.properties["Enabled"].data<bool>();
}

void ParticleSystem::setParameters( Emitter& emitter, uint32_t index, uint32_t spawnCount, float timeStep )
{
	const Settings settings = current( emitter );
	EmitterParameters params = {};
	params.origin = settings.position;
	params.spawn = static_cast<uint32_t>( settings.spawn );
	params.radius = settings.radius;
	params.heightMin = settings.heightMin;
	params.heightMax = settings.heightMax;
	params.capacity = emitter.capacity;
	params.spawnCount = spawnCount;
	params.frame = m_frame;
	params.emitterIndex = index;
	params.timeStep = timeStep;
	params.lifetimeMin = settings.lifetimeMin;
	params.lifetimeMax = std::max( settings.lifetimeMax, settings.lifetimeMin );
	params.sizeStart = settings.sizeStart;
	params.sizeEnd = settings.sizeEnd;
	params.color = settings.color;
	params.alpha = settings.alpha;
	params.velocity = settings.velocity;
	params.velocitySpread = settings.velocitySpread;
	params.gravity = settings.gravity;
	params.drag = settings.drag;
	params.wind = settings.wind;
	params.curl = settings.curl;
	params.curlScale = settings.curlScale;
	params.waterFlow = settings.waterFlow;
	params.waterSpeed = settings.waterSpeed;
	params.collide = settings.collide ? 1 : 0;
	params.fadeIn = settings.fadeIn;
	params.fadeOut = settings.fadeOut;
	params.transmission = settings.transmission;
	params.emissive = settings.emissive;
	params.shape = static_cast<uint32_t>( settings.shape );
	params.hasMask = emitter.mask ? 1 : 0;
	params.hasTerrain = m_terrain ? 1 : 0;
	params.viewportHeight = static_cast<float>( DMD3D::instance().backBufferHeight() );
	Device::updateResourceData( emitter.constants, params );
	DMD3D::instance().setConstantBuffer( 4, emitter.constants );
}

void ParticleSystem::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	DMD3D& d3d = DMD3D::instance();
	m_timeStep = std::min( frame.elapsedTime * 0.001f, maxTimeStep );
	++m_frame;

	const ShaderView* heightMap = nullptr;
	const ShaderView* foliageClear = nullptr;
	if( m_terrain )
	{
		const TerrainHeight terrain = m_terrain->terrainHeight();
		heightMap = terrain.heightMap;
		foliageClear = terrain.foliageClearMask;
		TerrainParameters params = {};
		params.worldSize = terrain.worldSize;
		params.heightMultiplier = terrain.heightMultiplier;
		params.heightOffset = terrain.heightOffset;
		Device::updateResourceData( m_terrainBuffer, params );
	}

	for( uint32_t index = 0; index < m_emitters.size(); ++index )
	{
		Emitter& emitter = *m_emitters[index];
		if( !enabled( emitter ) )
			continue;
		const Settings settings = current( emitter );

		// Рождений за кадр: у поля вокруг камеры rate — на 100 м²; дробная часть копится
		const bool field = settings.spawn == Settings::Spawn::camera || settings.spawn == Settings::Spawn::water;
		const float area = field ? 3.14159265f * settings.radius * settings.radius / 100.0f : 1.0f;
		emitter.pending += std::max( settings.rate, 0.0f ) * area * m_timeStep;
		const float limit = emitter.capacity * spawnShare;
		const uint32_t spawnCount = static_cast<uint32_t>( std::min( emitter.pending, limit ) );
		emitter.pending = std::min( emitter.pending - spawnCount, limit );

		PassDesc pass;
		pass.name = "Particles";
		if( heightMap )
			pass.reads = { { heightMap, "height map" }, { foliageClear, "foliage clear mask" } };
		if( emitter.mask )
			pass.reads.push_back( { emitter.mask, "density mask" } );
		pass.writes = { { &emitter.particlesUAV, "particles" }, { &emitter.deadUAV, "dead list" },
						{ &emitter.aliveUAV, "alive list" }, { &emitter.stateUAV, "state" } };
		d3d.beginPass( pass );
		setParameters( emitter, index, spawnCount, m_timeStep );
		if( m_terrain )
		{
			d3d.setConstantBuffer( 5, m_terrainBuffer );
			d3d.setSRV( 0, *heightMap );
			d3d.setSRV( 1, *foliageClear );
		}
		if( emitter.mask )
			d3d.setSRV( 2, *emitter.mask );

		// Привязка UAV после dispatch ставит барьер UAV → UAV: каждая стадия видит записанное предыдущей
		auto bind = [&]
		{
			d3d.setUAV( 0, emitter.particlesUAV );
			d3d.setUAV( 1, emitter.deadUAV );
			d3d.setUAV( 2, emitter.stateUAV );
			d3d.setUAV( 3, emitter.aliveUAV );
		};
		bind();
		m_resetShader.dispatchGroups( 1, 1, 1 );
		if( spawnCount > 0 )
		{
			bind();
			m_emitShader.dispatchGroups( groups( spawnCount ), 1, 1 );
		}
		bind();
		m_updateShader.dispatchGroups( groups( emitter.capacity ), 1, 1 );
	}
}

void ParticleSystem::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Только главный вид: тени частицы не отбрасывают. Глубина сцены — мягкий край
	if( m_initialized && view.index == 0 )
		collector.addCustom( passBit( MeshPass::transparent ), 0.0f, false, true );
}

void ParticleSystem::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	m_program.warmPipelines( { { RasterState::noCulling, DepthState::readOnly, BlendState::alpha },
							   { RasterState::wireframe, DepthState::readOnly, BlendState::alpha } }, states.scene, { m_phase } );
}

void ParticleSystem::renderCustom( const RenderContext& context )
{
	if( context.pass != MeshPass::transparent )
		return;
	DMD3D& d3d = DMD3D::instance();
	ScopedRenderState state( context.frameRaster == RasterState::wireframe ? RasterState::wireframe : RasterState::noCulling );
	d3d.setVertexBuffer( m_quad.vertexBuffer(), sizeof( XMFLOAT3 ) );
	d3d.setIndexBuffer( m_quad.indexBuffer(), DXGI_FORMAT_R32_UINT );
	m_program.setPass( m_phase );
	for( uint32_t index = 0; index < m_emitters.size(); ++index )
	{
		Emitter& emitter = *m_emitters[index];
		if( !enabled( emitter ) )
			continue;
		setParameters( emitter, index, 0, m_timeStep );
		d3d.setSRV( 0, emitter.particlesView );
		d3d.setSRV( SLOT_INSTANCE_DATA, emitter.aliveView );
		d3d.drawIndexedInstancedIndirectCount( emitter.state, 0, 1, emitter.state, 24 );
	}
}

PropertyContainer* ParticleSystem::properties()
{
	return &m_properties;
}

}
