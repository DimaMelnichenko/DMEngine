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

	bool clone( const std::string& source, const std::string& newName );

private:
	ModelLoader m_modelLoader;
};

}