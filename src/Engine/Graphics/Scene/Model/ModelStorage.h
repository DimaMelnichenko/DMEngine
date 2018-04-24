#pragma once

#include <unordered_map>
#include "DMResourceStorage.h"
#include "DMModel.h"
#include "ModelLoader.h"

namespace GS
{

class ModelStorage : public DMResourceStorage<std::unique_ptr<DMModel>>
{
public:
	ModelStorage( const std::string& path );
	~ModelStorage();

	bool load( const std::string& file );

private:
	ModelLoader m_modelLoader;
};

}