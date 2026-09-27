#pragma once

#include <memory>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "Shaders\DMComputeShader.h"
#include "Model/DMModel.h"
#include "ScattererPass.h"
#include "SceneObject.h"
#include "Terrain\TerrainHeightSource.h"

namespace GS
{

// Набор расстановки по террейну: трава, цветы, камешки, веточки (таблица ScatterSets). Слой набора — растение:
// одна или несколько моделей со всеми их LOD и весами (ScatterLayerModels, как Mesh Entries у Static Mesh Spawner в
// PCG UE), маска плотности и параметры (таблица ScatterLayers). Каждый кадр compute-шейдер Shaders\scatter.cs
// раскладывает инстансы слоя по сетке, привязанной к миру, в кольце вокруг камеры: модель ячейки — по весам, отсекает
// по frustum и кладёт в список «вариант × LOD» по расстоянию (дальности LOD модели, как у моделей уровня); отрисовка —
// indirect draw на список. Высоту и координаты масок даёт TerrainHeightSource, поэтому набор не зависит от устройства террейна.
// Проход и отсечение граней задаёт материал слоя (режим и двусторонность, как у травы ландшафта в UE): набор рисует
// непрозрачные и вырезанные по альфе слои в opaque, полупрозрачные — в transparent. Слой с cast_shadow рисуется
// и в проход теней — в те каскады, которые пересекает его кольцо (с запасом на длину тени). Расчёт и отрисовка всех
// наборов переключаются клавишами 3 и 4
class Scatterer : public SceneObject
{
public:
	explicit Scatterer( const std::string& name );

	bool Initialize();
	void setTerrain( const TerrainHeightSource* terrain );
	// Модель слоя (вариант растения) и её вес — доля ячеек слоя
	struct LayerModel
	{
		DMModel* model = nullptr;
		float weight = 1.0f;
	};
	// models — варианты растения (все их LOD, не больше ScatterPass::maxLods; вариантов — не больше
	// ScatterPass::maxVariants); mask — маска плотности в хранилище текстур, в координатах карты высот террейна
	bool addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterPass::PopulateParams& params );

	void compute( const FrameContext& frame ) override;
	// Свой вызов в проходах, где есть слои их режима материала
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;

	void setComputeEnabled( bool enabled );
	bool computeEnabled() const;

	// Окно набора в GUI: по слою — «Cast shadow» (начальное значение — ScatterLayers.cast_shadow)
	PropertyContainer* properties() override;

private:
	// cbuffer TerrainHeightBuffer в Shaders\terrain_height.sh
	struct alignas( 16 ) TerrainParams
	{
		float worldSize;
		float heightMultiplier;
		float heightOffset;
		float padding;
	};

	// cbuffer FrustumBuffer в Shaders\scatter.cs
	struct FrustumParams
	{
		XMFLOAT4 planes[6];
		XMFLOAT4 shadowCast;	// xyz — куда идёт свет солнца, w — длина тени на метр высоты вдоль луча (0 — солнца нет)
	};

	// LOD растения: меш и материал, часть кольца, где он рисуется
	struct LayerLod
	{
		DMModel::LodBlock* block = nullptr;
		DMShader* material = nullptr;
		float nearDistance = 0.0f;
		float farDistance = 0.0f;
	};

	// Вариант растения — модель слоя
	struct LayerVariant
	{
		std::vector<LayerLod> lods;
	};

	struct Layer
	{
		std::vector<LayerVariant> variants;
		std::string mask;
		std::unique_ptr<ScatterPass> pass;
		std::unique_ptr<PropertyContainer> properties;	// адрес не меняется при росте m_layers: его хранит GUI
	};

	// LOD слоя отбрасывает тень: флаг слоя и вариант материала «только глубина»
	bool castsShadow( const Layer& layer, const LayerLod& lod ) const;
	// LOD слоя рисуется в depth prepass: непрозрачный или Masked с вариантом «только глубина»
	bool inDepthPrepass( const LayerLod& lod ) const;

	// Больше потоков на слой не запускаем: при мелком шаге сетка покроет не всё кольцо, а только его середину
	static constexpr uint16_t maxGridDim = 1024;

	bool m_computeEnabled = true;
	float m_shadowLength = 0.0f;	// длина тени на метр высоты инстанса в этом кадре (FrustumParams::shadowCast.w)
	const TerrainHeightSource* m_terrain = nullptr;
	std::vector<Layer> m_layers;
	DMComputeShader m_computeShader;
	DMComputeShader m_initShader;
	com_unique_ptr<ID3D11Buffer> m_terrainBuffer;
	com_unique_ptr<ID3D11Buffer> m_frustumBuffer;
	PropertyContainer m_properties;
};

}
