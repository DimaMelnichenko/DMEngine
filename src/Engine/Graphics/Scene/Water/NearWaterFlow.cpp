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

}

bool NearWaterFlow::initialize()
{
	// Тот же шейдер, что у течения статичной воды, с FLOW_NEAR; воспроизводимо, как у него (течение одно в Debug и Release)
	const char* file = "Shaders\\water_flow.cs";
	const char* define = "FLOW_NEAR";
	const bool same = true;
	DMD3D& d3d = DMD3D::instance();
	if( !m_init.initialize( file, "mainFlowInit", same, define ) || !m_faces.initialize( file, "mainFlowFaces", same, define ) ||
		!m_advect.initialize( file, "mainFlowAdvect", same, define ) || !m_vorticity.initialize( file, "mainNearVorticity", same, define ) ||
		!m_viscosity.initialize( file, "mainFlowViscosity", same, define ) || !m_nudge.initialize( file, "mainFlowNudge", same, define ) ||
		!m_divergence.initialize( file, "mainFlowDivergence", same, define ) || !m_red.initialize( file, "mainFlowRed", same, define ) ||
		!m_black.initialize( file, "mainFlowBlack", same, define ) || !m_project.initialize( file, "mainFlowProject", same, define ) ||
		!m_output.initialize( file, "mainFlowOutput", same, define ) ||
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
	for( uint32_t k = 0; k < 4; ++k )
	{
		const bool pattern = k >= 2;
		Texture& texture = pattern ? m_pattern[k - 2] : m_foam[k];
		StorageView& uav = pattern ? m_patternUAV[k - 2] : m_foamUAV[k];
		TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = pattern ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16_FLOAT;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		const char* name = pattern ? "Water near foam pattern" : "Water near foam";
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createStorageView( texture, {}, uav ) ||
			!d3d.createShaderView( texture, {}, pattern ? m_patternView[k - 2] : m_foamView[k] ) )
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
	const bool possible = m_initialized && window.enabled && water.detailTileCount() > 0 && height.detailTiles && height.detailIndex;
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
	std::vector<XMFLOAT4> stones;
	stones.reserve( 128 );
	const float minX = m_origin.x * step;
	const float minZ = m_origin.y * step;
	for( size_t s = 0; s < m_stoneBounds.size() && stones.size() < 2 * maxStones; ++s )
	{
		const XMFLOAT4& bounds = m_stoneBounds[s];
		if( bounds.x + bounds.z < minX || bounds.x > minX + side || bounds.y + bounds.z < minZ || bounds.y > minZ + side )
			continue;
		stones.push_back( bounds );
		uint32_t index = static_cast<uint32_t>( s );
		float packed;
		std::memcpy( &packed, &index, sizeof( packed ) );
		stones.push_back( XMFLOAT4( packed, 0.0f, 0.0f, 0.0f ) );
	}
	const uint32_t stoneCount = static_cast<uint32_t>( stones.size() / 2 );
	if( stones.empty() )
		stones.push_back( XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f ) );

	// Шаги — по времени игры кадра (с -freezetime их нет: окно — течение детальной сетки, снимки совпадают)
	const uint32_t blocks = size / 8;
	const uint32_t steps = elapsed > 0.0f ? static_cast<uint32_t>( std::clamp( window.steps, 1, 8 ) ) : 0u;
	const float dt = steps > 0 ? elapsed / steps : 0.0f;
	const WaterFlowBuffer flowParams = water.flowParameters( settings.flow, window.viscosity, dt, step, blocks * blocks );
	const WaterDetailFlowBuffer detailParams = water.detailFlowParameters();
	WaterNearFlowBuffer nearParams = {};
	nearParams.nearOrigin = m_origin;
	nearParams.nearPreviousOrigin = previous;
	nearParams.nearSize = size;
	nearParams.nearStep = step;
	nearParams.nearStoneCount = stoneCount;
	nearParams.nearPassage = std::clamp( window.passage, 0.0f, 1.0f );
	nearParams.nearEddies = std::max( window.eddies, 0.0f );
	nearParams.nearNudgeShare = std::min( dt / nudgeTime, 1.0f );
	nearParams.nearReset = m_reset ? 1u : 0u;
	nearParams.nearSurface = std::max( window.surface, 0.0f );

	PassDesc pass;
	pass.name = "Water near flow";
	pass.reads = { { &water.detailStaticView(), "detail static water" }, { height.detailTiles, "terrain detail tiles" },
				   { height.detailIndex, "terrain detail index" }, { &water.detailWaterView(), "detail water" },
				   { &m_stoneTopsView, "stone tops" } };
	pass.writes = { { &m_infoUAV, "near flow cells" },		   { &m_info2UAV, "near flow radius" },
					{ &m_velocityUAV, "near flow velocity" },	   { &m_advectedUAV, "near flow advected" },
					{ &m_pressureUAV, "near flow pressure" },	   { &m_divergenceUAV, "near flow divergence" },
					{ &m_vorticityUAV, "near flow vorticity" },  { &m_resultUAV, "near flow" } };
	d3d.beginPass( pass );
	field.setParameters( settings );
	Device::updateResourceData( m_flowBuffer, flowParams );
	d3d.setConstantBuffer( 5, m_flowBuffer );
	Device::updateResourceData( m_detailBuffer, detailParams );
	d3d.setConstantBuffer( 6, m_detailBuffer );
	Device::updateResourceData( m_nearBuffer, nearParams );
	d3d.setConstantBuffer( 7, m_nearBuffer );
	d3d.setSRV( 3, water.detailStaticView() );
	d3d.setSRV( 6, *height.detailTiles );
	d3d.setSRV( 7, *height.detailIndex );
	m_stones.updateData( stones.data(), stones.size() * sizeof( XMFLOAT4 ) );
	m_stones.setToSlot( 9 );
	d3d.setSRV( 8, m_stoneTopsView );
	d3d.setSRV( 10, water.detailWaterView() );
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
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( window.iterations, 1, 60 ) );
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
	dispatch( m_output );
	// Шейдерам — до следующего кадра (вода, расстановка, частицы): привязка завершает проход над окном
	d3d.setSRV( SLOT_WATER_NEAR, m_resultView );
	stepFoam( settings, foam, frame.gameTime, elapsed, nearParams );
	m_reset = false;
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
				   { &m_patternView[previous], "near foam pattern previous" } };
	pass.writes = { { &m_foamUAV[m_foamCurrent], "near foam" }, { &m_patternUAV[m_foamCurrent], "near foam pattern" } };
	d3d.beginPass( pass );
	// Правила — поля пены 0,25 м (те же настройки «Surface»), шаг — время игры кадра
	WaterFoamBuffer params = {};
	params.foamSpeed = std::max( settings.foamSpeed, 0.01f );
	params.foamShear = std::max( settings.foamShear, 0.01f );
	params.foamSlope = std::max( settings.channels.foamSlope, 0.01f );
	params.foamTimeStep = elapsed;
	params.foamLifetime = std::max( settings.foamLifetime, 0.1f );
	params.foamStillLifetime = std::max( settings.foamStillLifetime, 0.1f );
	params.foamGrowth = 1.0f - std::exp( -elapsed / WaterFoam::growthTime );
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
	Device::updateResourceData( m_patternBuffer, pattern );
	d3d.setConstantBuffer( 6, m_patternBuffer );
	d3d.setSRV( 0, m_resultView );
	d3d.setSRV( 1, m_foamView[previous] );
	d3d.setSRV( 2, foam.detailView() );
	d3d.setSRV( 3, m_patternView[previous] );
	d3d.setUAV( 0, m_foamUAV[m_foamCurrent] );
	d3d.setUAV( 1, m_patternUAV[m_foamCurrent] );
	const uint32_t groups = ( m_size + 7 ) / 8;
	m_foamStep.dispatchGroups( groups, groups, 1 );
	d3d.setSRV( SLOT_WATER_NEAR_FOAM, m_foamView[m_foamCurrent] );
	d3d.setSRV( SLOT_WATER_NEAR_PATTERN, m_patternView[m_foamCurrent] );
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
	}
}

bool NearWaterFlow::exportState( const std::string& prefix, std::string& reason )
{
	if( !m_active )
	{
		reason = "no near flow window (no detail water around the camera or Near flow is off)";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies, foamCopies;
	std::vector<uint8_t> bytes, foamBytes;
	if( !DMD3D::instance().captureTexture( m_result, copies, bytes ) || copies.empty() ||
		!DMD3D::instance().captureTexture( m_foam[m_foamCurrent], foamCopies, foamBytes ) || foamCopies.empty() )
	{
		reason = "near flow readback failed";
		return false;
	}
	const uint32_t size = m_size;
	std::vector<float> channels[5];
	for( auto& channel : channels )
		channel.resize( static_cast<size_t>( size ) * size );
	for( uint32_t j = 0; j < size; ++j )
		for( uint32_t i = 0; i < size; ++i )
		{
			// Ячейка окна (i, j от его угла, j — по Z) → тексель по модулю; строка растра — с дальнего края по z
			const XMUINT2 texel = NearWindow::address( XMINT2( m_origin.x + static_cast<int32_t>( i ), m_origin.y + static_cast<int32_t>( j ) ), size );
			const auto* source = reinterpret_cast<const PackedVector::HALF*>( bytes.data() + copies[0].offset + texel.y * copies[0].rowPitch ) + texel.x * 4;
			const auto* foam = reinterpret_cast<const PackedVector::HALF*>( foamBytes.data() + foamCopies[0].offset + texel.y * foamCopies[0].rowPitch ) + texel.x;
			const size_t row = static_cast<size_t>( size - 1 - j ) * size + i;
			for( int c = 0; c < 4; ++c )
				channels[c][row] = PackedVector::XMConvertHalfToFloat( source[c] );
			channels[4][row] = PackedVector::XMConvertHalfToFloat( *foam );
		}
	const char* names[5] = { "_near_surface.dds", "_near_vx.dds", "_near_vz.dds", "_near_passage.dds", "_near_foam.dds" };
	for( int c = 0; c < 5; ++c )
	{
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
