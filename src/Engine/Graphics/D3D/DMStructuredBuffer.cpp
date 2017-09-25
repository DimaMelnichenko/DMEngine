#include "DMStructuredBuffer.h"



DMStructuredBuffer::DMStructuredBuffer( DMD3D* dmd3d ) : DM3DObject( dmd3d )
{
}


DMStructuredBuffer::~DMStructuredBuffer()
{
}

void DMStructuredBuffer::CreateBuffer( size_t sizeOfElement, size_t countElements )
{
	m_sizeOfElement = sizeOfElement;
	m_countElements = countElements;

	D3D11_BUFFER_DESC buffer_desc;
	buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.ByteWidth = m_countElements * m_sizeOfElement;
	buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.StructureByteStride = m_sizeOfElement;
	buffer_desc.Usage = D3D11_USAGE_DYNAMIC;

	ID3D11Buffer* buffer;
	if( FAILED( m_dmd3d->GetDevice()->CreateBuffer( &buffer_desc, nullptr, &buffer ) ) )
	{
		throw std::logic_error( "DMStructuredBuffer can`t create buffer" );
	}

	m_buffer = make_com_ptr<ID3D11Buffer>( buffer );


	D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
	std::memset( &view_desc, 0, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );

	view_desc.Format = DXGI_FORMAT_UNKNOWN;
	view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	view_desc.Buffer.ElementWidth = m_countElements;

	ID3D11ShaderResourceView* view;
	if( FAILED( m_dmd3d->GetDevice()->CreateShaderResourceView( m_buffer.get(), &view_desc, &view ) ) )
	{
		throw std::logic_error( "DMStructuredBuffer can`t create ShaderResourceView" );
	}

	m_SRV = make_com_ptr<ID3D11ShaderResourceView>( view );
}

void DMStructuredBuffer::UpdateData( void* data, size_t sizeInByte )
{
	D3D11_MAPPED_SUBRESOURCE mappedData;
	std::memset( &mappedData, 0, sizeof( mappedData ) );

	if( sizeInByte > ( m_sizeOfElement * m_countElements ) )
	{
		sizeInByte = m_sizeOfElement * m_countElements;;
	}
	if( FAILED( m_dmd3d->GetDeviceContext()->Map( m_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData ) ) )
	{
		//handleError( ... ); // insert error handling here
		return;
	}

	std::memcpy( mappedData.pData, data, sizeInByte );

	m_dmd3d->GetDeviceContext()->Unmap( m_buffer.get(), 0 );
}

void DMStructuredBuffer::setToSlot( int8_t slot, SRVType type )
{
	ID3D11ShaderResourceView* view = m_SRV.get();

	switch( type )
	{
		case vs:
			m_dmd3d->GetDeviceContext()->VSSetShaderResources( slot, 1, &view );
			break;
		case ps:
			m_dmd3d->GetDeviceContext()->PSSetShaderResources( slot, 1, &view );
			break;
		case gs:
			m_dmd3d->GetDeviceContext()->GSSetShaderResources( slot, 1, &view );
			break;
		case cs:
			m_dmd3d->GetDeviceContext()->CSSetShaderResources( slot, 1, &view );
			break;
		default:
			break;
	}

	
}
