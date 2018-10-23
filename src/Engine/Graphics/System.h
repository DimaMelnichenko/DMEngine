#pragma once

#include "Scene\TextureObjects\DMTextureStorage.h"
#include "Scene\Model\Mesh\MeshStorage.h"
#include "Scene\Model\ModelStorage.h"
#include "Scene\Materials\MaterialStorage.h"

#include "Common\Dictionaries\MaterialParameterKind.h"

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

	static MaterialParameterKind& materialParameterKind();

	static void destroy();


};

}