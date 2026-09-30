#include "DMStructuredBuffer.h"
#include <stdexcept>

DMStructuredBuffer::DMStructuredBuffer( )
{
}

DMStructuredBuffer::~DMStructuredBuffer()
{
}

void DMStructuredBuffer::createBuffer( size_t sizeOfElement, size_t countElements )
{
	m_sizeOfElement = sizeOfElement;
	m_countElements = countElements;

	BufferDesc desc;
	desc.size = static_cast<uint32_t>( m_countElements * m_sizeOfElement );
	desc.stride = static_cast<uint32_t>( m_sizeOfElement );
	desc.usage = BufferUsage::shaderResource | BufferUsage::structured | BufferUsage::cpuWrite;
	if( !DMD3D::instance().createBuffer( desc, nullptr, m_buffer ) )
		throw std::logic_error( "DMStructuredBuffer can`t create buffer" );

	if( !DMD3D::instance().createShaderView( m_buffer, {}, m_view ) )
		throw std::logic_error( "DMStructuredBuffer can`t create ShaderResourceView" );
}

void DMStructuredBuffer::updateData( const void* data, size_t sizeInByte )
{
	sizeInByte = std::min( sizeInByte, m_sizeOfElement * m_countElements );

	D3D11_MAPPED_SUBRESOURCE mappedData = {};
	if( FAILED( DMD3D::instance().GetDeviceContext()->Map( m_buffer.handle(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData ) ) )
		return;

	std::memcpy( mappedData.pData, data, sizeInByte );

	DMD3D::instance().GetDeviceContext()->Unmap( m_buffer.handle(), 0 );
}

void DMStructuredBuffer::setToSlot( int8_t slot, SRVType type )
{
	DMD3D::instance().setSRV( type, slot, m_view );
}

uint32_t DMStructuredBuffer::sizeofElement() const
{
	return static_cast<uint32_t>( m_sizeOfElement );
}

uint32_t DMStructuredBuffer::numElements() const
{
	return static_cast<uint32_t>( m_countElements );
}
