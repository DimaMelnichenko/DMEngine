#include "MaterialStorage.h"
#include "Shaders\DMColorShader.h"
#include "Shaders\TextureShader.h"
#include "Shaders\PhongLight.h"
#include "Shaders\DMClipMapShader.h"
#include "Shaders\DMParticleShader.h"

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

	if( matClass == "Color" )
		material->m_shader.reset( new DMColorShader() );
	else if( matClass == "Phong" )
		material->m_shader.reset( new PhongLight() );
	else if( matClass == "Texture" )
		material->m_shader.reset( new TextureShader() );
	else if( matClass == "Texture" )
		material->m_shader.reset( new TextureShader() );
	else if( matClass == "Particle" )
		material->m_shader.reset( new DMParticleShader() );
	else if( matClass == "GeoClipMap" )
		material->m_shader.reset( new DMClipMapShader() );
	if( material == nullptr )
		return false;

	insertResource( std::move( material ) );

	return true;
}

}