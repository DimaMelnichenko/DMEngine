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

	// Процедурные меши, пока нет настоящих: вписаны в куб от -0.5 до 0.5, плоскость лежит в XZ нормалью вверх;
	// карточка для травы — вертикальный квадрат высотой 1, стоящий на y = 0
	enum class Primitive
	{
		box,
		sphere,
		plane,
		card
	};
	// "box", "sphere", "plane", "card" — как в колонке primitive таблицы Meshes
	static bool primitiveFromName( const std::string& name, Primitive& primitive );

	bool load( uint32_t id, const std::string& name, const std::string& file );
	// Примитив под заданными id и именем: подставляется вместо меша, у которого нет файла
	bool createPrimitive( uint32_t id, const std::string& name, Primitive primitive );
	// Куб в слоте placeholderId: подставляется вместо незагруженных мешей без своего примитива
	bool createPlaceholder();
	// Процедурные меши движка, id вне диапазона base.db3 — до сборки VertexPool (Renderer::initialize): карточка
	// импостера (cardId, Primitive::card — его вершинный шейдер разворачивает её к виду, Shaders/impostor.vs)
	bool createDefaults();
	static constexpr uint32_t cardId = 1000001;

	uint32_t vertexCount() const;
	uint32_t indexCount() const;

private:
	MeshLoader m_meshLoader;
	uint32_t m_vertexCount;
	uint32_t m_indexCount;
};

}