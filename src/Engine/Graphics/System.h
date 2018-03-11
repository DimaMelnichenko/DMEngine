#pragma once

#include "Scene\TextureObjects\DMTextureStorage.h"

namespace GS
{

class System
{
public:
	System();
	~System();

	static DMTextureStorage& textures();

private:
	static std::unique_ptr<DMTextureStorage> m_textureStorage;
};

}