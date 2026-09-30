#include "ConstantRing.h"
#include "Logger\Logger.h"

bool ConstantRing::initialize( ID3D11Device* device, ID3D11DeviceContext1* context, uint32_t bytes )
{
	m_device = device;
	m_context = context;

	// Больше 64 КБ: на уровне 11.1 размер константного буфера не ограничен, ограничено окно привязки (4096 констант)
	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = ( bytes + alignment - 1 ) / alignment * alignment;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	ID3D11Buffer* buffer = nullptr;
	if( FAILED( device->CreateBuffer( &desc, nullptr, &buffer ) ) )
	{
		LOG( "Constant ring: can`t create a " + std::to_string( desc.ByteWidth ) + "-byte constant buffer" );
		return false;
	}
	m_buffer = make_com_ptr<ID3D11Buffer>( buffer );
	m_capacity = desc.ByteWidth;
	m_current.capacity = m_last.capacity = m_capacity;
	m_head = 0;
	m_discardNext = true;
	return true;
}

void ConstantRing::beginFrame()
{
	m_last = m_current;
	m_current = {};
	m_current.capacity = m_capacity;
	m_discardNext = true;
}

void* ConstantRing::beginWrite( uint32_t size, uint32_t& offset, uint32_t& bytes )
{
	bytes = std::max<uint32_t>( ( size + alignment - 1 ) / alignment * alignment, alignment );
	if( bytes > m_capacity )
	{
		LOG( "Constant ring: a " + std::to_string( size ) + "-byte block does not fit into the ring" );
		bytes = m_capacity;
	}

	// Свободного места до конца нет — начинаем с нуля с DISCARD: прошлые участки кадра GPU дочитает из старой памяти
	bool discard = m_discardNext;
	if( !discard && m_head + bytes > m_capacity )
	{
		discard = true;
		++m_current.frameWraps;
	}
	if( discard )
		m_head = 0;
	m_discardNext = false;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if( FAILED( m_context->Map( m_buffer.get(), 0, discard ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &mapped ) ) )
	{
		LOG( "Constant ring: Map failed" );
		return nullptr;
	}
	m_mapped = true;
	offset = m_head;
	m_head += bytes;
	m_current.frameBytes += bytes;
	++m_current.frameWrites;
	return static_cast<char*>( mapped.pData ) + offset;
}

void ConstantRing::finishWrite()
{
	if( !m_mapped )
		return;
	m_context->Unmap( m_buffer.get(), 0 );
	m_mapped = false;
}

void ConstantRing::bind( SRVType stage, uint16_t slot, uint32_t offset, uint32_t bytes ) const
{
	// Смещение и размер — в константах по 16 байт, кратные 16 константам (256 байт)
	ID3D11Buffer* buffer = m_buffer.get();
	const UINT first = offset / 16;
	const UINT count = bytes / 16;
	switch( stage )
	{
		case SRVType::vs:
			m_context->VSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		case SRVType::ps:
			m_context->PSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		case SRVType::gs:
			m_context->GSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		case SRVType::hs:
			m_context->HSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		case SRVType::ds:
			m_context->DSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		case SRVType::cs:
			m_context->CSSetConstantBuffers1( slot, 1, &buffer, &first, &count );
			break;
		default:
			break;
	}
}
