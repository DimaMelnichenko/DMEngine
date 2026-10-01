#include "DMStructuredBuffer.h"
#include <stdexcept>
#include <algorithm>
#include <cstring>

DMStructuredBuffer::DMStructuredBuffer( )
{
}

DMStructuredBuffer::~DMStructuredBuffer()
{
}

void DMStructuredBuffer::createBuffer( size_t sizeOfElement, size_t countElements, const char* name )
{
	m_sizeOfElement = sizeOfElement;
	m_countElements = countElements;

	BufferDesc desc;
	desc.size = static_cast<uint32_t>( m_countElements * m_sizeOfElement );
	desc.stride = static_cast<uint32_t>( m_sizeOfElement );
	desc.usage = BufferUsage::shaderResource | BufferUsage::structured | BufferUsage::cpuWrite;
	if( !DMD3D::instance().createBuffer( desc, nullptr, m_buffer ) )
		throw std::logic_error( "DMStructuredBuffer can`t create buffer" );
	(void)name;	// своего ресурса у буфера кольца нет — имя носит кольцо
}

void DMStructuredBuffer::updateData( const void* data, size_t sizeInByte )
{
	sizeInByte = std::min( sizeInByte, m_sizeOfElement * m_countElements );

	// Данные кадра — в участок upload-кольца (DMD3D::beginWrite); endWrite даёт участку временный SRV этого кадра
	if( void* mapped = DMD3D::instance().beginWrite( m_buffer, static_cast<uint32_t>( sizeInByte ) ) )
		std::memcpy( mapped, data, sizeInByte );
	DMD3D::instance().endWrite();
}

void DMStructuredBuffer::setToSlot( int8_t slot )
{
	DMD3D::instance().setSRV( slot, m_buffer );
}

uint32_t DMStructuredBuffer::sizeofElement() const
{
	return static_cast<uint32_t>( m_sizeOfElement );
}

uint32_t DMStructuredBuffer::numElements() const
{
	return static_cast<uint32_t>( m_countElements );
}
