#include "MaterialStorage.h"
#include "Shaders\DMColorShader.h"
#include "Shaders\TextureShader.h"
#include "Shaders\PBRMaterial.h"
#include "Shaders\DMParticleShader.h"
#include "Shaders\DMGrassShader.h"
#include "Shaders\VertexLight.h"
#include "Logger\Logger.h"

namespace GS
{

MaterialStorage::MaterialStorage( const std::string& path ) : DMResourceStorage( path )
{

}

MaterialStorage::~MaterialStorage()
{

}

bool MaterialStorage::createMaterial( uint32_t id, const std::string& name, const std::string& matClass )
{
	if( exists( id ) )
		return true;

	std::string fullPath = path() + "\\";
	
	std::unique_ptr<Material> material( new Material( id, name ) );

	DMShader* shader = nullptr;

	if( matClass == "Color" )
		shader = new DMColorShader();
	else if( matClass == "PBR" )
		shader = new PBRMaterial();
	else if( matClass == "Texture" )
		shader = new TextureShader();
	else if( matClass == "Particle" )
		shader = new DMParticleShader();
	else if( matClass == "Grass" )
		shader = new DMGrassShader();
	else if( matClass == "VertexLight" )
		shader = new VertexLight();

	if( !shader )
	{
		LOG( "Unknown material class '" + matClass + "' of material " + name );
		return false;
	}

	shader->setLayoutDesc( std::move( shader->initLayouts() ) );

	material->m_shader.reset( shader );

	insertResource( std::move( material ) );

	return true;
}

}