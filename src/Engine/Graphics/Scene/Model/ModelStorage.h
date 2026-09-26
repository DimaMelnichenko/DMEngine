#pragma once

#include <unordered_map>
#include "Storage\DMResourceStorage.h"
#include "DMModel.h"

namespace GS
{

class ModelStorage : public DMResourceStorage<std::unique_ptr<DMModel>>
{
public:
	ModelStorage( const std::string& path );
	~ModelStorage();

	bool createModel( uint32_t id, const std::string& name );
};

}