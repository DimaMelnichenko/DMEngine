#include "ModelLoader.h"
#include <fstream>
#include "ResourceMetaFile.h"
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
	int lods = resourceFile.number( "General", "LOD" );
	std::string model_name = resourceFile.string( "General", "Name" );

	std::unique_ptr<DMModel> model( new DMModel( id, model_name ) );

	int x = resourceFile.number( "Position", "x" );
	int y = resourceFile.number( "Position", "y" );
	int z = resourceFile.number( "Position", "z" );
	model->transformBuffer().setPosition( x, y, z );

	size_t lod_counter = 0;
	// загружаем все лоды модели
	for( size_t i = 0; i < lods; i++ )
	{
		std::string currentLOD = "LOD" + std::to_string( i );

		int range = resourceFile.number( currentLOD.data(), "Range" );

		std::string meshName = resourceFile.string( currentLOD, "Mesh" );
		std::string matName = resourceFile.string( currentLOD, "Material" );

		if( !System::meshes().load( meshName ) )
		{
			std::string text = "Can`not load mesh:" + meshName;				
			throw std::exception( text.data() );
		}
		if( !System::materials().load( matName ) )
		{
			std::string text = "Can`not load material:" + matName;
			throw std::exception( text.data() );
		}
		model->addLod( range, System::meshes().id( meshName ), System::materials().id( matName ) );
	}
	

	return model.release();
}

}