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
	

	// Частицы — структурный буфер: пишет compute, читает вершинный шейдер
	BufferDesc desc;
	desc.size = sizeof( ParticleData ) * m_max_count;
	desc.stride = sizeof( ParticleData );
	desc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createBuffer( desc, data, m_structuredBuffer ) ||
		!d3d.createShaderView( m_structuredBuffer, {}, m_srvParticles ) ||
		!d3d.createStorageView( m_structuredBuffer, {}, m_uavParticles ) )
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

	DMD3D::instance().setConstantBuffer( 3, m_constantBuffer );

	m_computeShader.setUAVBuffer( 0, m_uavParticles );

	m_computeShader.Dispatch( m_max_count, elapsedTime );
}

void DMParticleSystem::compute( const GS::FrameContext& frame )
{
	if( !m_initialized )
		return;

	// Compute-проход: буфер частиц пишется (beginPass снимает его с входа вершинного шейдера прошлого кадра); высота,
	// на которой частица гибнет, отсчитывается от террейна: шейдер читает карту высот из t0
	const ShaderView& heightMap = GS::System::textures().get( m_heightMap )->srv();
	DMD3D::instance().beginPass( PassDesc{ "Particles update", {}, {}, 0, 0, { { &heightMap, "height map" } }, { { &m_uavParticles, "particles" } } } );
	DMD3D::instance().setSRV( 0, heightMap );
	update( frame.elapsedTime );
}

void DMParticleSystem::collectMeshes( const GS::RenderView&, GS::MeshCollector& collector )
{
	if( m_initialized && GS::System::materials().exists( m_material ) )
		collector.addCustom( GS::passBit( GS::MeshPass::transparent ) );
}

void DMParticleSystem::renderCustom( const GS::RenderContext& )
{

	// Точки по SV_VertexID: топология точек — у материала частиц (ParticleMaterial), часть пайплайна
	GS::Material* material = GS::System::materials().get( m_material ).get();
	material->setPass( 0 );

	bindParticles();
	DMD3D::instance().setSRV( 0, GS::System::textures().get( m_texture )->srv() );
	DMD3D::instance().draw( particleCount(), 0 );
}

PropertyContainer* DMParticleSystem::properties()
{
	return &m_propertyContainer;
}

void DMParticleSystem::bindParticles()
{
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( 5, m_srvParticles );
	// Буферов вершин и индексов нет
	d3d.unbindGeometry();
}

unsigned int DMParticleSystem::particleCount()
{
	return  m_max_count;
}
