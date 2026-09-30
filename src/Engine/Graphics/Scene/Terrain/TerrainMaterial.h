#pragma once

#include "D3D\GpuResources.h"
#include <array>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"

namespace GS
{

// Материал террейна для Shaders\terrain.ps. Слои — строки таблицы TerrainLayers (до maxLayers), вес слоя — канал
// splat-карты: она — массив из двух RGBA, как weightmap в UE Landscape (срез 0 — слои 0…3, срез 1 — 4…7; файл из
// одного среза дополняется нулями). Текстуры слоёв при загрузке собираются в два массива текстур: «альбедо + высота»
// и «нормаль + шероховатость». Все слои приводятся к размеру первого загруженного и к R8G8B8A8: альбедо — sRGB
// (R8G8B8A8_UNORM_SRGB, высота в альфе линейная), нормаль и шероховатость — UNORM; вместо ненайденного файла
// подставляется шахматка (альбедо) или плоская нормаль. Слои из фото собирает Tools/pack_terrain_layer.py.
// Мипы строятся без WIC: WIC масштабирует с премультипликацией альфы, а в альфе здесь данные (высота,
// шероховатость, вес четвёртого слоя), и цвет в мипах там, где альфа близка к нулю, пропал бы
class TerrainMaterial
{
public:
	static constexpr uint32_t maxLayers = 8;
	static constexpr uint32_t splatSlices = maxLayers / 4;

	// splatMap — файл относительно каталога текстур
	bool initialize( uint32_t terrainId, const std::string& splatMap );
	// Пиксельный шейдер: t1 — splat-карта, t2 — альбедо + высота, t3 — нормаль + шероховатость, t4 — шум
	void bind() const;
	// Повторов текстуры слоя на единицу мира (1 / tiling) по слоям, по четыре в XMFLOAT4
	const std::array<XMFLOAT4, splatSlices>& layerScale() const { return m_layerScale; }
	// Слоёв в массивах текстур: наибольший описанный TerrainLayers.layer + 1
	uint32_t layerCount() const { return m_layerCount; }

private:
	struct Layer
	{
		std::string name;
		std::string albedo;
		std::string normal;
		float tiling = 1.0f;
	};

	bool loadLayers( uint32_t terrainId, std::vector<Layer>& layers );

private:
	// Текстуры и виды: массивы срезов (splat-карта — веса, слои — альбедо с высотой и нормаль с шероховатостью)
	Texture m_splatMapTexture;
	Texture m_albedoHeightTexture;
	Texture m_normalRoughnessTexture;
	ShaderView m_splatMap;
	ShaderView m_albedoHeight;
	ShaderView m_normalRoughness;
	std::array<XMFLOAT4, splatSlices> m_layerScale = {};
	uint32_t m_layerCount = 0;
};

}
