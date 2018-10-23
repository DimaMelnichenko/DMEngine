#pragma once
#include "Storage\DMResource.h"
#include "VertexData.h"

namespace GS
{

class AbstractMesh : public DMResource
{
public:
	AbstractMesh() = default;
	AbstractMesh( uint32_t id, const std::string& name, std::vector<uint32_t>&& );
	AbstractMesh( AbstractMesh&& other );
	AbstractMesh& operator=( AbstractMesh&& other );
	~AbstractMesh();

	VertexData::Type vertexType() const;

	void setOffsets( uint32_t vertexOffset, uint32_t indexoffset );
	uint32_t vertexOffset() const;
	uint32_t indexOffset() const;
	virtual uint32_t vertexCount() const = 0;
	uint32_t indexCount() const;

	virtual char* getVertices() = 0;
	const std::vector<uint32_t>& getIndices() const;


protected:
	VertexData::Type m_vertex_combination;
	std::vector<uint32_t> m_indices;
	uint32_t m_vertexOffset;
	uint32_t m_indexOffset;
};

}