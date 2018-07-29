#pragma once

#include <unordered_map>

#include "DMResourceStorage.h"
#include "DMTexture.h"
#include "TextureLoader.h"

namespace GS
{

class DMTextureStorage : public DMResourceStorage<std::unique_ptr<DMTexture>>
{
public:
	DMTextureStorage( const std::string& path );
	~DMTextureStorage();

	bool load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap );

private:
	TextureLoader m_textureLoader;
};

}