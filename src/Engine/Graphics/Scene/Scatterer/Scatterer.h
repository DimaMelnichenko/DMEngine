#pragma once

#include <memory>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "ComputeShader.h"
#include "Level\LevelSettings.h"
#include "Model/DMModel.h"
#include "ScattererPass.h"
#include "SceneObject.h"
#include "Terrain\TerrainCanopy.h"
#include "Terrain\TerrainHeightSource.h"
#include "Terrain\TerrainHydrology.h"
#include "Shaders\terrain_height_constants.h"
#include "Shaders\scatter_constants.h"

namespace GS
{

class ImpostorMaterial;
class ModelCollision;

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
// расстановки (Material::enablePlacedInstances). Слой камней русел (placement stream_stones) — постоянный, но раскладывает
// его не маска на GPU, а конвейер рельефа и воды (TerrainHydrology::Result::stones): модель — по весам, размер — по
// поперечнику камня, поворот — случайный; экземпляры загружаются в постоянный пул с CPU (ScatterPass::placeInstances).
// У экземпляров постоянных слоёв (лес, камни русел) — тела мира столкновений по простой коллизии модели варианта (ствол у
// елей, оболочка у камней; docs/physics.md): лес — по копии постоянного пула с GPU, камни — сразу с CPU. Кольцевые слои
// (трава, галька) — без тел. Расчёт и отрисовка всех наборов переключаются клавишами 3 и 4
class Scatterer : public SceneObject
{
public:
	// settings — строка ScatterSets: «Shadow LOD scale» из базы, id — для сохранения уровня
	Scatterer( const std::string& name, const ScatterSetSettings& settings );

	bool Initialize();
	void setTerrain( const TerrainHeightSource* terrain );
	// Слой материала террейна каждой роли (TerrainLayers.role, порядок TerrainLayerRole; −1 — нет) — до addLayer: по ним
	// фильтр слоёв земли у слоя (ScatterLayerSettings::layerFilter) и его флажки в окне
	void setGroundLayers( const std::vector<int32_t>& layersOfRoles );
	// Модель слоя (вариант растения), её вес — доля ячеек слоя — и тень: вариант отбрасывает её, если она включена
	// у слоя (как Cast Shadow в дескрипторе Mesh Entry у PCG UE: тень только у крупных вариантов дешевле)
	struct LayerModel
	{
		DMModel* model = nullptr;
		float weight = 1.0f;
		bool castShadow = true;
		uint32_t id = 0;	// строка ScatterLayerModels — куда пишет «Save level»
		std::vector<float> lodDistances;	// свои границы LOD модели у слоя, м (ScatterLayerModels.lod_distances); пусто — модели
		ScatterVariantPlacement placement;	// свои размер, разброс, наклон и группа куртин (NULL в базе — как у слоя)
	};
	// models — варианты растения (все их LOD, не больше ScatterPass::maxLods; вариантов — не больше
	// ScatterPass::maxVariants); mask — маска плотности в хранилище текстур, в координатах карты высот террейна
	bool addLayer( const std::vector<LayerModel>& models, const std::string& mask, const ScatterLayerSettings& settings );

	// Мир столкновений (формы моделей — ModelCollision): тела постоянных слоёв — после их раскладки
	void setCollision( ModelCollision* collision ) { m_collision = collision; }
	// Карта полога (TerrainCanopy): кроны постоянных слоёв с «Canopy» > 0 — радиус × «Canopy»; номер растёт, когда кроны
	// сменились (лес разложен заново, правка «Canopy») — тогда Scene собирает карту заново
	uint32_t canopyRevision() const { return m_canopyRevision; }
	void appendCanopy( std::vector<CanopyStamp>& stamps ) const;
	// Кольцо деревьев на дальних горах (слои с placement far_forest): дальняя граница и её исчезание, м — горам
	// (FarTerrain::setForestRing: тон леса — дальше); false — таких слоёв нет
	bool farForestRing( float& border, float& fade ) const;
	// Готовая копия постоянного пула с GPU — тела его экземпляров (прежние тела слоя — убираются)
	void update( const FrameContext& frame ) override;
	// Раскладка по видам кадра (FrameContext::views: главный и каскады теней)
	void compute( const FrameContext& frame ) override;
	// Свой вызов в проходах, где есть слои их режима материала
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	// Импостеры вариантов слоёв с impostor_distance: материал на модель (общий для наборов — по имени в хранилище)
	bool bake( const BakeContext& context ) override;
	// Правка материала растения (подокно «Materials»): импостеры, у чьих моделей изменились параметры секций LOD0, — кадры
	// заново (Scene::applyEditorChanges: когда ползунок отпущен, в начале кадра); контекст запекания — с загрузки
	void rebakeChangedImpostors();

	void setComputeEnabled( bool enabled );
	bool computeEnabled() const;

	// Окно набора в GUI: «Shadow LOD scale» — множитель дальностей LOD у видов теней (< 1 — тень более грубым LOD, без
	// дизеринга перехода); по слою — колонки ScatterLayers (шаг, кольцо, исчезание, размер, разброс, поворот, тень) и
	// подокна вариантов — вес и тень (ScatterLayerModels). Шаг, кольцо, размер и веса пересобирают буферы слоя, прочее
	// меняет константы раскладки; постоянный слой после любой правки раскладывается заново
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Набор и слои с настройками из GUI — для сохранения уровня (строки ScatterSets, ScatterLayers и ScatterLayerModels)
	ScatterSetSettings settings() const;
	std::vector<ScatterLayerRecord> layerRecords() const;
	// Земля или вода изменились без перезагрузки (русла перестроены): постоянные слои — раскладка заново
	void terrainChanged()
	{
		for( Layer& layer : m_layers )
			layer.placed = false;
	}
	// Камни русел конвейера (при загрузке и после перестройки русел): слои stream_stones раскладываются заново
	void setStreamStones( const std::vector<TerrainHydrology::Stone>& stones );

private:


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
		ImpostorMaterial* impostorMaterial = nullptr;	// после bake: затемнение низа — из окна слоя каждый кадр
		uint32_t modelLods = 0;	// LOD модели у варианта (без импостера): границ LOD модели — modelLods − 1
		bool castShadow = true;
		DirectX::BoundingBox box;	// границы LOD0 модели: по ним размер и посадка камня русла
		// Смена LOD дизерингом: у всех секций материал с DitheredLODTransition; обновляет compute() — флаг материала
		// меняется в GUI
		bool ditheredLodTransition = false;
		uint64_t bakedParamsHash = 0;	// параметры секций LOD0 модели, по которым запечён импостер (rebakeChangedImpostors)
		uint32_t id = 0;	// строка ScatterLayerModels
		std::unique_ptr<PropertyContainer> properties;	// «Weight», «Cast shadow», своя раскладка, кольца LOD — подокно слоя
		std::unique_ptr<PropertyContainer> prefersProperties;	// «Prefers»: флажок на роль слоя земли (предпочтение вида)
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
		std::unique_ptr<PropertyContainer> groundProperties;	// «Grows on»: флажок на роль слоя земли (фильтр слоёв)
		// «Materials»: экземпляры материалов растений слоя (общие с моделями, Assets::materialInstances) — правка вживую
		std::unique_ptr<PropertyContainer> materialProperties;
		// Применённые настройки: с ними сравниваются свойства окна. Варианты раскладки (веса, дальности LOD) и размеры
		// моделей — для пересборки буферов слоя при смене шага, кольца, размера и весов
		ScatterLayerSettings settings;
		std::vector<ScatterPass::Variant> passVariants;
		// Номер правки окна слоя (с вариантами, слоями земли и материалами, PropertyContainer::revision), по которому
		// применены настройки и сверены отпечатки импостеров: не сменился — сверять нечего
		uint64_t appliedRevision = ~0ull;
		uint64_t bakeRevision = ~0ull;
		// Тела экземпляров постоянного слоя в мире столкновений; ждём копию пула с GPU (раскладка в этом кадре)
		std::vector<uint32_t> bodies;
		bool bodiesPending = false;
		// Кроны постоянного слоя для карты полога (радиус — кроны LOD0 модели × размер экземпляра, без «Canopy»); ждём
		// копию пула с GPU
		std::vector<CanopyStamp> canopy;
		bool canopyPending = false;
	};

	// Константы раскладки из настроек слоя (ёмкости и число вариантов заполняет ScatterPass::createBuffers)
	void fillParams( ScatterPass::PopulateParams& params, const ScatterLayerSettings& settings ) const;
	// Фильтр слоёв земли — биты слоёв материала по ролям из layerFilter (бит 31 — фильтр включён), 0 — без фильтра
	uint32_t groundLayerMask( const ScatterLayerSettings& settings ) const;
	// Настройки слоя по свойствам окна (у прочих колонок — применённые)
	ScatterLayerSettings settingsFromProperties( const Layer& layer ) const;
	// Правки окна слоя — в раскладку: константы или новые буферы (rebuildPass)
	void applyProperties( Layer& layer );
	// Буферы слоя заново по layer.settings и layer.passVariants: прежние GPU отпустит, когда закончит кадры с ними
	bool rebuildPass( Layer& layer );
	static float maxHeight( const Layer& layer );
	// Наибольший радиус экземпляра (сфера LOD0 × размер слоя и варианта), м: запас границ кластера постоянного слоя
	static float maxRadius( const Layer& layer );
	// Слой камней русел: экземпляры по камням конвейера — в постоянный пул
	void placeStreamStones( Layer& layer );
	// Тела экземпляров постоянного слоя (прежние — убираются): по модели варианта, размер — масштаб
	void buildBodies( Layer& layer, const std::vector<ScatterPass::PlacedInstance>& instances );
	// Кроны постоянного слоя для карты полога: круг на экземпляр по границам LOD0 модели варианта
	void buildCanopy( Layer& layer, const std::vector<ScatterPass::PlacedInstance>& instances );
	// Ёмкость постоянного пула слоя: у камней русел — их наибольшее число, у прочих — экземпляр на ячейку мира
	static uint32_t placedCapacity( const ScatterLayerSettings& settings )
	{
		return settings.placement == ScatterPlacement::streamStones ? maxStreamStones : 0;
	}

	// Секция варианта отбрасывает тень: флаги слоя и варианта и вариант её материала «только глубина»
	bool castsShadow( const Layer& layer, const LayerVariant& variant, const LayerSection& section ) const;
	// Секция рисуется в depth prepass: непрозрачная или Masked с вариантом «только глубина»
	bool inDepthPrepass( const LayerSection& section ) const;
	// Группы секций списков слоя и таблица секций для команд
	void assignGroups( Layer& layer );
	// Отпечаток параметров материала: секции с равными параметрами рисуются одной группой
	static uint64_t hashParams( const PropertyContainer& params );
	// Отпечаток того, из чего запечён импостер модели: параметры секций LOD0 и настройки запекания слоя (размер кадра,
	// плотность, затенение)
	static uint64_t bakeSourceHash( DMModel& model, const ScatterLayerSettings& settings );
	// Параметры секций LOD0 модели правятся сейчас (ползунок тянут)
	static bool paramsEditing( DMModel& model );
	// Флажок «Visible» окна слоя: скрытый слой не раскладывается и не рисуется (и в тенях)
	static bool layerVisible( const Layer& layer );

	// Больше потоков на слой не запускаем: при мелком шаге сетка покроет не всё кольцо, а только его середину
	static constexpr uint16_t maxGridDim = 1024;
	// Камней русел в слое — не больше (постоянный пул на 3 МБ); камень утоплен в дно на эту долю высоты модели, наклонён
	// не больше чем на столько радиан
	static constexpr uint32_t maxStreamStones = 65536;
	static constexpr float stoneEmbed = 0.25f;
	static constexpr float stoneTilt = 0.3f;

	uint32_t m_setId = 0;				// строка ScatterSets — для сохранения уровня
	bool m_computeEnabled = true;
	float m_shadowLength = 0.0f;	// длина тени на метр высоты инстанса в этом кадре — запас дальности каскада
	const TerrainHeightSource* m_terrain = nullptr;
	std::vector<int32_t> m_groundLayers;	// слой материала каждой роли (setGroundLayers)
	std::vector<TerrainHydrology::Stone> m_streamStones;
	std::vector<Layer> m_layers;
	ModelCollision* m_collision = nullptr;
	uint32_t m_canopyRevision = 0;
	ComputeShader m_computeShader;
	ComputeShader m_commandShader;	// buildCommands в Shaders\scatter.cs
	// Постоянные слои: раскладка на карту (placeWorld) и отбор кластеров кадра (cullPlaced) — с первым таким слоем
	ComputeShader m_placeShader;
	ComputeShader m_cullShader;
	bool m_persistentShaders = false;
	Buffer m_terrainBuffer;
	Buffer m_frustumBuffer;
	PropertyContainer m_properties;
	// Контекст запекания с загрузки (константы и общий буфер вершин рендерера — живут, пока жив рендерер): перезапекание
	// импостеров по правке материала
	std::unique_ptr<BakeContext> m_bakeContext;
};

}
