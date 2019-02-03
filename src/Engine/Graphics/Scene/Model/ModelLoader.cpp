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
				matParam.param.insert( paramName, std::stol( paramValue ) );
			}
			else if( paramType == "vec4" )
			{
				XMFLOAT4 vec4;
				if( strToVec4( paramValue, vec4 ) )
					matParam.param.insert( paramName, vec4 );
			}

			xmlParam = xmlParam->NextSiblingElement( "param" );
		}	
		
		matParam.shaderName = xmlMaterial->Attribute( "Shader" );

		matsSet.insert( { xmlMaterial->Attribute( "Name" ), std::move( matParam ) } );

		xmlMaterial = xmlMaterial->NextSiblingElement( "material" );
	}
}

bool ModelLoader::loadMaterialParam( ResourceMetaFile& metaResource, const std::string& matSection, int32_t count, PropertyContainer& properties )
{
	for( uint32_t i = 0; i < count; ++i )
	{
		std::string paramBlock = "Param" + matSection + std::to_string( i );

		std::string paramName = metaResource.get<std::string>( paramBlock, "Name" );
		std::string paramType = metaResource.get<std::string>( paramBlock, "Type" );
		std::string paramValue = metaResource.get<std::string>( paramBlock, "Value" );

		if( paramType == "texture" )
		{
			properties.insert( paramName, (uint32_t)std::stol( paramValue ) );
		}
		else if( paramType == "vec4" )
		{
			XMFLOAT4 vec4;
			if( strToVec4( paramValue, vec4 ) )
				properties.insert( paramName, vec4 );
		}
	}

	return true;
}

}