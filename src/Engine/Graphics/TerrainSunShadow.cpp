#include "TerrainSunShadow.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

bool TerrainSunShadow::initialize()
{
	if( !m_shader.Initialize( "Shaders\\terrain_sun_shadow.cs", "main" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constants ) )
	{
		LOG( "Terrain sun shadow: shader or constants are not created" );
		return false;
	}
	return true;
}

bool TerrainSunShadow::createTexture( uint32_t size )
{
	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	if( !d3d.createTexture( desc, nullptr, m_texture ) || !d3d.createStorageView( m_texture, {}, m_storageView ) ||
		!d3d.createShaderView( m_texture, {}, m_shaderView ) )
	{
		LOG( "Terrain sun shadow: texture is not created" );
		return false;
	}
	d3d.setName( m_texture, "Terrain sun shadow" );
	m_size = size;
	return true;
}

void TerrainSunShadow::update( const TerrainHeight& terrain, const XMFLOAT3& toLight )
{
	if( !terrain.heightMap || !terrain.heightMap->valid() || terrain.mapSize < 2 || terrain.worldSize <= 0.0f )
	{
		m_valid = false;
		return;
	}
	const uint32_t size = std::max( terrain.mapSize / 2, 1u );
	const bool resized = size != m_size;
	if( resized && !createTexture( size ) )
	{
		m_valid = false;
		return;
	}

	const float cosChange = std::cos( XMConvertToRadians( changeAngle ) );
	const float turn = toLight.x * m_direction.x + toLight.y * m_direction.y + toLight.z * m_direction.z;
	if( m_valid && !resized && turn >= cosChange && terrain.revision == m_revision &&
		terrain.heightMultiplier == m_heightMultiplier && terrain.heightOffset == m_heightOffset )
		return;

	Parameters params = {};
	const float horizontal = std::sqrt( toLight.x * toLight.x + toLight.z * toLight.z );
	// Источник в зените: азимута нет, и заслонять нечему
	params.sunAzimuth = horizontal > 1e-4f ? XMFLOAT2( toLight.x / horizontal, toLight.z / horizontal ) : XMFLOAT2( 1.0f, 0.0f );
	params.sunSlope = horizontal > 1e-4f ? toLight.y / horizontal : 1e6f;
	params.texelSize = terrain.worldSize / static_cast<float>( size );
	params.heightMultiplier = terrain.heightMultiplier;
	params.heightOffset = terrain.heightOffset;
	params.worldSize = terrain.worldSize;
	params.maxHeight = terrain.maxValue * terrain.heightMultiplier + terrain.heightOffset;
	params.mapTexel = terrain.worldSize / static_cast<float>( terrain.mapSize );
	params.startDistance = 2.0f * params.texelSize;
	params.size = size;

	DMD3D& d3d = DMD3D::instance();
	Device::updateResourceData<Parameters>( m_constants, params );
	d3d.beginPass( PassDesc{ "Terrain sun shadow", {}, {}, 0, 0, { { terrain.heightMap, "terrain height map" } },
							 { { &m_storageView, "terrain sun shadow" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 0, *terrain.heightMap );
	m_shader.setUAVBuffer( 0, m_storageView );
	m_shader.dispatchGroups( ( size + 7 ) / 8, ( size + 7 ) / 8, 1 );

	m_worldSize = terrain.worldSize;
	m_direction = toLight;
	m_revision = terrain.revision;
	m_heightMultiplier = terrain.heightMultiplier;
	m_heightOffset = terrain.heightOffset;
	m_valid = true;
	++m_updates;
}

}
