#pragma once

#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"

namespace GS
{

// Материал террейна для Shaders\terrain.ps. Слои — строки таблицы TerrainLayers (до maxLayers), вес слоя —
// канал RGBA splat-карты. Текстуры слоёв при загрузке собираются в два массива текстур: «альбедо + высота»
// и «нормаль + шероховатость». Все слои приводятся к размеру первого загруженного и к R8G8B8A8_UNORM,
// вместо ненайденного файла подставляется шахматка (альбедо) или плоская нормаль.
// Мипы строятся без WIC: WIC масштабирует с премультипликацией альфы, а в альфе здесь данные (высота,
// шероховатость, вес четвёртого слоя), и цвет в мипах там, где альфа близка к нулю, пропал бы
class TerrainMaterial
{
public:
	static constexpr uint32_t maxLayers = 4;

	// splatMap — файл относительно каталога текстур
	bool initialize( uint32_t terrainId, const std::string& splatMap );
	// Пиксельный шейдер: t1 — splat-карта, t2 — альбедо + высота, t3 — нормаль + шероховатость, t4 — шум
	void bind() const;
	// Повторов текстуры слоя на единицу мира (1 / tiling) по слоям
	const XMFLOAT4& layerScale() const { return m_layerScale; }

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
	com_unique_ptr<ID3D11ShaderResourceView> m_splatMap;
	com_unique_ptr<ID3D11ShaderResourceView> m_albedoHeight;
	com_unique_ptr<ID3D11ShaderResourceView> m_normalRoughness;
	XMFLOAT4 m_layerScale = XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
};

}
