#include "WaterField.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr int32_t maxSourceRadius = 8;			// ячеек: размытие источников — цикл (2r + 1)² в шейдере
constexpr float mmPerHour = 0.001f / 3600.0f;	// мм/ч → м/с

}

bool WaterField::initialize( const TerrainHeightSource& source )
{
	terrain = &source;
	const TerrainHeight height = source.terrainHeight();
	size = height.mapSize;
	cellSize = height.worldSize / std::max( size, 1u );
	if( size == 0 || !height.heightMap )
	{
		LOG( "Water simulation needs a terrain height map" );
		return false;
	}
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createShaderConstantBuffer( sizeof( Parameters ), constants ) )
		return false;
	auto create = [&]( DXGI_FORMAT format, const char* name, Texture& texture, StorageView& uav, ShaderView& srv )
	{
		TextureDesc desc;
		desc.width = size;
		desc.height = size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createStorageView( texture, {}, uav ) || !d3d.createShaderView( texture, {}, srv ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		// Без начальных данных память не обнулена: первая операция — очистка
		d3d.clearStorageView( uav );
		return true;
	};
	return create( DXGI_FORMAT_R32_FLOAT, "Water depth", depth, depthUAV, depthView ) &&
		   create( DXGI_FORMAT_R16G16B16A16_FLOAT, "Water state", state, stateUAV, stateView );
}

void WaterField::setParameters( const WaterSimulationSettings& settings )
{
	const TerrainHeight height = terrain->terrainHeight();
	Parameters params = {};
	params.size = size;
	params.cellSize = cellSize;
	params.timeStep = settings.timeStep;
	params.gravity = gravity;
	params.rain = settings.rain * mmPerHour;
	params.evaporation = settings.evaporation * mmPerHour;
	// л/с на ячейку → м/с слоя воды на её площади
	params.sourceRate = settings.sourceRate * 0.001f / ( cellSize * cellSize );
	params.heightMultiplier = height.heightMultiplier;
	params.heightOffset = height.heightOffset;
	params.logFlowStart = std::log10( std::max( settings.flowStart, 1.0f ) );
	params.logFlowFull = std::max( std::log10( std::max( settings.flowFull, 1.0f ) ), params.logFlowStart + 0.01f );
	params.sourceRadius = std::clamp( static_cast<int32_t>( std::lround( settings.sourceRadius / cellSize ) ), 0, maxSourceRadius );
	params.manning = settings.manning;
	params.helperCount = helperCount;
	Device::updateResourceData( constants, params );
	DMD3D::instance().setConstantBuffer( 4, constants );
}

}
