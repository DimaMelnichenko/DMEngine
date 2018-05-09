#pragma once
#include "DirectX.h"
#include "DMModel.h"
#include "ResourceMetaFile.h"

namespace GS
{

class ModelLoader
{
public:
	ModelLoader();
	~ModelLoader();

	DMModel* loadFromFile( const std::string& filename, uint32_t id );

private:
	ParamSet loadMaterialParam( ResourceMetaFile&, const std::string& matSection, int32_t count );
};

}