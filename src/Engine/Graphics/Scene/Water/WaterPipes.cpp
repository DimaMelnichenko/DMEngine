#include "WaterPipes.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>
#include <DirectXPackedVector.h>
#include "Assets.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Utils\Fingerprint.h"
#include "D3D\TextureImages.h"

namespace GS
{

namespace
{

constexpr uint32_t groupSize = WaterField::groupSize;
constexpr uint32_t maxStepsPerFrame = 16;		// больше за кадр не догоняем: симуляция отстаёт, но кадр не растёт
constexpr uint32_t warmupStepsPerSubmit = 500;
// Итераций заполнения низин и растекания метки озера: уровень и метка проходят за итерацию хотя бы ячейку, обычно —
// много (запись на месте); с запасом на путь от края карты до середины и через озеро
constexpr uint32_t fillIterations = 2048;
constexpr uint32_t lakeGrowIterations = 512;
constexpr uint32_t iterationsPerSubmit = 512;
constexpr uint32_t validationWarmupSteps = 600;	// при загрузке — порциями, чтобы одна отправка GPU не шла секундами

bool sameSources( const WaterSimulationSettings& a, const WaterSimulationSettings& b )
{
	return a.sourceRate == b.sourceRate && a.flowStart == b.flowStart && a.flowFull == b.flowFull && a.sourceRadius == b.sourceRadius;
}

}

bool WaterPipes::initialize( WaterField& field, const WaterSimulationSettings& settings )
{
	m_field = &field;
	DMD3D& d3d = DMD3D::instance();
	if( !m_sourcesShader.initialize( "Shaders\\water_simulation.cs", "mainSources" ) ||
		!m_fluxShader.initialize( "Shaders\\water_simulation.cs", "mainFlux" ) ||
		!m_waterShader.initialize( "Shaders\\water_simulation.cs", "mainWater" ) ||
		!m_fillInitShader.initialize( "Shaders\\water_simulation.cs", "mainFillInit" ) ||
		!m_fillShader.initialize( "Shaders\\water_simulation.cs", "mainFill" ) ||
		!m_lakeInitShader.initialize( "Shaders\\water_simulation.cs", "mainLakeInit" ) ||
		!m_lakeGrowShader.initialize( "Shaders\\water_simulation.cs", "mainLakeGrow" ) ||
		!m_lakeApplyShader.initialize( "Shaders\\water_simulation.cs", "mainLakeApply" ) )
		return false;

	auto create = [&]( DXGI_FORMAT format, const char* name, Texture& texture, StorageView& uav, ShaderView* srv )
	{
		TextureDesc desc;
		desc.width = m_field->size;
		desc.height = m_field->size;
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
	if( !create( DXGI_FORMAT_R32G32B32A32_FLOAT, "Water flux", m_flux, m_fluxUAV, nullptr ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water sources", m_sources, m_sourcesUAV, &m_sourcesView ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water memory", m_memory, m_memoryUAV, nullptr ) )
		return false;

	// Источники-помощники — гауссовы пятна притока в mainSources: x, z, расход м³/с, σ (не меньше ячейки)
	if( !settings.sources.empty() )
	{
		std::vector<DirectX::XMFLOAT4> helpers;
		for( const WaterSource& source : settings.sources )
			helpers.emplace_back( source.position.x, source.position.y, source.rate * 0.001f, std::max( source.radius * 0.5f, m_field->cellSize ) );
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
		m_field->helperCount = static_cast<uint32_t>( helpers.size() );
		for( const WaterSource& source : settings.sources )
			LOG( "Water source " + source.name + ": " + std::to_string( source.rate ) + " l/s at " +
				 std::to_string( source.position.x ) + ", " + std::to_string( source.position.y ) );
	}
	return true;
}

bool WaterPipes::setChannelFlow( const TerrainHydrology::Result& hydrology )
{
	DMD3D& d3d = DMD3D::instance();
	const uint32_t size = m_field->size;
	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.format = DXGI_FORMAT_R32_FLOAT;
	TextureData initial;
	initial.data = hydrology.channelFlow.data();
	initial.rowPitch = size * static_cast<uint32_t>( sizeof( float ) );
	initial.slicePitch = initial.rowPitch * size;
	if( !d3d.createTexture( desc, &initial, m_channelFlow ) || !d3d.createShaderView( m_channelFlow, {}, m_channelFlowView ) )
	{
		LOG( "Failed to create water texture Water channel flow" );
		return false;
	}
	d3d.setName( m_channelFlow, "Water channel flow" );
	return true;
}

void WaterPipes::buildSources( const WaterSimulationSettings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& flowMap = m_channelFlowView;
	PassDesc pass;
	pass.name = "Water sources";
	pass.reads = { { &flowMap, "flow map" } };
	if( m_field->helperCount > 0 )
		pass.reads.push_back( { &m_helpersView, "water source helpers" } );
	pass.writes = { { &m_sourcesUAV, "water sources" } };
	d3d.beginPass( pass );
	m_field->setParameters( settings );
	d3d.setSRV( 1, flowMap );
	if( m_field->helperCount > 0 )
		d3d.setSRV( 4, m_helpersView );
	d3d.setUAV( 3, m_sourcesUAV );
	const uint32_t groups = ( m_field->size + groupSize - 1 ) / groupSize;
	m_sourcesShader.dispatchGroups( groups, groups, 1 );
	m_sourcesBuilt = settings;
}

bool WaterPipes::fillLakes( const WaterSimulationSettings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	Texture fill, lake;
	StorageView fillUAV, lakeUAV;
	for( auto [texture, uav, name] : { std::tuple{ &fill, &fillUAV, "Water lake level" }, std::tuple{ &lake, &lakeUAV, "Water lake mask" } } )
	{
		TextureDesc desc;
		desc.width = m_field->size;
		desc.height = m_field->size;
		desc.format = DXGI_FORMAT_R32_FLOAT;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
	}

	const ShaderView& heightMap = *m_field->terrain->terrainHeight().heightMap;
	const uint32_t groups = ( m_field->size + groupSize - 1 ) / groupSize;
	auto run = [&]( ComputeShader& shader, uint32_t iterations )
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
				pass.writes = { { &fillUAV, "lake level" }, { &lakeUAV, "lake mask" }, { &m_field->depthUAV, "water depth" } };
				d3d.beginPass( pass );
				m_field->setParameters( settings );
				d3d.setSRV( 0, heightMap );
				d3d.setSRV( 2, m_sourcesView );
			}
			d3d.setUAV( 0, m_field->depthUAV );
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

uint32_t WaterPipes::warmUp( const WaterSimulationSettings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	uint32_t warmupSteps = settings.timeStep > 0.0f ? static_cast<uint32_t>( settings.warmupTime / settings.timeStep ) : 0;
	if( d3d.gpuValidation() && warmupSteps > validationWarmupSteps )
	{
		// Под GPU-based validation шаг в десятки раз дольше: озёра уже налиты, ручьи досчитаются в игре
		LOG( "Water simulation: warm-up is cut to " + std::to_string( validationWarmupSteps ) + " steps under GPU-based validation" );
		warmupSteps = validationWarmupSteps;
	}
	SlowTask warmup( static_cast<float>( std::max( warmupSteps, 1u ) ) );
	for( uint32_t done = 0; done < warmupSteps; done += warmupStepsPerSubmit )
	{
		// Порция — граница шага прогресса: параметры и привязки она ставит сама
		warmup.enterProgressFrame( static_cast<float>( std::min( warmupStepsPerSubmit, warmupSteps - done ) ) );
		m_field->setParameters( settings );
		step( std::min( warmupStepsPerSubmit, warmupSteps - done ) );
		d3d.waitForGpu();
	}
	return warmupSteps;
}

bool WaterPipes::advance( const FrameContext& frame, const WaterSimulationSettings& current )
{
	if( !sameSources( current, m_sourcesBuilt ) )
		buildSources( current );
	// Время симуляции — по времени кадра (с timestep — фиксированному), не больше maxStepsPerFrame шагов за кадр
	if( current.timeStep <= 0.0f )
		return false;
	m_accumulated = std::min( m_accumulated + frame.elapsedTime * 0.001f * current.timeScale, current.timeStep * maxStepsPerFrame );
	const uint32_t steps = static_cast<uint32_t>( m_accumulated / current.timeStep );
	m_accumulated -= steps * current.timeStep;
	if( steps == 0 )
		return false;
	m_field->setParameters( current );
	step( steps );
	return true;
}

void WaterPipes::step( uint32_t steps )
{
	if( steps == 0 )
		return;
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_field->terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water simulation";
	pass.reads = { { &heightMap, "height map" }, { &m_sourcesView, "water sources" } };
	pass.writes = { { &m_field->depthUAV, "water depth" }, { &m_fluxUAV, "water flux" }, { &m_field->stateUAV, "water state" },
					{ &m_memoryUAV, "water memory" } };
	d3d.beginPass( pass );
	// Константы привязаны setParameters до прохода; beginPass очищает только таблицу привязок
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 2, m_sourcesView );
	const uint32_t groups = ( m_field->size + groupSize - 1 ) / groupSize;
	for( uint32_t i = 0; i < steps; ++i )
	{
		// Привязка UAV после dispatch ставит барьер UAV → UAV: следующая стадия видит записанное предыдущей
		d3d.setUAV( 0, m_field->depthUAV );
		d3d.setUAV( 1, m_fluxUAV );
		m_fluxShader.dispatchGroups( groups, groups, 1 );
		d3d.setUAV( 0, m_field->depthUAV );
		d3d.setUAV( 1, m_fluxUAV );
		d3d.setUAV( 2, m_field->stateUAV );
		d3d.setUAV( 6, m_memoryUAV );
		m_waterShader.dispatchGroups( groups, groups, 1 );
	}
}

bool WaterPipes::exportDischarge( const std::string& file, std::string& reason )
{
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_flux, copies, bytes ) || copies.empty() )
	{
		reason = "water flux readback failed";
		return false;
	}
	std::vector<float> discharge( static_cast<size_t>( m_field->size ) * m_field->size );
	double total = 0.0;
	float peak = 0.0f;
	for( uint32_t row = 0; row < m_field->size; ++row )
	{
		const float* flux = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + row * copies[0].rowPitch );
		for( uint32_t col = 0; col < m_field->size; ++col )
		{
			const float* out = flux + col * 4;
			const float q = out[0] + out[1] + out[2] + out[3];
			discharge[static_cast<size_t>( row ) * m_field->size + col] = q;
			peak = std::max( peak, q );
			total += q;
		}
	}
	if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), m_field->size, m_field->size, discharge.data() ) )
	{
		reason = "cannot write " + file;
		return false;
	}
	char text[160];
	snprintf( text, sizeof( text ), "Water discharge: %ux%u, peak %.3f m3/s -> ", m_field->size, m_field->size, peak );
	LOG( text + file );
	return true;
}

}
