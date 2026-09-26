#pragma once

#include <unordered_map>

#include "Storage\DMResourceStorage.h"
#include "DMTexture.h"
#include "TextureLoader.h"

namespace GS
{

class DMTextureStorage : public DMResourceStorage<std::unique_ptr<DMTexture>>
{
public:
	DMTextureStorage( const std::string& path );
	~DMTextureStorage();

	bool load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB );
	// Шахматная текстура в слоте placeholderId: подставляется вместо незагруженных текстур
	bool createPlaceholder();
	// Текстуры 1×1 для материалов, у которых текстура не задана: белая и плоская нормаль.
	// Id вне диапазона base.db3, как у процедурного шума (1000000)
	bool createDefaults();

	static constexpr uint32_t whiteId = 1000001;
	static constexpr uint32_t flatNormalId = 1000002;

private:
	bool createSolid( uint32_t id, const std::string& name, uint32_t color );

	TextureLoader m_textureLoader;
};

}