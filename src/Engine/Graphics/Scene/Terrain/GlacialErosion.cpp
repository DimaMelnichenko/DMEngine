#include "GlacialErosion.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "D3D\DMD3D.h"
#include "ComputeShader.h"
#include "Logger\Logger.h"
#include "Shaders\glacial_erosion_constants.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr uint32_t cellGroup = 8;				// numthreads glacial_erosion.cs
constexpr uint32_t stepsPerSubmit = 2000;		// шагов на одну отправку: длинный список без ожидания GPU — как зависание (TDR)

// Лёд: плотность, кг/м³; ускорение свободного падения, м/с²; секунд в году
constexpr double iceDensity = 910.0;
constexpr double gravity = 9.81;
constexpr double secondsPerYear = 3.156e7;
// Закон Глена для тёплого льда: A = 2,4·10⁻²⁴ Па⁻³·с⁻¹ (Cuffey, Paterson 2010), n = 3; течение D_d = 2A (ρg)³ / 5 · H⁵ |∇s|²
constexpr double glenA = 2.4e-24 * secondsPerYear;
// Скольжение u_s = A_s τ_b² (Weertman, m = 2): при τ_b = 100 кПа — 50 м/год
constexpr double slidingA = 50.0 / ( 1.0e5 * 1.0e5 );
// Предел «диффузии» льда, м²/год: шаг — 0,2 · клетка² / предел (у сетки 128 м — 0,025 года). Течение быстрее — только у
// толстого льда на крутом (ледопады): там скорость упирается в предел
constexpr float diffusionCap = 131072.0f;

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
		LOG( std::string( "Glacial erosion: texture " ) + name + " is not created" );
		return false;
	}
	d3d.setName( result.texture, name );
	if( !data )
		d3d.clearStorageView( result.uav );
	return true;
}

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

bool GlacialErosion::run( const std::vector<float>& bed, uint32_t size, float cell, const XMFLOAT2& origin, float mapSize,
						  const GlacialErosionSettings& settings, const char* name, Result& result )
{
	const auto start = std::chrono::steady_clock::now();
	DMD3D& d3d = DMD3D::instance();
	// Строгий IEEE, как у эрозии карты: результат кэшируется на диске и не зависит от сборки
	ComputeShader fluxShader, updateShader;
	if( !fluxShader.initialize( "Shaders\\glacial_erosion.cs", "mainFlux", true ) ||
		!updateShader.initialize( "Shaders\\glacial_erosion.cs", "mainUpdate", true ) )
		return false;

	GpuTexture bedTexture, sourceTexture, ice, outflow, erosionRate, iceMax;
	if( !createTexture( size, DXGI_FORMAT_R32_FLOAT, bed.data(), sizeof( float ), "Glacier bed", bedTexture ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, bed.data(), sizeof( float ), "Glacier source bed", sourceTexture ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, nullptr, sizeof( float ), "Glacier ice", ice ) ||
		!createTexture( size, DXGI_FORMAT_R32G32B32A32_FLOAT, nullptr, 4 * sizeof( float ), "Glacier outflow", outflow ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, nullptr, sizeof( float ), "Glacier erosion rate", erosionRate ) ||
		!createTexture( size, DXGI_FORMAT_R32_FLOAT, nullptr, sizeof( float ), "Glacier ice max", iceMax ) )
		return false;

	const double rhoG = iceDensity * gravity;
	GlacialErosionBuffer params = {};
	params.size = size;
	params.cell = cell;
	params.diffusionCap = diffusionCap;
	params.timeStep = 0.2f * cell * cell / diffusionCap;
	params.deformation = static_cast<float>( 2.0 * glenA * rhoG * rhoG * rhoG / 5.0 ) * std::max( settings.flow, 0.0f );
	params.sliding = static_cast<float>( slidingA * rhoG * rhoG ) * std::max( settings.sliding, 0.0f );
	params.ela = settings.ela;
	params.balanceGradient = std::max( settings.balanceGradient, 0.0f ) * 0.001f;
	params.maxAccumulation = std::max( settings.maxAccumulation, 0.0f );
	params.erosion = std::max( settings.erosion, 0.0f );
	params.maxErosion = std::max( settings.maxErosion, 0.0f );
	params.origin = origin;
	params.mapSize = mapSize;
	params.mapFade = XMFLOAT2( settings.mapFade.from, std::max( settings.mapFade.to, settings.mapFade.from + 1.0f ) );
	Buffer constants;
	if( !d3d.createShaderConstantBuffer( sizeof( GlacialErosionBuffer ), constants ) )
		return false;

	const uint32_t iceSteps = static_cast<uint32_t>( std::max( settings.iceYears, 0.0f ) / params.timeStep );
	const uint32_t erosionSteps = static_cast<uint32_t>( std::max( settings.erosionYears, 0.0f ) / params.timeStep );
	const uint32_t steps = iceSteps + erosionSteps;
	const uint32_t groups = ( size + cellGroup - 1 ) / cellGroup;
	for( uint32_t first = 0; first < steps; first += stepsPerSubmit )
	{
		PassDesc pass;
		pass.name = "Glacial erosion";
		pass.writes = { { &bedTexture.uav, "glacier bed" }, { &ice.uav, "glacier ice" }, { &outflow.uav, "glacier outflow" },
						{ &erosionRate.uav, "glacier erosion rate" }, { &iceMax.uav, "glacier ice max" },
						{ &sourceTexture.uav, "glacier source bed" } };
		d3d.beginPass( pass );
		const uint32_t last = std::min( first + stepsPerSubmit, steps );
		// Константы — на каждый отрезок шагов одного этапа: этап меняется на границе iceSteps
		uint32_t step = first;
		while( step < last )
		{
			const bool erode = step >= iceSteps;
			const uint32_t stageEnd = erode ? last : std::min( last, iceSteps );
			params.erode = erode ? 1 : 0;
			Device::updateResourceData( constants, params );
			d3d.setConstantBuffer( 4, constants );
			for( ; step < stageEnd; ++step )
			{
				// Привязка UAV после dispatch — барьер UAV → UAV: следующий проход видит записанное предыдущим
				d3d.setUAV( 0, bedTexture.uav );
				d3d.setUAV( 1, ice.uav );
				d3d.setUAV( 2, outflow.uav );
				d3d.setUAV( 3, erosionRate.uav );
				fluxShader.dispatchGroups( groups, groups, 1 );
				d3d.setUAV( 0, bedTexture.uav );
				d3d.setUAV( 1, ice.uav );
				d3d.setUAV( 2, outflow.uav );
				d3d.setUAV( 3, erosionRate.uav );
				d3d.setUAV( 4, iceMax.uav );
				d3d.setUAV( 5, sourceTexture.uav );
				updateShader.dispatchGroups( groups, groups, 1 );
			}
		}
		d3d.waitForGpu();
	}
	if( !readTexture( bedTexture.texture, size, result.bed ) || !readTexture( ice.texture, size, result.ice ) ||
		!readTexture( iceMax.texture, size, result.iceMax ) )
		return false;

	float deepest = 0.0f;
	float thickest = 0.0f;
	size_t glaciated = 0;
	for( size_t i = 0; i < result.bed.size(); ++i )
	{
		deepest = std::max( deepest, bed[i] - result.bed[i] );
		thickest = std::max( thickest, result.iceMax[i] );
		glaciated += result.iceMax[i] > 10.0f ? 1 : 0;
	}
	char text[256];
	std::snprintf( text, sizeof( text ), "%s %ux%u, cell %.0f m, ELA %.0f m: %u steps of %.3f yr, ice cover %.1f%%, thickest %.0f m, "
				   "deepest erosion %.0f m, ms: %.0f", name, size, size, cell, settings.ela, steps, params.timeStep,
				   100.0 * static_cast<double>( glaciated ) / static_cast<double>( result.bed.size() ), thickest, deepest,
				   std::chrono::duration<double, std::milli>( std::chrono::steady_clock::now() - start ).count() );
	LOG( text );
	return true;
}

}
