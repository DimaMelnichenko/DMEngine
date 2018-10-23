#pragma once

#include <unordered_map>
#include "Storage\DMResourceStorage.h"
#include "DMMesh.h"
#include "MeshLoader.h"

namespace GS
{

class MeshStorage : public DMResourceStorage<std::unique_ptr<AbstractMesh>>
{
public:
	MeshStorage( const std::string& path );
	~MeshStorage();

	bool load( uint32_t id, const std::string& name, const std::string& file );

	uint32_t vertexCount() const;
	uint32_t indexCount() const;

private:
	MeshLoader m_meshLoader;
	uint32_t m_vertexCount;
	uint32_t m_indexCount;
};

}