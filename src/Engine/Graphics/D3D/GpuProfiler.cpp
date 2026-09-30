#include "GpuProfiler.h"

bool GpuProfiler::initialize()
{
	return true;
}

void GpuProfiler::beginFrame()
{
	m_frameScopes.clear();
	m_frameOpen = true;
}

void GpuProfiler::endFrame()
{
	if( !m_frameOpen )
		return;
	// До вехи M5 запросов нет: области кадра в том же порядке, времена — нули
	m_results = m_frameScopes;
	m_frameMilliseconds = 0.0f;
	m_frameOpen = false;
}

void GpuProfiler::beginScope( const std::string& name )
{
	if( m_frameOpen && m_frameScopes.size() < maxScopes )
		m_frameScopes.emplace_back( name, 0.0f );
}

void GpuProfiler::endScope()
{
}
