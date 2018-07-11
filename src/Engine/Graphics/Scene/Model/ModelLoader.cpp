#include "ModelLoader.h"
#include <fstream>
#include "../../System.h"
#include "tinyxml2.h"


using namespace tinyxml2;

namespace GS
{


ModelLoader::ModelLoader()
{
}


ModelLoader::~ModelLoader()
{
}

DMModel* ModelLoader::loadJSONFile( const std::string& filename, uint32_t id )
{
	using json = nlohmann::json;
	std::unique_ptr<DMModel> model;

	json jsonParser;

	try
	{
		std::ifstream jsonFile( filename.data() );
		jsonFile.is_open();
		jsonFile >> jsonParser;
		jsonFile.close();
	}
	catch( std::exception& )
	{
		return nullptr;
	}


	model.reset( new DMModel( id, jsonParser["Name"] ) );

	XMFLOAT3 pos = strToVec3( jsonParser["position"] );
	model->transformBuffer().setPosition( pos );

	if( jsonParser.find( "scale" ) != jsonParser.end() )
	{
		model->transformBuffer().setScale( (float)jsonParser["scale"] );
	}

	std::unordered_map<std::string, MaterialParam> matsSet;
	loadJSONMaterials( matsSet, jsonParser );
	
	for( auto& lod : jsonParser["lods"] )
	{
		std::string matName = lod["material"];

		if( !System::meshes().load( lod["mesh"] ) )
		{
			return nullptr;
		}

		if( !System::materials().load( matsSet[matName].shaderName ) )
		{
			return nullptr;
		}

		DMModel::LodBlock block;
		block.material = System::materials().id( matsSet[matName].shaderName );
		block.mesh = System::meshes().id( lod["mesh"] );
		block.params = matsSet[matName].param;

		model->addLod( lod["range"], block );
	}


	return model.release();
}

DMModel* ModelLoader::loadXMLFile( const std::string& filename, uint32_t id )
{
	tinyxml2::XMLDocument xmlDoc;

	XMLError eResult = xmlDoc.LoadFile( filename.data() );

	XMLNode * pRoot = xmlDoc.FirstChild();

	if( pRoot == nullptr )
		return nullptr;

	XMLElement* pElement = pRoot->FirstChildElement( "object" );
	if( !pElement )
		return nullptr;

	std::string modelName = pElement->Attribute( "Name" );
	std::unique_ptr<DMModel> model( new DMModel( id, modelName ) );

	XMFLOAT3 pos = strToVec3( pElement->FirstChildElement("position")->GetText() );
	model->transformBuffer().setPosition( pos );

	float scale = pElement->FirstChildElement( "scale" )->FloatText();
	if( scale != 0.0f )
		model->transformBuffer().setScale( scale );

	std::unordered_map<std::string, MaterialParam> matsSet;
	XMLElement* xmlMaterials = pElement->FirstChildElement( "materials" );
	loadMaterials( matsSet, xmlMaterials );


	XMLElement* xmlLodElement = pElement->FirstChildElement( "lods" )->FirstChildElement("lod");
	while( xmlLodElement )
	{
		float range = xmlLodElement->FirstChildElement("range")->IntText();

		std::string meshName( xmlLodElement->FirstChildElement( "mesh" )->GetText() );
		std::string matName( xmlLodElement->FirstChildElement( "material" )->GetText() );

		if( !System::meshes().load( meshName ) )
		{
			return nullptr;
		}

		if( !System::materials().load( matsSet[matName].shaderName ) )
		{
			return nullptr;
		}

		DMModel::LodBlock block;
		block.material = System::materials().id( matsSet[matName].shaderName );
		block.mesh = System::meshes().id( meshName );
		block.params = matsSet[matName].param;

		model->addLod( range, block );

		xmlLodElement = xmlLodElement->NextSiblingElement( "lod" );
	}

	return model.release();
}

void ModelLoader::loadMaterials( std::unordered_map<std::string, MaterialParam>& matsSet, XMLElement* xmlMaterials )
{
	XMLElement* xmlMaterial = xmlMaterials->FirstChildElement("material");
	MaterialParam matParam;

	while( xmlMaterial )
	{
		XMLElement* xmlParam = xmlMaterial->FirstChildElement("params")->FirstChildElement("param");
		while( xmlParam )
		{
			std::string paramName = xmlParam->Attribute( "Name" );
			std::string paramType = xmlParam->Attribute( "Type" );
			std::string paramValue = xmlParam->GetText();

			if( paramType == "texture" )
			{
				matParam.param.insert( { paramName, Parameter( static_cast<uint32_t>( std::stol( paramValue ) ) ) } );
			}
			else if( paramType == "vec4" )
			{
				matParam.param.insert( { paramName, Parameter( strToVec4( paramValue ) ) } );
			}

			xmlParam = xmlParam->NextSiblingElement( "param" );
		}	
		
		matParam.shaderName = xmlMaterial->Attribute( "Shader" );

		matsSet.insert( { xmlMaterial->Attribute( "Name" ), std::move( matParam ) } );

		xmlMaterial = xmlMaterial->NextSiblingElement( "material" );
	}
}

DMModel* ModelLoader::loadFromFile( const std::string& filename, uint32_t id )
{
	std::size_t found = filename.find( ".xml" );
	if( found != std::string::npos )
		return loadXMLFile( filename, id );

	found = filename.find( ".json" );
	if( found != std::string::npos )
		return loadJSONFile( filename, id );

	ResourceMetaFile resourceFile( filename );

	//GetPrivateProfileSection( "General", out, 500, level_file.data() );
	int lods = resourceFile.get<int32_t>( "General", "LOD" );
	std::string model_name = resourceFile.get<std::string>( "General", "Name" );

	std::unique_ptr<DMModel> model( new DMModel( id, model_name ) );

	XMFLOAT3 pos = strToVec3( resourceFile.get<std::string>( "General", "Position" ) );
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
	for( uint32_t i = 0; i < count; ++i )
	{
		std::string paramBlock = "Param" + matSection + std::to_string( i );

		std::string paramName = metaResource.get<std::string>( paramBlock, "Name" );
		std::string paramType = metaResource.get<std::string>( paramBlock, "Type" );
		std::string paramValue = metaResource.get<std::string>( paramBlock, "Value" );

		if( paramType == "texture" )
		{
			paramSet.insert( { paramName, Parameter( static_cast<uint32_t>( std::stol( paramValue ) ) ) } );
		}
		else if( paramType == "vec4" )
		{
			paramSet.insert( { paramName, Parameter( strToVec4( paramValue ) ) } );
		}
	}

	return paramSet;
}

void ModelLoader::loadJSONMaterials( std::unordered_map<std::string, MaterialParam>& matsSet, nlohmann::json& jsonParser )
{	
	MaterialParam matParam;

	for( auto& material : jsonParser["materials"] )
	{
		for( auto& param : material["params"] )
		{
			std::string paramName = param["Name"];
			std::string paramType = param["Type"];
			std::string paramValue = param["Value"];

			if( paramType == "texture" )
			{
				matParam.param.insert( { paramName, Parameter( static_cast<uint32_t>( std::stol( paramValue ) ) ) } );
			}
			else if( paramType == "vec4" )
			{
				matParam.param.insert( { paramName, Parameter( strToVec4( paramValue ) ) } );
			}
		}

		matParam.shaderName = material["Shader"];

		matsSet.insert( { material["Name"], std::move( matParam ) } );
	}
}

}