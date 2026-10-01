#pragma once

#include <memory>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "DMComputeShader.h"
#include "Level\LevelSettings.h"
#include "Model/DMModel.h"
#include "ScattererPass.h"
#include "SceneObject.h"
#include "Terrain\TerrainHeightSource.h"

namespace GS
{

// Набор расстановки по террейну: трава, цветы, камешки, веточки (таблица ScatterSets). Слой набора — растение:
// одна или несколько моделей со всеми их LOD и весами (ScatterLayerModels, как Mesh Entries у Static Mesh Spawner в
// PCG UE), маска плотности и параметры (таблица ScatterLayers). Каждый кадр compute-шейдер Shaders\scatter.cs
// раскладывает инстансы слоя по сетке, привязанной к миру, в кольце вокруг камеры: модель ячейки — по весам, а для
// каждого вида кадра (главная камера и каскады теней) отбирает видимые в свой список «вариант × LOD» по расстоянию
// (дальности LOD модели, как у моделей уровня, со своим разбросом у экземпляра; у видов теней LOD можно брать грубее —
// «Shadow LOD scale»; в полосе перехода — в списки перехода обоих LOD, которые рисуются с дизерингом, как Dithered
// LOD Transition в UE). Отрисовка — один ExecuteIndirect со счётчиком на группу секций с одним материалом и состоянием
// на вид; команды пишет тот же шейдер (buildCommands). Высоту и координаты масок даёт TerrainHeightSource, поэтому
// набор не зависит от устройства террейна. Проход и отсечение граней задаёт материал слоя (режим и двусторонность, как
// у травы ландшафта в UE): набор рисует непрозрачные и вырезанные по альфе слои в opaque, полупрозрачные — в
// transparent. Слой с cast_shadow рисуется и в проход теней — списками видов каскадов. Постоянный слой (persistent,
// лес) раскладывается не кольцом каждый кадр, а один раз на всю карту кластерами; каждый кадр отбираются видимые кластеры
// и их экземпляры — в те же списки видов. Модели слоя — и модели уровня (ель): материал собирает вариант для экземпляров
// расстановки (Material::enablePlacedInstances). Расчёт и отрисовка всех наборов переключаются клавишами 3 и 4
class Scatterer : public SceneObject
{
public:
	explicit Scatterer( const std::string& name );

	bool Initialize();
	void setTerrain( const TerrainHeightSource* terrain );
	// Модель слоя (вариант растения), её вес — доля ячеек слоя — и тень: вариант отбрасывает её, если она включена
	// у слоя (как Cast Shadow в дескрипторе Mesh Entry у PCG UE: тень только у крупных вариантов дешевле)
	struct LayerModel
	{
		DMModel* model = nullptr;
		float weight = 1.0f;
		bool castShadow = true;
	};
	// models — варианты растения (все их LOD, не больше ScatterPass::maxLods; вариантов — не больше
	// ScatterPass::maxVariants); mask — маска плотности в хранилище текстур, в координатах карты высот террейна
	bool addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterLayerSettings& settings );

	// Раскладка по видам кадра (FrameContext::views: главный и каскады теней)
	void compute( const FrameContext& frame ) override;
	// Свой вызов в проходах, где есть слои их режима материала
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	// Импостеры вариантов слоёв с impostor_distance: материал на модель (общий для наборов — по имени в хранилище)
	bool bake( const BakeContext& context ) override;

	void setComputeEnabled( bool enabled );
	bool computeEnabled() const;

	// Окно набора в GUI: «Shadow LOD scale» — множитель дальностей LOD у видов теней (< 1 — тень более грубым LOD, без
	// дизеринга перехода); по слою — «Cast shadow» (начальное значение — ScatterLayers.cast_shadow)
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

	// cbuffer FrustumBuffer в Shaders\scatter.cs: виды кадра
	struct alignas( 16 ) FrustumParams
	{
		DirectX::XMFLOAT4 planes[maxRenderViews * 6];
		DirectX::XMFLOAT4 viewParams[maxRenderViews];	// x — множитель дальностей LOD вида, y — 1: списки перехода у вида, zw — полоса каскада
		DirectX::XMFLOAT4 viewDepths[maxRenderViews];	// у каскада: xy — полоса глубины взгляда (cascadeNear, cascadeDepthFar)
		DirectX::XMFLOAT4 shadowCast;	// xyz — куда идёт свет источника теней, w — длина тени на метр высоты вдоль луча (0 — теней нет)
		uint32_t viewCount;
		uint32_t padding[3];
	};

	// Секция LOD: меш со своим материалом
	struct LayerSection
	{
		DMModel::Section* section = nullptr;
		Material* material = nullptr;
	};

	// LOD растения: секции
	struct LayerLod
	{
		std::vector<LayerSection> sections;
	};

	// Вариант растения — модель слоя
	struct LayerVariant
	{
		std::vector<LayerLod> lods;
		DMModel* model = nullptr;
		// Импостер — последний LOD (lods.back()): его секция — карточка, материал — после bake
		bool impostor = false;
		std::unique_ptr<DMModel::Section> impostorSection;
		bool castShadow = true;
		// Смена LOD дизерингом: у всех секций материал с DitheredLODTransition; обновляет compute() — флаг материала
		// меняется в GUI
		bool ditheredLodTransition = false;
	};

	// Группа секций списков с одним материалом, параметрами и состоянием: один ExecuteIndirect на вид
	// (ScatterPass::groups). Пересобирается каждый кадр в compute — материалы и флаги меняются в GUI
	struct LayerGroup
	{
		Material* material = nullptr;
		const PropertyContainer* params = nullptr;
		uint64_t paramsHash = 0;
		MaterialRenderState state;
		MeshPass pass = MeshPass::opaque;
		bool transition = false;	// список перехода: пул перехода и вариант шейдера LOD_DITHER
		bool prepassed = false;		// рисуется в depth prepass
		bool castShadow = false;	// рисуется в каскады теней
	};

	struct Layer
	{
		std::vector<LayerVariant> variants;
		std::vector<LayerGroup> groups;
		std::string mask;
		std::unique_ptr<ScatterPass> pass;
		bool placed = false;		// постоянный слой разложен на карту (раз, в первом compute)
		// Наибольшая высота экземпляра (границы моделей × размер слоя), м: длина его тени — запас дальности каскада
		float maxHeight = 1.0f;
		std::unique_ptr<PropertyContainer> properties;	// адрес не меняется при росте m_layers: его хранит GUI
	};

	// Секция варианта отбрасывает тень: флаги слоя и варианта и вариант её материала «только глубина»
	bool castsShadow( const Layer& layer, const LayerVariant& variant, const LayerSection& section ) const;
	// Секция рисуется в depth prepass: непрозрачная или Masked с вариантом «только глубина»
	bool inDepthPrepass( const LayerSection& section ) const;
	// Группы секций списков слоя и таблица секций для команд
	void assignGroups( Layer& layer );
	// Отпечаток параметров материала: секции с равными параметрами рисуются одной группой
	static uint64_t hashParams( const PropertyContainer& params );

	// Больше потоков на слой не запускаем: при мелком шаге сетка покроет не всё кольцо, а только его середину
	static constexpr uint16_t maxGridDim = 1024;

	bool m_computeEnabled = true;
	float m_shadowLength = 0.0f;	// длина тени на метр высоты инстанса в этом кадре — запас дальности каскада
	const TerrainHeightSource* m_terrain = nullptr;
	std::vector<Layer> m_layers;
	DMComputeShader m_computeShader;
	DMComputeShader m_commandShader;	// buildCommands в Shaders\scatter.cs
	// Постоянные слои: раскладка на карту (placeWorld) и отбор кластеров кадра (cullPlaced) — с первым таким слоем
	DMComputeShader m_placeShader;
	DMComputeShader m_cullShader;
	bool m_persistentShaders = false;
	Buffer m_terrainBuffer;
	Buffer m_frustumBuffer;
	PropertyContainer m_properties;
};

}
