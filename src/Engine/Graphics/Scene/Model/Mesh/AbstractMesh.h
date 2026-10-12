#pragma once
#include <algorithm>
#include <DirectXCollision.h>
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

	void setOffsets( uint32_t vertexOffset, uint32_t indexoffset );
	uint32_t vertexOffset() const;
	uint32_t indexOffset() const;
	virtual uint32_t vertexCount() const = 0;
	uint32_t indexCount() const;

	// Вершины на CPU (vertexCount × vertexStride байт): их же копирует VertexPool
	virtual char* getVertices() = 0;
	virtual uint32_t vertexStride() const = 0;
	const std::vector<uint32_t>& getIndices() const;

	// Границы в координатах меша (считаются при загрузке): по ним экземпляры отсекаются по frustum
	const DirectX::BoundingBox& bounds() const { return m_bounds; }
	// Границы основания — вершин не выше baseBand() над низом меша: где модель касается земли (все пучки куртины травы,
	// ствол дерева без кроны). По ним расстановка проверяет, что под моделью нет русла и воды (Shaders/scatter.cs)
	const DirectX::BoundingBox& baseBounds() const { return m_baseBounds; }
	static float baseBand( float height ) { return std::min( 0.1f * height, 0.25f ); }

	// Данные ветра дерева по вершинам (VertexData::Wind) — блок WIND файла меша; пусто — у меша их нет
	void setWind( std::vector<VertexData::Wind>&& wind ) { m_wind = std::move( wind ); }
	const std::vector<VertexData::Wind>& wind() const { return m_wind; }


protected:
	VertexData::Type m_vertex_combination;
	std::vector<uint32_t> m_indices;
	std::vector<VertexData::Wind> m_wind;
	uint32_t m_vertexOffset;
	uint32_t m_indexOffset;
	DirectX::BoundingBox m_bounds;
	DirectX::BoundingBox m_baseBounds;
};

}