#pragma once

#include "Scene\Texture\DMTextureStorage.h"
#include "Scene\Model\Mesh\MeshStorage.h"
#include "Scene\Model\ModelStorage.h"
#include "Scene\Materials\MaterialStorage.h"
#include "Scene\Materials\MaterialInstanceStorage.h"


namespace GS
{

// Реестр ассетов, как AssetManager в UE: хранилища текстур, мешей, моделей, материалов и их экземпляров
class Assets
{
public:

	static DMTextureStorage& textures();
	static MeshStorage& meshes();
	static ModelStorage& models();
	static MaterialStorage& materials();
	// Экземпляры материалов: параметры, общие у секций моделей с одним экземпляром
	static MaterialInstanceStorage& materialInstances();
	static void destroy();


};

}