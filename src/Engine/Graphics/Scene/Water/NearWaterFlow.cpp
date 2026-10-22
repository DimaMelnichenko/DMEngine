#include "NearWaterFlow.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <tuple>
#include <vector>
#include <DirectXPackedVector.h>
#include "NearWindow.h"
#include "StaticWater.h"
#include "WaterField.h"
#include "WaterFoam.h"
#include "SceneObject.h"
#include "ConstantBuffers.h"
#include "D3D\TextureImages.h"
#include "Logger\Logger.h"
#include "Shaders\slots.h"
#include "Shaders\water_foam_constants.h"
#include "Terrain\TerrainHeightSource.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr float maxFrameTime = 0.1f;	// с: после долгого кадра (загрузка, отладчик) — не больше шага в столько
constexpr float nudgeTime = 1.0f;		// с: средняя блока 8 × 8 тянется к течению детальной сетки за это время
constexpr float focusReach = 30.0f;		// м: окно — у точки взгляда на земле не дальше этого по горизонтали
// Мелкая вода: подшаг не длиннее (волна √(g h) + течение за подшаг — не дальше 0,4 ячейки 6,25 см при глубине до ~1,5 м и
// скорости до 2 м/с; глубже и быстрее — до ~0,7, схема ещё устойчива), подшагов за кадр не больше
constexpr float shallowMaxStep = 1.0f / 240.0f;
constexpr uint32_t shallowMaxSubsteps = 32;
constexpr float shallowNudgeTime = 2.0f;	// с: уровень окна (средний в 1,5 м) тянется к детальной воде за это время

}

bool NearWaterFlow::initialize()
{
	// Тот же шейдер, что у течения статичной воды, с FLOW_NEAR; воспроизводимо, как у него (течение одно в Debug и Release)
	const char* file = "Shaders\\water_flow.cs";
	const char* define = "FLOW_NEAR";
	const char* shallow = "Shaders\\water_near_shallow.cs";
	const bool same = true;
	DMD3D& d3d = DMD3D::instance();
	if( !m_init.initialize( file, "mainFlowInit", same, define ) || !m_faces.initialize( file, "mainFlowFaces", same, define ) ||
		!m_advect.initialize( file, "mainFlowAdvect", same, define ) || !m_vorticity.initialize( file, "mainNearVorticity", same, define ) ||
		!m_viscosity.initialize( file, "mainFlowViscosity", same, define ) || !m_nudge.initialize( file, "mainFlowNudge", same, define ) ||
		!m_divergence.initialize( file, "mainFlowDivergence", same, define ) || !m_red.initialize( file, "mainFlowRed", same, define ) ||
		!m_black.initialize( file, "mainFlowBlack", same, define ) || !m_project.initialize( file, "mainFlowProject", same, define ) ||
		!m_surfacePressure.initialize( file, "mainNearSurfacePressure", same, define ) ||
		!m_output.initialize( file, "mainFlowOutput", same, define ) ||
		!m_shallowInit.initialize( shallow, "mainShallowInit", same ) || !m_shallowFlux.initialize( shallow, "mainShallowFlux", same ) ||
		!m_shallowDepth.initialize( shallow, "mainShallowDepth", same ) || !m_shallowVelocity.initialize( shallow, "mainShallowVelocity", same ) ||
		!m_shallowBlocks.initialize( shallow, "mainShallowBlocks", same ) || !m_shallowOutput.initialize( shallow, "mainShallowOutput", same ) ||
		!m_foamStep.initialize( "Shaders\\water_near_foam.cs", "mainNearFoam", same ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterFoamBuffer ), m_foamBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterNearFlowBuffer ), m_foamWindowBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterNearPatternBuffer ), m_patternBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterFlowBuffer ), m_flowBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterDetailFlowBuffer ), m_detailBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( WaterNearFlowBuffer ), m_nearBuffer ) )
	{
		LOG( "Water near flow: shaders or constants are not created" );
		return false;
	}
	m_stones.createBuffer( sizeof( XMFLOAT4 ), 2 * maxStones, "Water near flow stones" );
	m_initialized = true;
	return true;
}

void NearWaterFlow::setStoneTops( std::vector<StoneTop>&& tops )
{
	static_assert( StoneTop::cells == WATER_NEAR_STONE_CELLS, "stone top squares of the scatter and of the near flow shader differ" );
	constexpr size_t texels = StoneTop::cells * StoneTop::cells;
	m_stoneBounds.clear();
	m_stoneBounds.reserve( tops.size() );
	m_stoneTopData.assign( ( tops.size() * texels + 1 ) / 2, 0u );
	for( size_t s = 0; s < tops.size(); ++s )
	{
		const StoneTop& top = tops[s];
		m_stoneBounds.push_back( XMFLOAT4( top.origin.x, top.origin.y, top.side, top.base ) );
		for( size_t t = 0; t < texels; ++t )
		{
			const size_t index = s * texels + t;
			const uint32_t half = PackedVector::XMConvertFloatToHalf( top.heights[t] );
			m_stoneTopData[index / 2] |= half << ( index % 2 ? 16 : 0 );
		}
	}
	m_stoneTopsPending = true;
	LOG( "Water near flow: " + std::to_string( tops.size() ) + " stream stones with model tops" );
}

bool NearWaterFlow::createTextures( uint32_t size )
{
	DMD3D& d3d = DMD3D::instance();
	for( auto [texture, uav, format, name] :
		 { std::tuple{ &m_info, &m_infoUAV, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water near flow cells" },
		   std::tuple{ &m_info2, &m_info2UAV, DXGI_FORMAT_R32G32_FLOAT, "Water near flow radius" },
		   std::tuple{ &m_velocity, &m_velocityUAV, DXGI_FORMAT_R32G32_FLOAT, "Water near flow velocity" },
		   std::tuple{ &m_advected, &m_advectedUAV, DXGI_FORMAT_R32G32_FLOAT, "Water near flow advected" },
		   std::tuple{ &m_pressure, &m_pressureUAV, DXGI_FORMAT_R32_FLOAT, "Water near flow pressure" },
		   std::tuple{ &m_divergenceTexture, &m_divergenceUAV, DXGI_FORMAT_R32_FLOAT, "Water near flow divergence" },
		   std::tuple{ &m_vorticityTexture, &m_vorticityUAV, DXGI_FORMAT_R32_FLOAT, "Water near flow vorticity" },
		   std::tuple{ &m_result, &m_resultUAV, DXGI_FORMAT_R16G16B16A16_FLOAT, "Water near flow" } } )
	{
		TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess | ( texture == &m_result ? TextureUsage::shaderResource : 0u );
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
		d3d.clearStorageView( *uav );
	}
	if( !d3d.createShaderView( m_result, {}, m_resultView ) )
		return false;
	// Мелкая вода
	const uint32_t blocks = size / 8;
	for( auto [texture, uav, side, format, name] :
		 { std::tuple{ &m_shallow.cells, &m_shallow.cellsUAV, size, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water near shallow cells" },
		   std::tuple{ &m_shallow.depth, &m_shallow.depthUAV, size, DXGI_FORMAT_R32_FLOAT, "Water near shallow depth" },
		   std::tuple{ &m_shallow.velocity[0], &m_shallow.velocityUAV[0], size, DXGI_FORMAT_R32G32_FLOAT, "Water near shallow velocity" },
		   std::tuple{ &m_shallow.velocity[1], &m_shallow.velocityUAV[1], size, DXGI_FORMAT_R32G32_FLOAT, "Water near shallow velocity" },
		   std::tuple{ &m_shallow.flux, &m_shallow.fluxUAV, size, DXGI_FORMAT_R32G32_FLOAT, "Water near shallow flux" },
		   std::tuple{ &m_shallow.detail, &m_shallow.detailUAV, size, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water near shallow detail" },
		   std::tuple{ &m_shallow.blocks, &m_shallow.blocksUAV, blocks, DXGI_FORMAT_R32G32_FLOAT, "Water near shallow blocks" } } )
	{
		TextureDesc desc;
		desc.width = side;
		desc.height = side;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
		d3d.clearStorageView( *uav );
	}
	// Мокрость — R32F: шейдер пены читает своё прошлое значение из UAV (типизированное чтение R32_FLOAT есть везде)
	for( auto [texture, uav, view, format, name] :
		 { std::tuple{ &m_fall, &m_fallUAV, &m_fallView, DXGI_FORMAT_R16_FLOAT, "Water near fall" },
		   std::tuple{ &m_wet, &m_wetUAV, &m_wetView, DXGI_FORMAT_R32_FLOAT, "Water near wetness" } } )
	{
		TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) ||
			!d3d.createShaderView( *texture, {}, *view ) )
		{
			LOG( std::string( "Failed to create texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
		d3d.clearStorageView( *uav );
	}
	for( uint32_t k = 0; k < 6; ++k )
	{
		// Пена, смещения узора и его фаза — по два поля
		const uint32_t kind = k / 2;
		Texture& texture = kind == 0 ? m_foam[k % 2] : kind == 1 ? m_pattern[k % 2] : m_phase[k % 2];
		StorageView& uav = kind == 0 ? m_foamUAV[k % 2] : kind == 1 ? m_patternUAV[k % 2] : m_phaseUAV[k % 2];
		ShaderView& view = kind == 0 ? m_foamView[k % 2] : kind == 1 ? m_patternView[k % 2] : m_phaseView[k % 2];
		TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = kind == 0 ? DXGI_FORMAT_R16_FLOAT : kind == 1 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16_FLOAT;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		const char* name = kind == 0 ? "Water near foam" : kind == 1 ? "Water near foam pattern" : "Water near foam pattern phase";
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createStorageView( texture, {}, uav ) ||
			!d3d.createShaderView( texture, {}, view ) )
		{
			LOG( std::string( "Failed to create texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		d3d.clearStorageView( uav );
	}
	m_foamReset = true;
	m_size = size;
	return true;
}

void NearWaterFlow::compute( const FrameContext& frame, const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
							 const TerrainHeight& height, const WaterFoam& foam )
{
	const Settings& window = settings.nearFlow;
	const float elapsed = std::clamp( frame.gameTime - m_gameTime, 0.0f, maxFrameTime );
	m_gameTime = frame.gameTime;
	const bool possible = m_initialized && window.enabled && water.detailTileCount() > 0 && height.detailTiles && height.detailIndex &&
						  height.heightMap;
	const float step = std::clamp( window.cell, 0.03f, 0.25f );
	const uint32_t size = NearWindow::cells( window.size, step );
	const float side = size * step;
	// Окно — там, куда смотрит камера (вода в десятке метров впереди — в нём, а не позади камеры)
	const TerrainHeightSource* terrain = field.terrain;
	const XMFLOAT2 center = NearWindow::focus( frame.view.position, frame.view.direction, focusReach, side,
											   [terrain]( float x, float z, float& ground ) { return terrain && terrain->surfaceHeight( x, z, ground ); } );
	const float centerX = center.x;
	const float centerZ = center.y;
	// Детальная вода в окне — хотя бы один мокрый блок 2 м (прежде — касание плитки 32 м: окно считалось и в 20 м от ручья)
	m_active = possible && water.detailWaterIn( centerX - 0.5f * side, centerZ - 0.5f * side, centerX + 0.5f * side, centerZ + 0.5f * side );
	if( !m_active )
	{
		m_reset = true;
		return;
	}
	if( size != m_size || step != m_step )
	{
		if( !createTextures( size ) )
		{
			m_active = false;
			return;
		}
		m_step = step;
		m_reset = true;
	}
	const XMINT2 previous = m_origin;
	m_origin = NearWindow::origin( centerX, centerZ, step, size );
	if( NearWindow::jumped( m_origin, previous, size ) )
		m_reset = true;

	// Верх камней — на GPU, раз после расстановки (пустой — буфер в одно слово: слоту нужен ресурс)
	DMD3D& d3d = DMD3D::instance();
	if( m_stoneTopsPending )
	{
		if( m_stoneTopData.empty() )
			m_stoneTopData.push_back( 0u );
		BufferDesc desc;
		desc.size = static_cast<uint32_t>( m_stoneTopData.size() * sizeof( uint32_t ) );
		desc.stride = sizeof( uint32_t );
		desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, m_stoneTopData.data(), m_stoneTops ) || !d3d.createShaderView( m_stoneTops, {}, m_stoneTopsView ) )
		{
			LOG( "Water near flow: stone tops are not created" );
			m_active = false;
			return;
		}
		d3d.setName( m_stoneTops, "Water near flow stone tops" );
		m_stoneTopData.clear();
		m_stoneTopData.shrink_to_fit();
		m_stoneTopsPending = false;
	}

	// Камни русла, задевающие окно: квадрат верха и его номер
	m_windowStones.clear();
	const float minX = m_origin.x * step;
	const float minZ = m_origin.y * step;
	for( size_t s = 0; s < m_stoneBounds.size() && m_windowStones.size() < 2 * maxStones; ++s )
	{
		const XMFLOAT4& bounds = m_stoneBounds[s];
		if( bounds.x + bounds.z < minX || bounds.x > minX + side || bounds.y + bounds.z < minZ || bounds.y > minZ + side )
			continue;
		m_windowStones.push_back( bounds );
		uint32_t index = static_cast<uint32_t>( s );
		float packed;
		std::memcpy( &packed, &index, sizeof( packed ) );
		m_windowStones.push_back( XMFLOAT4( packed, 0.0f, 0.0f, 0.0f ) );
	}

	// Режим сменился — окно заново
	if( window.shallow != m_shallowMode )
	{
		m_shallowMode = window.shallow;
		m_reset = true;
	}
	WaterNearFlowBuffer nearParams = {};
	nearParams.nearOrigin = m_origin;
	nearParams.nearPreviousOrigin = previous;
	nearParams.nearSize = size;
	nearParams.nearStep = step;
	nearParams.nearStoneCount = static_cast<uint32_t>( m_windowStones.size() / 2 );
	nearParams.nearPassage = std::clamp( window.passage, 0.0f, 1.0f );
	nearParams.nearEddies = std::max( window.eddies, 0.0f );
	nearParams.nearReset = m_reset ? 1u : 0u;
	nearParams.nearSurface = std::max( window.surface, 0.0f );
	nearParams.nearShallow = window.shallow ? 1u : 0u;
	nearParams.nearStoneDrag = std::max( window.stoneDrag, 0.0f );
	if( m_windowStones.empty() )
		m_windowStones.push_back( XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ) );
	if( window.shallow )
		stepShallow( settings, field, water, height, elapsed, nearParams );
	else
		stepRigidLid( settings, field, water, height, elapsed, nearParams );
	// Шейдерам — до следующего кадра (вода, расстановка, частицы): привязка завершает проход над окном
	d3d.setSRV( SLOT_WATER_NEAR, m_resultView );
	stepFoam( settings, foam, frame.gameTime, elapsed, nearParams );
	m_reset = false;
}

void NearWaterFlow::bindWindow( const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
								const TerrainHeight& height, const WaterFlowBuffer& flow, const WaterNearFlowBuffer& window )
{
	DMD3D& d3d = DMD3D::instance();
	field.setParameters( settings );
	Device::updateResourceData( m_flowBuffer, flow );
	d3d.setConstantBuffer( 5, m_flowBuffer );
	Device::updateResourceData( m_detailBuffer, water.detailFlowParameters() );
	d3d.setConstantBuffer( 6, m_detailBuffer );
	Device::updateResourceData( m_nearBuffer, window );
	d3d.setConstantBuffer( 7, m_nearBuffer );
	d3d.setSRV( 3, water.detailStaticView() );
	d3d.setSRV( 6, *height.detailTiles );
	d3d.setSRV( 7, *height.detailIndex );
	m_stones.updateData( m_windowStones.data(), m_windowStones.size() * sizeof( XMFLOAT4 ) );
	m_stones.setToSlot( 9 );
	d3d.setSRV( 8, m_stoneTopsView );
	d3d.setSRV( 10, water.detailWaterView() );
	// За краем детальных плиток — вода сетки 1 м и карта высот
	d3d.setSRV( 0, *height.heightMap );
	d3d.setSRV( 11, field.stateView );
}

void NearWaterFlow::stepRigidLid( const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
								  const TerrainHeight& height, float elapsed, WaterNearFlowBuffer window )
{
	const Settings& options = settings.nearFlow;
	// Шаги — по времени игры кадра (с -freezetime их нет: окно — течение детальной сетки, снимки совпадают)
	const uint32_t blocks = m_size / 8;
	const uint32_t steps = elapsed > 0.0f ? static_cast<uint32_t>( std::clamp( options.steps, 1, 8 ) ) : 0u;
	const float dt = steps > 0 ? elapsed / steps : 0.0f;
	window.nearNudgeShare = std::min( dt / nudgeTime, 1.0f );

	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water near flow";
	pass.reads = { { &water.detailStaticView(), "detail static water" }, { height.detailTiles, "terrain detail tiles" },
				   { height.detailIndex, "terrain detail index" }, { &water.detailWaterView(), "detail water" },
				   { &m_stoneTopsView, "stone tops" }, { &field.stateView, "water" }, { height.heightMap, "height map" } };
	pass.writes = { { &m_infoUAV, "near flow cells" },		   { &m_info2UAV, "near flow radius" },
					{ &m_velocityUAV, "near flow velocity" },	   { &m_advectedUAV, "near flow advected" },
					{ &m_pressureUAV, "near flow pressure" },	   { &m_divergenceUAV, "near flow divergence" },
					{ &m_vorticityUAV, "near flow vorticity" },  { &m_resultUAV, "near flow" } };
	d3d.beginPass( pass );
	bindWindow( settings, field, water, height, water.flowParameters( settings.flow, options.viscosity, dt, m_step, blocks * blocks ), window );
	auto dispatch = [&]( ComputeShader& shader )
	{
		d3d.setUAV( 0, m_infoUAV );
		d3d.setUAV( 1, m_velocityUAV );
		d3d.setUAV( 2, m_advectedUAV );
		d3d.setUAV( 3, m_pressureUAV );
		d3d.setUAV( 4, m_divergenceUAV );
		d3d.setUAV( 5, m_resultUAV );
		d3d.setUAV( 6, m_info2UAV );
		d3d.setUAV( 7, m_vorticityUAV );
		shader.dispatchGroups( blocks * blocks, 1, 1 );
	};
	// Ячейки (каждый кадр: окно сдвигается), вошедшие — течение детальной сетки, затем шаги
	dispatch( m_init );
	dispatch( m_faces );
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( options.iterations, 1, 60 ) );
	for( uint32_t done = 0; done < steps; ++done )
	{
		dispatch( m_advect );
		dispatch( m_vorticity );
		dispatch( m_viscosity );
		dispatch( m_nudge );
		dispatch( m_divergence );
		for( uint32_t i = 0; i < iterations; ++i )
		{
			dispatch( m_red );
			dispatch( m_black );
		}
		dispatch( m_project );
	}
	// Подъём поверхности — по сглаженному давлению (после шагов)
	if( steps > 0 )
		dispatch( m_surfacePressure );
	dispatch( m_output );
}

void NearWaterFlow::stepShallow( const WaterSimulationSettings& settings, WaterField& field, const StaticWater& water,
								 const TerrainHeight& height, float elapsed, WaterNearFlowBuffer window )
{
	// Подшаги — по времени игры кадра не длиннее shallowMaxStep (с -freezetime их нет: окно — детальная вода, снимки совпадают)
	const uint32_t blocks = m_size / 8;
	const uint32_t substeps = elapsed > 0.0f ? std::clamp( static_cast<uint32_t>( std::ceil( elapsed / shallowMaxStep ) ), 1u, shallowMaxSubsteps ) : 0u;
	const float dt = substeps > 0 ? elapsed / substeps : 0.0f;
	window.nearNudgeShare = std::min( elapsed / shallowNudgeTime, 1.0f );

	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water near shallow";
	pass.reads = { { &water.detailStaticView(), "detail static water" }, { height.detailTiles, "terrain detail tiles" },
				   { height.detailIndex, "terrain detail index" }, { &water.detailWaterView(), "detail water" },
				   { &m_stoneTopsView, "stone tops" }, { &field.stateView, "water" }, { height.heightMap, "height map" } };
	pass.writes = { { &m_shallow.cellsUAV, "near shallow cells" },		  { &m_shallow.depthUAV, "near shallow depth" },
					{ &m_shallow.velocityUAV[0], "near shallow velocity" }, { &m_shallow.velocityUAV[1], "near shallow velocity" },
					{ &m_shallow.fluxUAV, "near shallow flux" },			  { &m_resultUAV, "near flow" },
					{ &m_shallow.detailUAV, "near shallow detail" },		  { &m_shallow.blocksUAV, "near shallow blocks" } };
	d3d.beginPass( pass );
	// Трение ручья — по шероховатости дна окна (камни и уступы — само дно, сопротивление их формы не считается второй раз)
	WaterFlowBuffer flow = water.flowParameters( settings.flow, 0.0f, dt, m_step, blocks * blocks );
	const float roughness = std::clamp( settings.nearFlow.streamRoughness, 0.0f, 0.2f );
	flow.flowStreamFriction = dt * 9.81f * roughness * roughness;
	bindWindow( settings, field, water, height, flow, window );
	if( m_reset )
		m_shallow.current = 0;
	auto dispatch = [&]( ComputeShader& shader )
	{
		d3d.setUAV( 0, m_shallow.cellsUAV );
		d3d.setUAV( 1, m_shallow.depthUAV );
		d3d.setUAV( 2, m_shallow.velocityUAV[m_shallow.current] );
		d3d.setUAV( 3, m_shallow.velocityUAV[m_shallow.current ^ 1u] );
		d3d.setUAV( 4, m_shallow.fluxUAV );
		d3d.setUAV( 5, m_resultUAV );
		d3d.setUAV( 6, m_shallow.detailUAV );
		d3d.setUAV( 7, m_shallow.blocksUAV );
		shader.dispatchGroups( blocks * blocks, 1, 1 );
	};
	// Ячейки (каждый кадр: окно сдвигается), вошедшие и край окна — детальная вода, затем подшаги
	dispatch( m_shallowInit );
	for( uint32_t done = 0; done < substeps; ++done )
	{
		dispatch( m_shallowFlux );
		dispatch( m_shallowDepth );
		dispatch( m_shallowVelocity );
		m_shallow.current ^= 1u;
	}
	// Крупный масштаб — к детальной воде, итог
	dispatch( m_shallowBlocks );
	dispatch( m_shallowOutput );
}

void NearWaterFlow::stepFoam( const WaterSimulationSettings& settings, const WaterFoam& foam, float gameTime, float elapsed,
							  const WaterNearFlowBuffer& window )
{
	// Без поля пены 0,25 м (ещё не посчитано) брать вошедшим ячейкам нечего — пена окна ждёт его
	if( !foam.ready() )
	{
		m_foamReset = true;
		m_foamShare = 0.0f;
		m_patternShare = 0.0f;
		return;
	}
	const bool reset = m_foamReset || window.nearReset != 0;
	const uint32_t previous = m_foamCurrent;
	m_foamCurrent ^= 1u;
	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water near foam";
	pass.reads = { { &m_resultView, "near flow" }, { &m_foamView[previous], "near foam previous" }, { &foam.detailView(), "detail foam" },
				   { &m_patternView[previous], "near foam pattern previous" }, { &m_phaseView[previous], "near foam pattern phase previous" } };
	pass.writes = { { &m_foamUAV[m_foamCurrent], "near foam" }, { &m_patternUAV[m_foamCurrent], "near foam pattern" },
					{ &m_phaseUAV[m_foamCurrent], "near foam pattern phase" }, { &m_fallUAV, "near fall" }, { &m_wetUAV, "near wetness" } };
	d3d.beginPass( pass );
	// Правила — поля пены 0,25 м (те же настройки «Surface»), шаг — время игры кадра
	const WaterFoamBuffer params = WaterFoam::parameters( settings, elapsed );
	Device::updateResourceData( m_foamBuffer, params );
	d3d.setConstantBuffer( 5, m_foamBuffer );
	WaterNearFlowBuffer windowParams = window;
	windowParams.nearReset = reset ? 1u : 0u;
	Device::updateResourceData( m_foamWindowBuffer, windowParams );
	d3d.setConstantBuffer( 7, m_foamWindowBuffer );
	m_patternPeriod = std::max( settings.nearFlow.patternPeriod, 0.3f );
	WaterNearPatternBuffer pattern = {};
	pattern.nearPatternTime = gameTime;
	pattern.nearPatternStep = elapsed;
	pattern.nearPatternPeriod = m_patternPeriod;
	pattern.nearWetDrying = std::max( settings.nearFlow.wetDrying, 0.0f );
	Device::updateResourceData( m_patternBuffer, pattern );
	d3d.setConstantBuffer( 6, m_patternBuffer );
	d3d.setSRV( 0, m_resultView );
	d3d.setSRV( 1, m_foamView[previous] );
	d3d.setSRV( 2, foam.detailView() );
	d3d.setSRV( 3, m_patternView[previous] );
	d3d.setSRV( 4, m_phaseView[previous] );
	d3d.setUAV( 0, m_foamUAV[m_foamCurrent] );
	d3d.setUAV( 1, m_patternUAV[m_foamCurrent] );
	d3d.setUAV( 2, m_phaseUAV[m_foamCurrent] );
	d3d.setUAV( 3, m_fallUAV );
	d3d.setUAV( 4, m_wetUAV );
	const uint32_t groups = ( m_size + 7 ) / 8;
	m_foamStep.dispatchGroups( groups, groups, 1 );
	d3d.setSRV( SLOT_WATER_NEAR_FOAM, m_foamView[m_foamCurrent] );
	d3d.setSRV( SLOT_WATER_NEAR_PATTERN, m_patternView[m_foamCurrent] );
	d3d.setSRV( SLOT_WATER_NEAR_PHASE, m_phaseView[m_foamCurrent] );
	d3d.setSRV( SLOT_WATER_NEAR_FALL, m_fallView );
	d3d.setSRV( SLOT_WATER_NEAR_WET, m_wetView );
	// Пока время не шло (с -freezetime) — пена окна та же, что поле 0,25 м, а узор не сдвинут: материал берёт поле и плитки
	// (снимки — как без окна)
	m_foamTime = ( reset ? 0.0f : m_foamTime ) + elapsed;
	m_foamShare = m_foamTime > 0.0f ? std::clamp( settings.nearFlow.foam, 0.0f, 1.0f ) : 0.0f;
	m_patternShare = m_foamTime > 0.0f ? std::clamp( settings.nearFlow.pattern, 0.0f, 1.0f ) : 0.0f;
	m_patternStretch = std::max( settings.nearFlow.patternStretch, 1.2f );
	m_foamReset = false;
}

void NearWaterFlow::frameConstants( FrameParameters& constants ) const
{
	if( m_active )
	{
		constants.waterNear = XMFLOAT4( m_origin.x * m_step, m_origin.y * m_step, m_step, static_cast<float>( m_size ) );
		constants.waterNearFoam = XMFLOAT4( m_foamShare, m_patternShare, m_patternPeriod, m_patternStretch );
		constants.waterNearMode = XMFLOAT4( m_shallowMode ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f );
	}
}

bool NearWaterFlow::exportState( const std::string& prefix, std::string& reason )
{
	if( !m_active )
	{
		reason = "no near flow window (no detail water around the camera or Near flow is off)";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies, foamCopies, fallCopies, depthCopies;
	std::vector<uint8_t> bytes, foamBytes, fallBytes, depthBytes;
	if( !DMD3D::instance().captureTexture( m_result, copies, bytes ) || copies.empty() ||
		!DMD3D::instance().captureTexture( m_foam[m_foamCurrent], foamCopies, foamBytes ) || foamCopies.empty() ||
		!DMD3D::instance().captureTexture( m_fall, fallCopies, fallBytes ) || fallCopies.empty() ||
		( m_shallowMode && ( !DMD3D::instance().captureTexture( m_shallow.depth, depthCopies, depthBytes ) || depthCopies.empty() ) ) )
	{
		reason = "near flow readback failed";
		return false;
	}
	const uint32_t size = m_size;
	std::vector<float> channels[7];
	for( auto& channel : channels )
		channel.resize( static_cast<size_t>( size ) * size );
	for( uint32_t j = 0; j < size; ++j )
		for( uint32_t i = 0; i < size; ++i )
		{
			// Ячейка окна (i, j от его угла, j — по Z) → тексель по модулю; строка растра — с дальнего края по z
			const XMUINT2 texel = NearWindow::address( XMINT2( m_origin.x + static_cast<int32_t>( i ), m_origin.y + static_cast<int32_t>( j ) ), size );
			const auto* source = reinterpret_cast<const PackedVector::HALF*>( bytes.data() + copies[0].offset + texel.y * copies[0].rowPitch ) + texel.x * 4;
			const auto* foam = reinterpret_cast<const PackedVector::HALF*>( foamBytes.data() + foamCopies[0].offset + texel.y * foamCopies[0].rowPitch ) + texel.x;
			const auto* fall = reinterpret_cast<const PackedVector::HALF*>( fallBytes.data() + fallCopies[0].offset + texel.y * fallCopies[0].rowPitch ) + texel.x;
			const size_t row = static_cast<size_t>( size - 1 - j ) * size + i;
			for( int c = 0; c < 4; ++c )
				channels[c][row] = PackedVector::XMConvertHalfToFloat( source[c] );
			channels[4][row] = PackedVector::XMConvertHalfToFloat( *foam );
			channels[6][row] = PackedVector::XMConvertHalfToFloat( *fall );
			if( m_shallowMode )
				channels[5][row] = *( reinterpret_cast<const float*>( depthBytes.data() + depthCopies[0].offset + texel.y * depthCopies[0].rowPitch ) + texel.x );
		}
	const char* names[7] = { "_near_surface.dds", "_near_vx.dds", "_near_vz.dds", "_near_passage.dds", "_near_foam.dds", "_near_depth.dds", "_near_fall.dds" };
	for( int c = 0; c < 7; ++c )
	{
		if( c == 5 && !m_shallowMode )
			continue;
		const std::string file = prefix + names[c];
		if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), size, size, channels[c].data() ) )
		{
			reason = "can't write " + file;
			return false;
		}
	}
	char text[200];
	snprintf( text, sizeof( text ), "Water near flow: %ux%u cells of %.4f m from (%.2f, %.2f) -> %s_near_*.dds", size, size, m_step,
			  m_origin.x * m_step, m_origin.y * m_step, prefix.c_str() );
	LOG( text );
	return true;
}

}
