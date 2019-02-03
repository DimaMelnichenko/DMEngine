#pragma once
#include "DirectX.h"
#include "DMModel.h"
#include "ResourceMetaFile.h"
//#include "json.hpp"

namespace tinyxml2
{
	class XMLElement;
}

namespace GS
{

class ModelLoader
{
public:
	ModelLoader();
	~ModelLoader();

	DMModel* loadFromFile( const std::string& filename, uint32_t id );

private:
	bool loadMaterialParam( ResourceMetaFile&, const std::string& matSection, int32_t count, PropertyContainer& );


	struct MaterialParam
	{
		std::string shaderName;
		PropertyContainer param;
	};

	void loadMaterials( std::unordered_map<std::string, MaterialParam>& mats, tinyxml2::XMLElement* );

};

}