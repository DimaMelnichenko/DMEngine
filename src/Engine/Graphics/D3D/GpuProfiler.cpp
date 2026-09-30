#include "GpuProfiler.h"
#include "DMD3D.h"
#include "Logger\Logger.h"
#include <algorithm>

bool GpuProfiler::initialize()
{
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTimestampQueries( queriesPerFrame * DMD3D::frameCount ) )
		return false;

	// Буфер под разрешённые запросы кадра (default-куча — ResolveQueryData пишет только в неё) и кольцо копий на CPU
	BufferDesc desc;
	desc.size = sizeof( Timestamps );
	for( uint32_t i = 0; i < DMD3D::frameCount; ++i )
	{
		if( !d3d.createBuffer( desc, nullptr, m_resolved[i] ) )
			return false;
		d3d.setName( m_resolved[i], "GPU profiler timestamps " + std::to_string( i ) );
	}
	if( !m_readback.create( readbackCount ) )
		return false;
	LOG( "GPU profiler: timestamp frequency " + std::to_string( d3d.timestampFrequency() / 1000000 ) + " MHz" );
	return true;
}

void GpuProfiler::beginFrame()
{
	m_frameScopes.clear();
	m_openScopes.clear();
	m_frameOpen = true;
	DMD3D::instance().writeTimestamp( queryBase() );
}

void GpuProfiler::beginScope( const std::string& name )
{
	if( !m_frameOpen )
		return;
	DMD3D::instance().beginEvent( name.c_str() );
	if( m_frameScopes.size() >= maxScopes )
	{
		m_openScopes.push_back( maxScopes );	// область без замера: метка PIX есть, времени нет
		return;
	}
	const uint32_t index = static_cast<uint32_t>( m_frameScopes.size() );
	m_frameScopes.push_back( name );
	m_openScopes.push_back( index );
	DMD3D::instance().writeTimestamp( queryBase() + 2 + 2 * index );
}

void GpuProfiler::endScope()
{
	if( !m_frameOpen || m_openScopes.empty() )
		return;
	const uint32_t index = m_openScopes.back();
	m_openScopes.pop_back();
	if( index < maxScopes )
		DMD3D::instance().writeTimestamp( queryBase() + 3 + 2 * index );
	DMD3D::instance().endEvent();
}

void GpuProfiler::endFrame()
{
	if( !m_frameOpen )
		return;
	m_frameOpen = false;
	while( !m_openScopes.empty() )
		endScope();

	// Запросы кадра — в буфер, буфер — в кольцо; имена областей запоминаются под тем же номером кадра
	DMD3D& d3d = DMD3D::instance();
	d3d.writeTimestamp( queryBase() + 1 );
	Buffer& resolved = m_resolved[m_frame % DMD3D::frameCount];
	// Только записанные запросы: разрешение незаписанного — ошибка debug-слоя
	d3d.resolveTimestamps( queryBase(), 2 + 2 * static_cast<uint32_t>( m_frameScopes.size() ), resolved );
	m_readback.push( resolved );
	m_pushedScopes[m_frame % readbackCount] = m_frameScopes;
	readResults();
	++m_frame;
}

void GpuProfiler::readResults()
{
	// Самая свежая копия, до которой GPU дошёл, — кадр age назад; её области — под тем же номером
	Timestamps timestamps;
	uint32_t age = 0;
	if( !m_readback.latest( timestamps, age ) || age > m_frame )
		return;
	const std::vector<std::string>& scopes = m_pushedScopes[( m_frame - age ) % readbackCount];
	const double toMilliseconds = 1000.0 / static_cast<double>( std::max<uint64_t>( DMD3D::instance().timestampFrequency(), 1 ) );
	const auto span = [&]( uint32_t begin, uint32_t end )
	{
		return timestamps.values[end] >= timestamps.values[begin] ?
			   static_cast<float>( ( timestamps.values[end] - timestamps.values[begin] ) * toMilliseconds ) : 0.0f;
	};
	m_frameMilliseconds = span( 0, 1 );
	m_results.clear();
	for( uint32_t i = 0; i < scopes.size() && i < maxScopes; ++i )
		m_results.emplace_back( scopes[i], span( 2 + 2 * i, 3 + 2 * i ) );
}
