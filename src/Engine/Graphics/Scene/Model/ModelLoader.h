#pragma once
#include "DirectX.h"
#include "DMModel.h"

namespace GS
{

class ModelLoader
{
public:
	ModelLoader();
	~ModelLoader();

	DMModel&& loadFromFile( const std::wstring& filename, uint32_t id );
};

}