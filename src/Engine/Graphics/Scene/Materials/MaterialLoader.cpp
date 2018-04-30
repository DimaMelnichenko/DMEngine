#include "MaterialLoader.h"
#include "ResourceMetaFile.h"
#include "../Shaders/DMColorShader.h"


namespace GS
{

MaterialLoader::MaterialLoader()
{
}


MaterialLoader::~MaterialLoader()
{
}

Material* MaterialLoader::loadFromFile( const std::string& filename, uint32_t id )
{
	ResourceMetaFile metaResource( filename );

	Material* material = new Material( id, filename );
	material->m_shader.reset( new DMColorShader() );
	material->m_shader->Initialize();

	std::string path = "Shaders\\";

	std::string vsShaderName = metaResource.get<std::string>( "Shaders", "vs" );
	std::string psShaderName = metaResource.get<std::string>( "Shaders", "ps" );

	if( !vsShaderName.empty() && !psShaderName.empty() )
	{
		material->m_shader->addShaderPass( SRVType::vs, "main", path + vsShaderName );
		material->m_shader->addShaderPass( SRVType::ps, "main", path + psShaderName );
		material->m_shader->createPhase( 0, -1, 0 );
	}

	return material;
}

}