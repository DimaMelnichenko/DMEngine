#pragma once

#include <unordered_map>

#include "Storage\DMResourceStorage.h"
#include "DMTexture.h"

namespace GS
{

class DMTextureStorage : public DMResourceStorage<std::unique_ptr<DMTexture>>
{
public:
	DMTextureStorage( const std::string& path );
	~DMTextureStorage();

	// preserveAlphaCoverage — порог альфы (AlphaCutoff материала Masked), при котором мипы сохраняют долю
	// непрозрачных пикселей нулевого мипа (Textures.preserve_alpha_coverage); 0 — обычные мипы
	bool load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB,
			   float preserveAlphaCoverage = 0.0f );
	// Шахматная текстура в слоте placeholderId: подставляется вместо незагруженных текстур
	bool createPlaceholder();
	// Процедурные текстуры, id вне диапазона base.db3: монохромный шум 256² (R8_SNORM, один и тот же при каждом запуске —
	// террейн) и текстуры 1×1 для материалов, у которых текстура не задана: белая и плоская нормаль
	bool createDefaults();

	static constexpr uint32_t noiseId = 1000000;
	static constexpr uint32_t whiteId = 1000001;
	static constexpr uint32_t flatNormalId = 1000002;

private:
	bool createSolid( uint32_t id, const std::string& name, uint32_t color );
	bool createNoise( uint32_t id, const std::string& name, uint32_t size );
};

}