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
	std::uniform_real_distribution<> vel_x( 0.0000, 0.0006 );
	std::uniform_real_distribution<> vel_y( 0.0001, 0.0004 );
	std::uniform_real_distribution<> vel_z( 0.0000, 0.0006 );

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
				data[counter].position.x = static_cast<float>( x + urd( gen ) ) * 1.0;
				data[counter].position.z = static_cast<float>( y + urd( gen ) ) * 1.0;
				data[counter].position.y = static_cast<float>( urd( gen ) ) + 3000.0;

				data[counter].velocity = XMFLOAT3( (vel_x( gen )-0.0003) * 0.5, vel_y(gen) * 0.3, (vel_z( gen ) - 0.0003) * 0.5 );
				//data[counter].velocity = XMFLOAT3( 0.0, 0.0, 0.0 );
				counter++;
			}	
		}
	}
	

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	buffer_desc.ByteWidth = sizeof( ParticleData ) * m_max_count;	
	buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.StructureByteStride = sizeof( ParticleData );	

	D3D11_SUBRESOURCE_DATA sub_data;
	memset( &sub_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );
	sub_data.pSysMem = data;
	
	if( !DMD3D::instance().CreateBuffer( &buffer_desc, &sub_data, m_structuredBuffer ) )
		return false;



	D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
	memset( &srv_desc, 0, sizeof( D3D11_SHADER_RESOURCE_VIEW_DESC ) );
	srv_desc.Format = DXGI_FORMAT_UNKNOWN;
	srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srv_desc.Buffer.FirstElement = 0;
	srv_desc.Buffer.NumElements = m_max_count;

	
	if( !DMD3D::instance().createSRV( m_structuredBuffer, srv_desc, m_srvParticles ) )
		return false;

	

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	memset( &uavDesc, 0, sizeof( uavDesc ) );
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	//desc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
	uavDesc.Buffer.NumElements = m_max_count;

	if( !DMD3D::instance().createUAV( m_structuredBuffer, uavDesc, m_uavParticles ) )
		return false;


	m_computeShader.Initialize( "Shaders\\particle.cs", "main" );

	return true;
}

void DMParticleSystem::update( float elapsedTime )
{
	m_computeShader.setUAVBuffer( 0, m_uavParticles.get() );

	m_computeShader.Dispatch( m_max_count, elapsedTime );
}

void DMParticleSystem::Render()
{	
	ID3D11ShaderResourceView* srv = m_srvParticles.get();
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
	ID3D11Buffer* buffers[] = { m_structuredBuffer.get() };
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

XMMATRIX* DMParticleSystem::resultMatrix()
{
	m_world_matrix = XMMatrixIdentity();
	return &m_world_matrix;
}