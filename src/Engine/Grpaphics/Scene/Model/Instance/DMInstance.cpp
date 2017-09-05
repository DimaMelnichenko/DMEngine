#include "DMInstance.h"


DMInstance::DMInstance( DMD3D* dmd3d ) : DM3DObject( dmd3d )
{

}

DMInstance::~DMInstance()
{

}

DMModel* DMInstance::model()
{
	return m_model.get();
}

bool DMInstance::initialize( std::unique_ptr<DMModel>& model, INSTANCE_TYPE type, unsigned int max_count )
{
	m_model = std::move( model );

	m_max_count = max_count;
	m_current_count = 0;


	D3D11_BUFFER_DESC buffer_desc;
	buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.ByteWidth = m_max_count * sizeof( InstanceDataType );
	buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.StructureByteStride = sizeof( InstanceDataType );
	buffer_desc.Usage = D3D11_USAGE_DYNAMIC;

	ID3D11Buffer* buffer;
	if( FAILED( m_dmd3d->GetDevice()->CreateBuffer( &buffer_desc, nullptr, &buffer ) ) )
	{
		return false;
	}
	
	m_instance_buffer = make_com_ptr<ID3D11Buffer>( buffer );


	D3D11_SHADER_RESOURCE_VIEW_DESC view_desc;
	std::memset( &view_desc, 0, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );

	view_desc.Format = DXGI_FORMAT_UNKNOWN;
	view_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	//view_desc.Buffer.ElementOffset = sizeof( InstanceDataType );
	view_desc.Buffer.ElementWidth = m_max_count;

	ID3D11ShaderResourceView* view;
	if( FAILED( m_dmd3d->GetDevice()->CreateShaderResourceView( m_instance_buffer.get(), &view_desc, &view ) ) )
	{
		return false;
	}

	m_instance_view = make_com_ptr<ID3D11ShaderResourceView>( view );

}

unsigned int DMInstance::count()
{
	return m_current_count;
}

void DMInstance::set_data( const std::vector<DMInstance::InstanceDataType>& new_data )
{
	D3D11_MAPPED_SUBRESOURCE mappedData;
	std::memset( &mappedData, 0, sizeof( mappedData ) );

	if( new_data.size() > m_max_count )
	{
		m_current_count = 0;
		return;
	}

	m_current_count = new_data.size();

	if( FAILED( m_dmd3d->GetDeviceContext()->Map( m_instance_buffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedData ) ) )
	{
		//handleError( ... ); // insert error handling here
		return;
	}

	InstanceDataType* dataPtr = reinterpret_cast<InstanceDataType*>( mappedData.pData );
	
	std::memcpy( dataPtr, &new_data[0], sizeof( InstanceDataType ) * new_data.size() );

	m_dmd3d->GetDeviceContext()->Unmap( m_instance_buffer.get(), 0 );
}

void DMInstance::set_to_draw()
{
	ID3D11ShaderResourceView* view = m_instance_view.get();

	m_dmd3d->GetDeviceContext()->VSSetShaderResources( 16, 1, &view );
}