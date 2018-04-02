#pragma once

#include <unordered_map>
#include "DMResourceStorage.h"
#include "DMModel.h"
#include "ModelLoader.h"

namespace GS
{

class ModelStorage : public DMResourceStorage<DMModel>
{
public:
	ModelStorage( const std::wstring& path );
	~ModelStorage();

	bool load( const std::wstring& file );

private:
	ModelLoader m_modelLoader;
};

}