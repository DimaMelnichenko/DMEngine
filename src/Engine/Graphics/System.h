#pragma once

#include "Scene\Texture\DMTextureStorage.h"
#include "Scene\Model\Mesh\MeshStorage.h"
#include "Scene\Model\ModelStorage.h"
#include "Scene\Materials\MaterialStorage.h"


namespace GS
{

class System
{
public:

	static DMTextureStorage& textures();
	static MeshStorage& meshes();
	static ModelStorage& models();
	static MaterialStorage& materials();
	static void destroy();


};

}