#include "MaterialLoader.h"
#include "ResourceMetaFile.h"
#include "Shaders\DMColorShader.h"
#include "Shaders\TextureShader.h"
#include "Shaders\PhongLight.h"
#include "Shaders\DMClipMapShader.h"
#include "System.h"


namespace GS
{

MaterialLoader::MaterialLoader()
{
}


MaterialLoader::~MaterialLoader()
{
}

Material* MaterialLoader::loadFromFile( const std::string& filename, uint32_t id, const std::string& name )
{
	ResourceMetaFile metaResource( filename );

	std::unique_ptr<Material> material( new Material( id, name ) );

	if( name == "Color" )
		material->m_shader.reset( new DMColorShader() );
	else if( name == "Phong" )
		material->m_shader.reset( new PhongLight() );
	else if( name == "Texture" )
		material->m_shader.reset( new TextureShader() );
	else if( name == "GeoClipMap" )
		material->m_shader.reset( new DMClipMapShader() );

	std::string path = "Shaders\\";

	std::string vsShaderName = metaResource.get<std::string>( "Shaders", "vs" );
	std::string psShaderName = metaResource.get<std::string>( "Shaders", "ps" );

	if( !vsShaderName.empty() )
	{
		if( !material->m_shader->addShaderPass( SRVType::vs, "main", path + vsShaderName ) )
			return nullptr;
	}

	if( !psShaderName.empty() )
	{
		if( !material->m_shader->addShaderPass( SRVType::ps, "main", path + psShaderName ) )
			return nullptr;
	}

	material->m_shader->Initialize();

	return material.release();
}

}