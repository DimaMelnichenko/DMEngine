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

// Набор расстановки по террейну: трава, цветы, камешки, веточки (таблица ScatterSets). Слой набора — LOD модели,
// маска плотности и параметры (таблица ScatterLayers): каждый кадр compute-шейдер Shaders\scatter.cs раскладывает
// инстансы слоя по сетке, привязанной к миру, в кольце вокруг камеры и отсекает их по frustum, отрисовка — indirect
// draw. Высоту и координаты масок даёт TerrainHeightSource, поэтому набор не зависит от устройства террейна.
// Проход и отсечение граней задаёт материал слоя (режим и двусторонность, как у травы ландшафта в UE): набор рисует
// непрозрачные и вырезанные по альфе слои в opaque, полупрозрачные — в transparent. Расчёт и отрисовка всех наборов
// переключаются клавишами 3 и 4
class Scatterer : public SceneObject
{
public:
	explicit Scatterer( const std::string& name );

	bool Initialize();
	void setTerrain( const TerrainHeightSource* terrain );
	// Текстура цвета травы в слоте t1 пиксельного шейдера; пустое имя — не привязывать
	void setColorTexture( const std::string& texture );
	// mask — маска плотности в хранилище текстур, в координатах карты высот террейна
	bool addLayer( DMModel::LodBlock* lodBlock, const std::string& mask, const ScatterPass::PopulateParams& params );

	void compute( const FrameContext& frame ) override;
	// Свой вызов в проходах, где есть слои их режима материала
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;

	void setComputeEnabled( bool enabled );
	bool computeEnabled() const;

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
	};

	struct Layer
	{
		DMModel::LodBlock* lodBlock = nullptr;
		DMShader* material = nullptr;
		std::string mask;
		std::unique_ptr<ScatterPass> pass;
	};

	// Больше потоков на слой не запускаем: при мелком шаге сетка покроет не всё кольцо, а только его середину
	static constexpr uint16_t maxGridDim = 1024;

	bool m_computeEnabled = true;
	const TerrainHeightSource* m_terrain = nullptr;
	std::string m_colorTexture;
	std::vector<Layer> m_layers;
	DMComputeShader m_computeShader;
	DMComputeShader m_initShader;
	com_unique_ptr<ID3D11Buffer> m_terrainBuffer;
	com_unique_ptr<ID3D11Buffer> m_frustumBuffer;
};

}
