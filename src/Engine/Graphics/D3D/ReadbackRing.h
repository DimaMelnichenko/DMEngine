#pragma once

#include <cstdint>
#include <vector>
#include "GpuResources.h"
#include "DMD3D.h"

// Чтение значения с GPU на CPU без ожидания: кольцо из count копий (BufferUsage::readback). Каждый кадр push копирует
// источник в очередной буфер, latest отдаёт самую свежую из прошлых копий, до которой GPU уже дошёл (DMD3D::readBuffer
// без ожидания; в D3D12 — по значению fence копии). GPU отстаёт от CPU на несколько кадров, поэтому готовы только
// старые копии, а значение приходит с задержкой в 2–3 кадра. Экспозиция (PostProcess), позже — GpuProfiler
template<class T>
class ReadbackRing
{
public:
	bool create( uint32_t count )
	{
		BufferDesc desc;
		desc.size = sizeof( T );
		desc.usage = BufferUsage::readback;
		m_buffers.resize( count );
		for( Buffer& buffer : m_buffers )
		{
			if( !DMD3D::instance().createBuffer( desc, nullptr, buffer ) )
				return false;
		}
		m_pushed = 0;
		return true;
	}

	// Копия source (размером sizeof( T )) в следующий буфер кольца — раз за кадр
	void push( const Buffer& source )
	{
		if( m_buffers.empty() )
			return;
		DMD3D::instance().copyBuffer( m_buffers[m_pushed % m_buffers.size()], source );
		++m_pushed;
	}

	// Самая свежая готовая копия из прошлых (последняя, push этого кадра, готовой не бывает); false — ни одна не готова
	// или кольцо ещё не заполнено
	bool latest( T& value ) const
	{
		const uint32_t count = static_cast<uint32_t>( m_buffers.size() );
		if( m_pushed < count )
			return false;
		for( uint32_t age = 1; age < count; ++age )
		{
			if( DMD3D::instance().readBuffer( m_buffers[( m_pushed - 1 - age ) % count], &value, sizeof( T ) ) )
				return true;
		}
		return false;
	}

private:
	std::vector<Buffer> m_buffers;
	uint32_t m_pushed = 0;
};
