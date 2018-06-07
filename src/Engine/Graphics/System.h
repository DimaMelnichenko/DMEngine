#pragma once

#include "Scene\TextureObjects\DMTextureStorage.h"
#include "Scene\Model\Mesh\MeshStorage.h"
#include "Scene\Model\ModelStorage.h"
#include "Scene\Materials\MaterialStorage.h"

namespace GS
{

class System
{
public:
	System();
	~System();

	static DMTextureStorage& textures();
	static MeshStorage& meshes();
	static ModelStorage& models();
	static MaterialStorage& materials();
	static void destroy();

private:
	static std::unique_ptr<DMTextureStorage> m_textureStorage;
	static std::unique_ptr<MeshStorage> m_meshStorage;
	static std::unique_ptr<ModelStorage> m_modelStorage;
	static std::unique_ptr<MaterialStorage> m_materialStorage;
};

}