#include "TerrainErosion.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include "D3D\DMD3D.h"
#include "DMComputeShader.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr uint32_t dropletGroup = 64;	// DROPLET_GROUP в terrain_erosion.cs
constexpr uint32_t cellGroup = 8;		// CELL_GROUP
constexpr uint32_t batchSize = 65536;	// капель в пакете: внутри шага друг друга не видят

// Раскладка — cbuffer TerrainErosionBuffer (b4) в terrain_erosion.cs
struct alignas( 16 ) Parameters
{
	uint32_t size;
	uint32_t padded;
	uint32_t pad;
	int32_t radius;
	float cell;
	float inertia;
	float capacity;
	float minSlope;
	float erodeSpeed;
	float depositSpeed;
	float evaporation;
	float gravity;
	float brushSum;
	uint32_t dropletCount;
	uint32_t batch;
	uint32_t seed;
	float padding0;
	float rainScale;
	float rainMin;
	float talusTangent;
	float thermalRate;
	float padding[3];
};
static_assert( sizeof( Parameters ) == 96, "TerrainErosionBuffer layout" );

// Droplet в terrain_erosion.cs
struct Droplet
{
	float position[2];
	float direction[2];
	float speed;
	float water;
	float sediment;
	uint32_t alive;
	uint32_t key;
	uint32_t step;
};

struct GpuTexture
{
	Texture texture;
	StorageView uav;
};

bool createTexture( uint32_t size, DXGI_FORMAT format, const void* data, uint32_t texelBytes, const char* name, GpuTexture& result )
{
	DMD3D& d3d = DMD3D::instance();
	TextureDesc desc;
	desc.width = size;
	desc.height = size;
	desc.format = format;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	TextureData initial;
	initial.data = data;
	initial.rowPitch = size * texelBytes;
	initial.slicePitch = initial.rowPitch * size;
	if( !d3d.createTexture( desc, data ? &initial : nullptr, result.texture ) || !d3d.createStorageView( result.texture, {}, result.uav ) )
	{
		LOG( std::string( "Terrain erosion: texture " ) + name + " is not created" );
		return false;
	}
	d3d.setName( result.texture, name );
	if( !data )
		d3d.clearStorageView( result.uav );
	return true;
}

// Текстура R32 на CPU: size × size значений
bool readTexture( const Texture& texture, uint32_t size, std::vector<float>& values )
{
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( texture, copies, bytes ) || copies.empty() )
		return false;
	values.resize( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		std::memcpy( &values[static_cast<size_t>( row ) * size], bytes.data() + copies[0].offset + static_cast<uint64_t>( row ) * copies[0].rowPitch,
					 size * sizeof( float ) );
	return true;
}

}

bool TerrainErosion::run( const std::vector<float>& heights, uint32_t size, float cell, const TerrainErosionSettings& settings,
						  Result& result )
{
	const auto start = std::chrono::steady_clock::now();
	DMD3D& d3d = DMD3D::instance();
	// Шейдеры — одинаковые в Debug и Release, строгий IEEE: результат кэшируется на диске и не должен зависеть ни от сборки,
	// ни от debug-слоя (капли хаотичны: разница в последнем бите вырастает в метры)
	DMComputeShader spawnShader, dropletShader, applyShader, thermalOutShader, thermalInShader;
	if( !spawnShader.Initialize( "Shaders\\terrain_erosion.cs", "mainSpawn", true ) ||
		!dropletShader.Initialize( "Shaders\\terrain_erosion.cs", "mainDroplet", true ) ||
		!applyShader.Initialize( "Shaders\\terrain_erosion.cs", "mainApply", true ) ||
		!thermalOutShader.Initialize( "Shaders\\terrain_erosion.cs", "mainThermalOut", true ) ||
		!thermalInShader.Initialize( "Shaders\\terrain_erosion.cs", "mainThermalIn", true ) )
		return false;

	// Карта капель — продолжена за край на кисть: капли доходят до края и уходят за него, без нетронутой рамки
	const int32_t radius = std::max( settings.radius, 1 );
	const uint32_t pad = static_cast<uint32_t>( radius ) + 1;
	const uint32_t padded = size + 2 * pad;
	std::vector<float> extended( static_cast<size_t>( padded ) * padded );
	for( uint32_t row = 0; row < padded; ++row )
	{
		const uint32_t sourceRow = static_cast<uint32_t>( std::clamp<int64_t>( static_cast<int64_t>( row ) - pad, 0, size - 1 ) );
		for( uint32_t col = 0; col < padded; ++col )
		{
			const uint32_t sourceCol = static_cast<uint32_t>( std::clamp<int64_t>( static_cast<int64_t>( col ) - pad, 0, size - 1 ) );
			extended[static_cast<size_t>( row ) * padded + col] = heights[static_cast<size_t>( sourceRow ) * size + sourceCol];
		}
	}

	// Сумма весов кисти — как в прежнем erosion.py: max(r − расстояние, 0) по клеткам круга
	double brushSum = 0.0;
	for( int32_t y = -radius; y <= radius; ++y )
		for( int32_t x = -radius; x <= radius; ++x )
			if( x * x + y * y <= radius * radius )
				brushSum += std::max( radius - std::sqrt( static_cast<double>( x * x + y * y ) ), 0.0 );

	Parameters params = {};
	params.size = size;
	params.padded = padded;
	params.pad = pad;
	params.radius = radius;
	params.cell = cell;
	params.inertia = settings.inertia;
	params.capacity = settings.capacity;
	params.minSlope = settings.minSlope;
	params.erodeSpeed = settings.erodeSpeed;
	params.depositSpeed = settings.depositSpeed;
	params.evaporation = settings.evaporation;
	params.gravity = settings.gravity;
	params.brushSum = static_cast<float>( brushSum );
	params.seed = settings.seed;
	params.rainScale = std::max( settings.rainScale, 1.0f );
	params.rainMin = std::clamp( settings.rainMin, 0.0f, 1.0f );
	params.talusTangent = std::tan( settings.talusAngle * 3.14159265f / 180.0f );
	params.thermalRate = settings.thermalRate;
	Buffer constants;
	if( !d3d.createShaderConstantBuffer( sizeof( Parameters ), constants ) )
		return false;
	auto setParameters = [&]
	{
		Device::updateResourceData( constants, params );
		d3d.setConstantBuffer( 4, constants );
	};

	// 1. Капли
	GpuTexture height, delta;
	Buffer droplets;
	StorageView dropletsUAV;
	BufferDesc dropletsDesc;
	dropletsDesc.size = batchSize * sizeof( Droplet );
	dropletsDesc.stride = sizeof( Droplet );
	dropletsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::structured;
	if( !createTexture( padded, DXGI_FORMAT_R32_FLOAT, extended.data(), sizeof( float ), "Erosion height", height ) ||
		!createTexture( padded, DXGI_FORMAT_R32_SINT, nullptr, sizeof( int32_t ), "Erosion change", delta ) ||
		!d3d.createBuffer( dropletsDesc, nullptr, droplets ) || !d3d.createStorageView( droplets, {}, dropletsUAV ) )
		return false;
	d3d.setName( droplets, "Erosion droplets" );

	const uint32_t lifetime = static_cast<uint32_t>( std::max( settings.lifetime, 1 ) );
	const uint32_t cellGroups = ( padded + cellGroup - 1 ) / cellGroup;
	uint32_t remaining = static_cast<uint32_t>( std::max( settings.droplets, 0 ) );
	for( uint32_t batch = 0; remaining > 0; ++batch )
	{
		const uint32_t count = std::min( remaining, batchSize );
		remaining -= count;
		params.dropletCount = count;
		params.batch = batch;
		PassDesc pass;
		pass.name = "Terrain erosion droplets";
		pass.writes = { { &height.uav, "erosion height" }, { &delta.uav, "erosion change" }, { &dropletsUAV, "erosion droplets" } };
		d3d.beginPass( pass );
		setParameters();
		const uint32_t dropletGroups = ( count + dropletGroup - 1 ) / dropletGroup;
		d3d.setUAV( 2, dropletsUAV );
		spawnShader.dispatchGroups( dropletGroups, 1, 1 );
		for( uint32_t step = 0; step < lifetime; ++step )
		{
			// Привязка UAV после dispatch — барьер UAV → UAV: следующий проход видит записанное предыдущим
			d3d.setUAV( 0, height.uav );
			d3d.setUAV( 1, delta.uav );
			d3d.setUAV( 2, dropletsUAV );
			dropletShader.dispatchGroups( dropletGroups, 1, 1 );
			d3d.setUAV( 0, height.uav );
			d3d.setUAV( 1, delta.uav );
			applyShader.dispatchGroups( cellGroups, cellGroups, 1 );
		}
		// Пакет — в очередь и ждать: длинный список команд без ожидания GPU мог бы сработать как зависание (TDR)
		d3d.waitForGpu();
	}
	std::vector<float> eroded;
	if( !readTexture( height.texture, padded, eroded ) )
		return false;
	const auto dropletsEnd = std::chrono::steady_clock::now();

	// 2. Осыпание — по карте без продолжения, за краем — крайняя клетка
	std::vector<float> inner( static_cast<size_t>( size ) * size );
	for( uint32_t row = 0; row < size; ++row )
		std::memcpy( &inner[static_cast<size_t>( row ) * size], &eroded[static_cast<size_t>( row + pad ) * padded + pad], size * sizeof( float ) );
	GpuTexture heightA, heightB, thermal, talus;
	if( !createTexture( size, DXGI_FORMAT_R32_FLOAT, inner.data(), sizeof( float ), "Erosion thermal height A", heightA ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, nullptr, sizeof( float ), "Erosion thermal height B", heightB ) ||
		!createTexture( size, DXGI_FORMAT_R32G32_FLOAT, nullptr, 2 * sizeof( float ), "Erosion thermal out", thermal ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, nullptr, sizeof( float ), "Erosion talus", talus ) )
		return false;
	const uint32_t groups = ( size + cellGroup - 1 ) / cellGroup;
	GpuTexture* current = &heightA;
	GpuTexture* next = &heightB;
	const uint32_t iterations = static_cast<uint32_t>( std::max( settings.thermalIterations, 0 ) );
	PassDesc pass;
	pass.name = "Terrain erosion thermal";
	pass.writes = { { &heightA.uav, "erosion height A" }, { &heightB.uav, "erosion height B" }, { &thermal.uav, "erosion thermal" },
					{ &talus.uav, "erosion talus" } };
	d3d.beginPass( pass );
	setParameters();
	for( uint32_t i = 0; i < iterations; ++i )
	{
		d3d.setUAV( 0, current->uav );
		d3d.setUAV( 3, thermal.uav );
		thermalOutShader.dispatchGroups( groups, groups, 1 );
		d3d.setUAV( 0, current->uav );
		d3d.setUAV( 3, thermal.uav );
		d3d.setUAV( 4, next->uav );
		d3d.setUAV( 5, talus.uav );
		thermalInShader.dispatchGroups( groups, groups, 1 );
		std::swap( current, next );
	}
	d3d.waitForGpu();
	if( !readTexture( current->texture, size, result.height ) || !readTexture( talus.texture, size, result.talus ) )
		return false;

	// Размыв и отложения — итоговое изменение против исходной карты, а не сумма за все шаги
	result.wear.resize( result.height.size() );
	result.deposition.resize( result.height.size() );
	for( size_t i = 0; i < result.height.size(); ++i )
	{
		const float change = result.height[i] - heights[i];
		result.wear[i] = std::max( -change, 0.0f );
		result.deposition[i] = std::max( change, 0.0f );
	}
	const auto end = std::chrono::steady_clock::now();
	char text[200];
	std::snprintf( text, sizeof( text ), "Terrain erosion %ux%u: %d droplets, ms: %.0f (droplets %.0f, thermal %.0f)", size, size,
				   settings.droplets, std::chrono::duration<double, std::milli>( end - start ).count(),
				   std::chrono::duration<double, std::milli>( dropletsEnd - start ).count(),
				   std::chrono::duration<double, std::milli>( end - dropletsEnd ).count() );
	LOG( text );
	return true;
}

}
