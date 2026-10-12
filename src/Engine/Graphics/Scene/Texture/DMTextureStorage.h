#pragma once

#include <unordered_map>

#include "Storage\DMResourceStorage.h"
#include "DMTexture.h"

namespace GS
{

// Сжатие текстуры на GPU, как Compression Settings в UE (колонка Textures.compression): standard («default») — BC7 (цвет и
// данные с альфой; sRGB — по Textures.sRGB), normalMap («normalmap») — BC5 (X и Y нормали; Z восстанавливает шейдер),
// none — без сжатия (R8G8B8A8, как в файле)
enum class TextureCompression { none, standard, normalMap };

class DMTextureStorage : public DMResourceStorage<std::unique_ptr<DMTexture>>
{
public:
	DMTextureStorage( const std::string& path );
	~DMTextureStorage();

	// preserveAlphaCoverage — порог альфы (AlphaCutoff материала Masked), при котором мипы сохраняют долю
	// непрозрачных пикселей нулевого мипа (Textures.preserve_alpha_coverage); 0 — обычные мипы. Готовая текстура (мипы,
	// сжатие) — в кэше cache\textures\<имя>.<отпечаток>.dds, как cook в UE: разбор PNG, мипы и сжатие — один раз, пока
	// не сменились файл или настройки (отпечаток — DMTexture::sourceStamp)
	bool load( uint32_t id, const std::string& name, const std::string& file, bool generateMipMap, bool sRGB,
			   float preserveAlphaCoverage = 0.0f, TextureCompression compression = TextureCompression::none );
	// Значение колонки Textures.compression: default, normalmap, none (и всё прочее)
	static TextureCompression compressionFromName( const std::string& name );
	// Меняется приготовление текстур (мипы, параметры сжатия) — поднять: кэш приготовится заново
	static constexpr uint32_t cookVersion = 1;
	// Шахматная текстура в слоте placeholderId: подставляется вместо незагруженных текстур
	bool createPlaceholder();
	// Процедурные текстуры, id вне диапазона base.db3: монохромный шум 256² (R8_SNORM, один и тот же при каждом запуске —
	// террейн) и текстуры 1×1 для материалов, у которых текстура не задана: белая и плоская нормаль
	bool createDefaults();
	// Текстура, которую считает сам движок (маски правил слоёв, TerrainMasks), по имени: есть — пересоздаётся на месте
	// (кто держит &srv(), видит новую), нет — добавляется с id вне base.db3
	bool setGenerated( const std::string& name, const TextureDesc& desc, const TextureData& data );

	static constexpr uint32_t noiseId = 1000000;
	static constexpr uint32_t whiteId = 1000001;
	static constexpr uint32_t flatNormalId = 1000002;

private:
	bool createSolid( uint32_t id, const std::string& name, uint32_t color );
	bool createNoise( uint32_t id, const std::string& name, uint32_t size );
};

}