#include "DMParticleSystem.h"
#include <random>
#include "System.h"


DMParticleSystem::DMParticleSystem() :
	SceneObject( "Particles" )
{
}


DMParticleSystem::~DMParticleSystem()
{
}

bool DMParticleSystem::Initialize( unsigned int max_count, unsigned int map_size, const std::string& heightMap,
								   const std::string& material, const std::string& texture )
{
	m_heightMap = heightMap;
	m_material = material;
	m_texture = texture;

	std::mt19937 gen( 0 );

	std::uniform_real_distribution<> urd_x( 0, 100 );
	std::uniform_real_distribution<> urd_y( 0, 100 );
	std::uniform_real_distribution<> urd_z( 0, 100 );
	std::uniform_real_distribution<> vel_x( 0.0000, 0.0006 );
	std::uniform_real_distribution<> vel_y( 0.0003, 0.0005 );
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
				data[counter].position.y = static_cast<float>( urd( gen ) ) + 0.0;

				data[counter].velocity = XMFLOAT3( (vel_x( gen )-0.0003) * 0.5, vel_y(gen), (vel_z( gen ) - 0.0003) * 0.5 );
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
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;      // Format must be must be DXGI_FORMAT_UNKNOWN, when creating a View of a Structured Buffer
	uavDesc.Buffer.NumElements = m_max_count;

	if( !DMD3D::instance().createUAV( m_structuredBuffer, uavDesc, m_uavParticles ) )
		return false;


	if( !m_computeShader.Initialize( "Shaders\\particle.cs", "main" ) )
	{
		LOG( "Can`t compile shader" );
		return false;
	}

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ParticleParams ), m_constantBuffer ) )
	{
		LOG( "Can`t create constant buffer" );
		return false;
	}

	m_propertyContainer.setName( "Particles:" );
	auto prop = m_propertyContainer.insert( "Terrain height multiplier", 1.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 1000.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_propertyContainer.insert( "High of death", 1.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 200.0f );
	prop->setControlType( GUIControlType::SLIDER );

	m_initialized = true;

	return true;
}

void DMParticleSystem::update( float elapsedTime )
{
	Device::updateResource<ParticleParams>( m_constantBuffer, [this]( ParticleParams& data ) ->void
	{
		data.heightMultiplier.x = m_propertyContainer["Terrain height multiplier"].data<float>();
		data.highOfDeath.x = m_propertyContainer["High of death"].data<float>();
	} );

	DMD3D::instance().setConstantBuffer( SRVType::cs, 3, m_constantBuffer );

	m_computeShader.setUAVBuffer( 0, m_uavParticles.get() );

	m_computeShader.Dispatch( m_max_count, elapsedTime );
}

void DMParticleSystem::compute( const GS::FrameContext& frame )
{
	if( !m_initialized )
		return;

	// Высота, на которой частица гибнет, отсчитывается от террейна: шейдер читает карту высот из t0
	DMD3D::instance().setSRV( SRVType::cs, 0, GS::System::textures().get( m_heightMap )->srv() );
	update( frame.elapsedTime );
}

void DMParticleSystem::collectMeshes( const GS::RenderView&, GS::MeshCollector& collector )
{
	if( m_initialized && GS::System::materials().exists( m_material ) )
		collector.addCustom( GS::passBit( GS::MeshPass::transparent ) );
}

void DMParticleSystem::renderCustom( const GS::RenderContext& )
{

	GS::DMShader* shader = GS::System::materials().get( m_material )->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( GS::DMShader::by_vertex );

	bindParticles();
	DMD3D::instance().setSRV( SRVType::ps, 0, GS::System::textures().get( m_texture )->srv() );
	shader->render( particleCount(), 0, 0 );
	DMD3D::instance().GetDeviceContext()->GSSetShader( nullptr, nullptr, 0 );
}

PropertyContainer* DMParticleSystem::properties()
{
	return &m_propertyContainer;
}

void DMParticleSystem::bindParticles()
{
	ID3D11ShaderResourceView* srv = m_srvParticles.get();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 5, 1, &srv );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 0, nullptr, 0, 0 );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( nullptr, DXGI_FORMAT_R32_UINT, 0 );
}

unsigned int DMParticleSystem::particleCount()
{
	return  m_max_count;
}
