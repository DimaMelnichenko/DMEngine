#include "ModelLoader.h"
#include <fstream>
#include "../../System.h"



namespace GS
{


ModelLoader::ModelLoader()
{
}


ModelLoader::~ModelLoader()
{
}

DMModel* ModelLoader::loadFromFile( const std::string& filename, uint32_t id )
{
	ResourceMetaFile resourceFile( filename );

	//GetPrivateProfileSection( "General", out, 500, level_file.data() );
	int lods = resourceFile.get<int32_t>( "General", "LOD" );
	std::string model_name = resourceFile.get<std::string>( "General", "Name" );

	std::unique_ptr<DMModel> model( new DMModel( id, model_name ) );

	D3DXVECTOR3 pos = strToVec3( resourceFile.get<std::string>( "General", "Position" ) );
	model->transformBuffer().setPosition( pos );

	float scale = resourceFile.get<float>( "General", "Scale" );
	if( scale != 0.0f )
		model->transformBuffer().setScale( scale );

	size_t lod_counter = 0;
	// загружаем все лоды модели
	for( size_t i = 0; i < lods; i++ )
	{
		std::string currentLOD = "LOD" + std::to_string( i );

		float range = resourceFile.get<float>( currentLOD.data(), "Range" );

		std::string meshName = resourceFile.get<std::string>( currentLOD, "Mesh" );
		std::string matName = resourceFile.get<std::string>( currentLOD, "Material" );

		if( !System::meshes().load( meshName ) )
		{
			return nullptr;
		}
		if( !System::materials().load( matName ) )
		{
			return nullptr;
		}
		DMModel::LodBlock block;
		block.material = System::materials().id( matName );
		block.mesh = System::meshes().id( meshName );
		block.params = loadMaterialParam( resourceFile,
										  resourceFile.get<std::string>( currentLOD, "MatParam" ),
										  resourceFile.get<int32_t>( currentLOD, "ParamCount" ) );
		model->addLod( range, block );
	}

	return model.release();
}

ParamSet ModelLoader::loadMaterialParam( ResourceMetaFile& metaResource, const std::string& matSection, int32_t count )
{
	ParamSet paramSet;
	for( int32_t i = 0; i < count; ++i )
	{
		std::string paramBlock = "Param" + matSection + std::to_string( i );

		std::string paramName = metaResource.get<std::string>( paramBlock, "Name" );
		std::string paramType = metaResource.get<std::string>( paramBlock, "Type" );
		std::string paramValue = metaResource.get<std::string>( paramBlock, "Value" );

		if( paramType == "texture" && System::textures().load( paramValue ) )
		{
			paramSet.insert( { paramName, Parameter( System::textures().id( paramValue ) ) } );
		}
		else if( paramType == "vec4" )
		{
			paramSet.insert( { paramName, Parameter( strToVec4( paramValue ) ) } );
		}
	}

	return paramSet;
}

}