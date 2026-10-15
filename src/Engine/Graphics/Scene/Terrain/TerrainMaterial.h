#pragma once

#include "D3D\GpuResources.h"
#include <array>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\terrain_layer_constants.h"

namespace GS
{

// Материал террейна для Shaders\terrain.ps. Слои — строки таблицы TerrainLayers (до maxLayers), вес слоя — канал
// splat-карты: она — массив RGBA, как weightmap в UE Landscape (срез 0 — слои 0…3, срез 1 — 4…7, срез 2 — не веса: R —
// мокрость, G, B, A — запас под дождь); веса клеток считает TerrainPipeline (правила, правки рельефа, русла, мазки кисти). Текстуры слоёв при загрузке собираются в два массива текстур: «альбедо + высота»
// и «нормаль (RG) + затенение окружением (B) + шероховатость». Все слои приводятся к размеру первого загруженного и к R8G8B8A8: альбедо — sRGB
// (R8G8B8A8_UNORM_SRGB, высота в альфе линейная), нормаль и шероховатость — UNORM; вместо ненайденного файла
// подставляется шахматка (альбедо) или плоская нормаль. Слои из фото собирает Tools/pack_terrain_layer.py.
// Мипы строятся без WIC: WIC масштабирует с премультипликацией альфы, а в альфе здесь данные (высота,
// шероховатость, вес четвёртого слоя), и цвет в мипах там, где альфа близка к нулю, пропал бы
class TerrainMaterial
{
public:
	static constexpr uint32_t maxLayers = TERRAIN_MAX_LAYERS;
	static constexpr uint32_t splatSlices = maxLayers / 4;
	static constexpr uint32_t splatTextureSlices = splatSlices + 1;	// и срез не весов (мокрость)

	// Итог клеток splat-карты (size², строка 0 — дальний край по z): веса слоёв 0…1 (пустой — слоя нет) и мокрость 0…1
	// (пустая — 0); size 0 — весь вес у слоя 0
	struct SplatWeights
	{
		uint32_t size = 0;
		std::array<std::vector<float>, maxLayers> layers;
		std::vector<float> wetness;
	};

	bool initialize( uint32_t terrainId, const SplatWeights& weights );
	// Splat-карта заново (русла, правила, мазки)
	bool repaint( const SplatWeights& weights );
	// Пиксельный шейдер: t3 — нормаль + AO + шероховатость
	void bind() const;
	// Ресурсы сцены (terrain_layers.sh): splat-карта, альбедо + высота и шум пятен яркости — их читают и дисплейсмент,
	// расстановка (цвет земли у травы), частицы
	void bindScene() const;
	// Повторов текстуры слоя на единицу мира (1 / tiling) по слоям, по четыре в XMFLOAT4
	const std::array<DirectX::XMFLOAT4, splatSlices>& layerScale() const { return m_layerScale; }
	// Слоёв в массивах текстур: наибольший описанный TerrainLayers.layer + 1
	uint32_t layerCount() const { return m_layerCount; }
	// Слой: имя (TerrainLayers.name; пусто — слоя в таблице нет) и метров на повтор текстуры (TerrainLayers.tiling)
	const std::string& layerName( uint32_t layer ) const { return m_layerNames[layer]; }
	float tiling( uint32_t layer ) const { return m_tiling[layer]; }
	void setTiling( uint32_t layer, float tiling );
	// Множитель альбедо слоя (TerrainLayers.albedo_tint, как Layer Tint у материалов Landscape в UE): фото-текстура темнее
	// или другого оттенка, чем нужно уровню, — цвет подбирается в окне террейна без перепаковки; по слоям для шейдера — xyz
	const DirectX::XMFLOAT3& tint( uint32_t layer ) const { return m_tint[layer]; }
	void setTint( uint32_t layer, const DirectX::XMFLOAT3& tint );
	const std::array<DirectX::XMFLOAT4, maxLayers>& layerTint() const { return m_layerTint; }
	// Размах дисплейсмента слоя, м (TerrainLayers.displacement, NULL — 0): высота текстуры 0…1 сдвигает вершину на
	// (высота − 0,5) × размах; по слоям для шейдера — по четыре в XMFLOAT4; наибольший — запас границ узлов
	float displacement( uint32_t layer ) const { return m_displacement[layer]; }
	void setDisplacement( uint32_t layer, float displacement );
	const std::array<DirectX::XMFLOAT4, splatSlices>& layerDisplacement() const { return m_layerDisplacement; }
	float maxDisplacement() const;
	// Сила затенения окружением слоя (TerrainLayers.occlusion, NULL — 1): AO из B массива нормалей — 1 как в текстуре, 0 —
	// без него; по слоям для шейдера — по четыре в XMFLOAT4
	float occlusion( uint32_t layer ) const { return m_occlusion[layer]; }
	void setOcclusion( uint32_t layer, float occlusion );
	const std::array<DirectX::XMFLOAT4, splatSlices>& layerOcclusion() const { return m_layerOcclusion; }

private:
	struct Layer
	{
		std::string name;
		std::string albedo;
		std::string normal;
		float tiling = 1.0f;
		DirectX::XMFLOAT3 tint = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
		float displacement = 0.0f;
		float occlusion = 1.0f;
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
	std::array<DirectX::XMFLOAT4, splatSlices> m_layerScale = {};
	uint32_t m_layerCount = 0;
	std::vector<std::string> m_layerNames;
	std::vector<float> m_tiling;
	std::vector<DirectX::XMFLOAT3> m_tint;
	std::vector<float> m_displacement;
	std::array<DirectX::XMFLOAT4, splatSlices> m_layerDisplacement = {};
	std::vector<float> m_occlusion;
	std::array<DirectX::XMFLOAT4, splatSlices> m_layerOcclusion = {};
	std::array<DirectX::XMFLOAT4, maxLayers> m_layerTint = {};
};

}
