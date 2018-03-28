#pragma once

#include <unordered_map>
#include "DMResourceStorage.h"
#include "DMMesh.h"
#include "MeshLoader.h"

namespace GS
{

class MeshStorage : public DMResourceStorage<std::unique_ptr<AbstractMesh>>
{
public:
	MeshStorage( const std::wstring& path );
	~MeshStorage();

	bool load( const std::wstring& file );

private:
	MeshLoader m_meshLoader;
};

}