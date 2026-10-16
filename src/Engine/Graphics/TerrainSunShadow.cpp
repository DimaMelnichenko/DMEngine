#include "TerrainSunShadow.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

using namespace DirectX;

namespace GS
{

bool TerrainSunShadow::initialize( bool mountains )
{
	m_mountains = mountains;
	if( !m_shader.initialize( "Shaders\\terrain_sun_shadow.cs", "main" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( TerrainSunShadowBuffer ), m_constants ) )
	{
		LOG( "Terrain sun shadow: shader or constants are not created" );
		return false;
	}
	return true;
}

bool TerrainSunShadow::createTextures( uint32_t size )
{
	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	TextureDesc external = desc;
	external.format = DXGI_FORMAT_R32G32_FLOAT;
	if( !d3d.createTexture( desc, nullptr, m_texture ) || !d3d.createStorageView( m_texture, {}, m_storageView ) ||
		!d3d.createShaderView( m_texture, {}, m_shaderView ) || !d3d.createTexture( external, nullptr, m_externalTexture ) ||
		!d3d.createStorageView( m_externalTexture, {}, m_externalStorage ) ||
		!d3d.createShaderView( m_externalTexture, {}, m_externalView ) )
	{
		LOG( "Terrain sun shadow: texture is not created" );
		return false;
	}
	d3d.setName( m_texture, m_mountains ? "Mountain sun shadow" : "Terrain sun shadow" );
	d3d.setName( m_externalTexture, m_mountains ? "Mountain sun shadow external" : "Terrain sun shadow external" );
	m_size = size;
	return true;
}

void TerrainSunShadow::update( const TerrainHeight& terrain, const FarTerrainHeight& farTerrain, const XMFLOAT3& toLight )
{
	const bool hasMap = terrain.heightMap && terrain.heightMap->valid() && terrain.mapSize >= 2 && terrain.worldSize > 0.0f;
	const bool hasFar = farTerrain.heightMap && farTerrain.heightMap->valid() && farTerrain.extent > 0.0f && farTerrain.size > 0;
	// Область: карта уровня или всё кольцо гор
	if( !hasMap || ( m_mountains && ( !hasFar || farTerrain.shadowSize == 0 ) ) )
	{
		m_valid = false;
		return;
	}
	const uint32_t size = m_mountains ? farTerrain.shadowSize : std::max( terrain.mapSize / 2, 1u );
	const XMFLOAT2 origin = m_mountains ? farTerrain.origin : XMFLOAT2( 0.0f, 0.0f );
	const float extent = m_mountains ? farTerrain.extent : terrain.worldSize;
	const bool resized = size != m_size;
	if( resized && !createTextures( size ) )
	{
		m_valid = false;
		return;
	}

	const float cosChange = std::cos( XMConvertToRadians( changeAngle ) );
	const float turn = toLight.x * m_direction.x + toLight.y * m_direction.y + toLight.z * m_direction.z;
	const ShaderView* farMap = hasFar ? farTerrain.heightMap : nullptr;
	if( m_valid && !resized && turn >= cosChange && terrain.revision == m_revision &&
		terrain.heightMultiplier == m_heightMultiplier && terrain.heightOffset == m_heightOffset && farMap == m_farMap &&
		( !farMap || farTerrain.revision == m_farRevision ) && origin.x == m_origin.x && origin.y == m_origin.y && extent == m_extent )
		return;

	TerrainSunShadowBuffer params = {};
	const float horizontal = std::sqrt( toLight.x * toLight.x + toLight.z * toLight.z );
	// Источник в зените: азимута нет, и заслонять нечему
	params.sunAzimuth = horizontal > 1e-4f ? XMFLOAT2( toLight.x / horizontal, toLight.z / horizontal ) : XMFLOAT2( 1.0f, 0.0f );
	params.sunSlope = horizontal > 1e-4f ? toLight.y / horizontal : 1e6f;
	params.texelSize = extent / static_cast<float>( size );
	params.areaOrigin = origin;
	params.size = size;
	params.startDistance = 2.0f * params.texelSize;
	params.heightMultiplier = terrain.heightMultiplier;
	params.heightOffset = terrain.heightOffset;
	params.worldSize = terrain.worldSize;
	params.mapTexel = terrain.worldSize / static_cast<float>( terrain.mapSize );
	params.maxHeight = terrain.maxValue * terrain.heightMultiplier + terrain.heightOffset;
	if( hasFar )
	{
		params.farOrigin = farTerrain.origin;
		params.farExtent = farTerrain.extent;
		params.farTexel = farTerrain.extent / static_cast<float>( farTerrain.size );
		params.maxHeight = std::max( params.maxHeight, farTerrain.maxHeight );
	}
	// У карты уровня заслоны за её краем (горы) — отдельно: их тени нет в каскадах
	params.external = !m_mountains && hasFar ? 1 : 0;

	DMD3D& d3d = DMD3D::instance();
	Device::updateResourceData<TerrainSunShadowBuffer>( m_constants, params );
	const ShaderView& farView = hasFar ? *farTerrain.heightMap : *terrain.heightMap;	// без гор — заглушка: шейдер её не читает
	d3d.beginPass( PassDesc{ m_mountains ? "Mountain sun shadow" : "Terrain sun shadow", {}, {}, 0, 0,
							 { { terrain.heightMap, "terrain height map" }, { &farView, "far terrain heights" } },
							 { { &m_storageView, "terrain sun shadow" }, { &m_externalStorage, "terrain sun shadow external" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 0, *terrain.heightMap );
	d3d.setSRV( 1, farView );
	m_shader.setUAVBuffer( 0, m_storageView );
	m_shader.setUAVBuffer( 1, m_externalStorage );
	m_shader.dispatchGroups( ( size + 7 ) / 8, ( size + 7 ) / 8, 1 );

	m_origin = origin;
	m_extent = extent;
	m_external = params.external != 0;
	m_direction = toLight;
	m_revision = terrain.revision;
	m_heightMultiplier = terrain.heightMultiplier;
	m_heightOffset = terrain.heightOffset;
	m_farMap = farMap;
	m_farRevision = hasFar ? farTerrain.revision : 0;
	m_valid = true;
	++m_updates;
}

}
