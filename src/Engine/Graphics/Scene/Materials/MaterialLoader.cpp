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

Material* MaterialLoader::loadFromFile( uint32_t id, const std::string& name, const std::string& vsShaderFile, const std::string& gsShaderFile, const std::string& psShaderFile )
{
	std::unique_ptr<Material> material( new Material( id, name ) );

	if( name == "Color" )
		material->m_shader.reset( new DMColorShader() );
	else if( name == "Phong" )
		material->m_shader.reset( new PhongLight() );
	else if( name == "Texture" )
		material->m_shader.reset( new TextureShader() );
	else if( name == "GeoClipMap" || name == "GeoClipMapWater" )
		material->m_shader.reset( new DMClipMapShader() );

	if( !vsShaderFile.empty() )
	{
		if( !material->m_shader->addShaderPass( SRVType::vs, "main", vsShaderFile ) )
			return nullptr;
	}

	if( !gsShaderFile.empty() )
	{
		if( !material->m_shader->addShaderPass( SRVType::gs, "main", gsShaderFile ) )
			return nullptr;
	}

	if( !psShaderFile.empty() )
	{
		if( !material->m_shader->addShaderPass( SRVType::ps, "main", psShaderFile ) )
			return nullptr;
	}

	material->m_shader->Initialize();

	return material.release();
}

}