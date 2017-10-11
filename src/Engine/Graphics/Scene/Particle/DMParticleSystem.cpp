#include "DMParticleSystem.h"
#include <random>


DMParticleSystem::DMParticleSystem()
{
}


DMParticleSystem::~DMParticleSystem()
{
}

bool DMParticleSystem::Initialize( unsigned int max_count, unsigned int map_size )
{
	std::mt19937 gen( 0 );

	std::uniform_real_distribution<> urd_x( 0, 100 );
	std::uniform_real_distribution<> urd_y( 0, 100 );
	std::uniform_real_distribution<> urd_z( 0, 100 );

	m_max_count = max_count;

	ParticleData* data = nullptr;
	
	unsigned int counter = 0;

	unsigned int width = map_size - 1;
	unsigned int height = map_size - 1;

	m_max_count = max_count * width * height;

	data = new ParticleData[m_max_count];

	std::uniform_real_distribution<> urd( 0, 1 );

	for( size_t x = 1; x < width; x++ )
	{
		for( size_t y = 1; y < height; y++ )
		{
			for( size_t i = 0; i < max_count; i++ )
			{
				data[counter].position.x = static_cast<float>( x + urd( gen ) );
				data[counter].position.z = static_cast<float>( y + urd( gen ) );
				data[counter].position.y = 0.0f;
				data[counter].velocity = D3DXVECTOR4( 1.0, 0.0, 0.0, 0.0 );
				counter++;
			}	
		}
	}
	

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.ByteWidth = sizeof( ParticleData ) * m_max_count;	
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.StructureByteStride = sizeof( ParticleData );	

	D3D11_SUBRESOURCE_DATA sub_data;
	memset( &sub_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );
	sub_data.pSysMem = data;
	
	ID3D11Buffer* buffer;
	HRESULT hr = DMD3D::instance().GetDevice()->CreateBuffer( &buffer_desc, &sub_data, &buffer );
	if( FAILED( hr ) )
	{		
		return false;
	}
	
	m_structured_buffer_0 = make_com_ptr<ID3D11Buffer>( buffer );

	hr = DMD3D::instance().GetDevice()->CreateBuffer( &buffer_desc, &sub_data, &buffer );
	if( FAILED( hr ) )
	{
		return false;
	}

	m_structured_buffer_1 = make_com_ptr<ID3D11Buffer>( buffer );

	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
	memset( &srv_desc, 0, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );
	srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = m_max_count;

	ID3D11ShaderResourceView* srv;
	hr = DMD3D::instance().GetDevice()->CreateShaderResourceView( m_structured_buffer_0.get(), &srv_desc, &srv );

	if( FAILED( hr ) )
	{
		return false;
	}

	m_SRV_0 = make_com_ptr<ID3D11ShaderResourceView>( srv );

	hr = DMD3D::instance().GetDevice()->CreateShaderResourceView( m_structured_buffer_1.get(), &srv_desc, &srv );

	if( FAILED( hr ) )
	{
		return false;
	}

	m_SRV_1 = make_com_ptr<ID3D11ShaderResourceView>( srv );


	createBufferUAV();


	return true;
}

void DMParticleSystem::update( std::unique_ptr<DMComputeShader>& shader, float elapsed_time )
{
	ID3D11ShaderResourceView* srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	DMD3D::instance().GetDeviceContext()->CSSetShaderResources( 0, 10, srv );

	shader->setStructuredBuffer( 0, m_SRV_0.get() );
	shader->setUAVBuffer( 0, m_UAV_1.get() );

	shader->Dispatch( m_max_count, elapsed_time );

	std::swap( m_structured_buffer_0, m_structured_buffer_1 );
	std::swap( m_SRV_0, m_SRV_1 );
	std::swap( m_UAV_0, m_UAV_1 );
}

void DMParticleSystem::Render()
{	
	ID3D11ShaderResourceView* srv = m_SRV_0.get();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 5, 1, &srv );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 0, NULL, 0, 0 );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( NULL, DXGI_FORMAT_R32_UINT, 0 );
}

void DMParticleSystem::generate()
{
	//ID3D11ShaderResourceView* srv = m_SRV_0.get();
	//DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 5, 1, &srv );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 0, NULL, 0, 0 );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( NULL, DXGI_FORMAT_R32_UINT, 0 );

	unsigned int offset = 0;
	ID3D11Buffer* buffers[] = { m_structured_buffer_0.get() };
	DMD3D::instance().GetDeviceContext()->SOSetTargets( 1, buffers, &offset );
}

void DMParticleSystem::after_generate()
{
	unsigned int offset = 0;
	ID3D11Buffer* buffers[] = { nullptr };
	DMD3D::instance().GetDeviceContext()->SOSetTargets( 1, buffers, &offset );
}

unsigned int DMParticleSystem::particleCount()
{
	return  m_max_count;
}

D3DXMATRIX* DMParticleSystem::resultMatrix()
{
	D3DXMatrixIdentity( &m_world_matrix );
	return  &m_world_matrix;
}

HRESULT DMParticleSystem::createBufferUAV()
{
	D3D11_BUFFER_DESC descBuf;
	ZeroMemory( &descBuf, sizeof( descBuf ) );
	m_structured_buffer_0->GetDesc( &descBuf );

	D3D11_UNORDERED_ACCESS_VIEW_DESC desc;
	ZeroMemory( &desc, sizeof( desc ) );
	desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	desc.Buffer.FirstElement = 0;
	//desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	
	desc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
	desc.Buffer.NumElements = descBuf.ByteWidth / descBuf.StructureByteStride;
		

	ID3D11UnorderedAccessView* uav;
	HRESULT hr = DMD3D::instance().GetDevice()->CreateUnorderedAccessView( m_structured_buffer_0.get(), &desc, &uav );

	if( SUCCEEDED( hr ) )
	{
		m_UAV_0 = make_com_ptr<ID3D11UnorderedAccessView>( uav );
	}

	hr = DMD3D::instance().GetDevice()->CreateUnorderedAccessView( m_structured_buffer_1.get(), &desc, &uav );

	if( SUCCEEDED( hr ) )
	{
		m_UAV_1 = make_com_ptr<ID3D11UnorderedAccessView>( uav );
	}

	return hr;
}