#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include "SceneObject.h"
#include "TerrainMaterial.h"
#include "TerrainHeightSource.h"
#include "TerrainHydrology.h"
#include "TerrainMasks.h"
#include "TerrainWind.h"
#include "TerrainCanopy.h"
#include "TerrainPaint.h"
#include "TerrainDetailSplat.h"

namespace DirectX
{
struct Image;
}

namespace GS
{

struct TerrainEditCoverage;

// Конвейер рельефа и воды — данные рельефа уровня отдельно от его отрисовки (CDLODTerrain). Ступени при загрузке:
// исходная карта высот → эрозия (TerrainErosion, кэш eroded\) → ручные правки (TerrainEdits) → кривые, русла, ручьи и озёра
// по итоговому рельефу (TerrainHydrology) → детальная земля у русел → слои и маски по рельефу (TerrainMasks, кэш generated\)
// → материал слоёв (TerrainMaterial: splat-карта по маскам, правкам, руслам и мазкам кисти) и детальный слой весов
// (TerrainDetailSplat); поле ветра (TerrainWind) — после моделей уровня (buildWindField). Результаты: итоговая карта на CPU
// (запросы высоты: ходьба, луч кисти, пересадка моделей) и на GPU с мипами, детальные плитки, маска очистки растительности,
// маски — текстуры хранилища по имени, поле ветра; ресурсы сцены привязывает compute. Их читают отрисовка рельефа,
// расстановка, вода, частицы, следы в траве и тень рельефа (TerrainHeightSource, hydrology). Правки окон (правила слоёв,
// ветер, кривые русел) и мазки кисти (данные мазков — здесь, кисть — TerrainPaintTool) перестраивают нужные ступени без
// перезагрузки. Окна — части окна террейна (attachWindow).
// Настройки — строка Terrain (карта высот, смещение, шаг) и слои TerrainLayers
class TerrainPipeline : public SceneObject, public TerrainHeightSource
{
public:
	TerrainPipeline();

	// terrain — строка Terrain уровня и слои его материала (LibraryLoader: конвейер о базе не знает), edits — правки рельефа уровня по
	// порядку слоёв (накладываются на копию карты высот при загрузке: её читают вершины, узлы квадродерева и всё, что стоит на
	// рельефе; их очистка растительности — маска для расстановки, покраска — в splat-карту материала). water — вода уровня:
	// после правок по итоговому рельефу конвейер (TerrainHydrology) режет русла и строит ручьи и маску озёр; nullptr — воды
	// нет. erosion — эрозия исходной карты до правок (TerrainErosion, кэш — каталог eroded\ рядом с картой высот); nullptr —
	// карта уже готова. layerRules — правила слоёв и масок по рельефу (TerrainMasks)
	bool initialize( const TerrainDescription& terrain, const std::vector<TerrainEdit>& edits = {},
					 const WaterSimulationSettings* water = nullptr, const TerrainErosionSettings* erosion = nullptr,
					 const TerrainLayerRules& layerRules = {} );
	// Части окна террейна: слои материала («Layer tiling», «Layer tint», «Layer displacement», «Layer occlusion», «Layer rotation»), «Erosion»,
	// «Layer rules» — подокна window; мазки кисти — данные уровня: отмечают window изменённым до «Save level»
	void attachWindow( PropertyContainer& window );
	// Множитель высоты из окна террейна (Terrain.height_multiplier) — вживую: запросы высоты, ресурсы и отрисовка; ступени
	// (русла, ветер) берут его при следующей перестройке
	void setHeightMultiplier( float multiplier ) { m_heightMultiplier = multiplier; }
	// Для сохранения уровня: мазки кисти, tiling, оттенок, дисплейсмент и AO строк TerrainLayers, эрозия и правила слоёв
	// (строка Terrain — у окна террейна, CDLODTerrain::settings)
	void settings( TerrainSettings& settings ) const;

	// Господствующий ветер — куда дует (WindSettings::direction; y не учитывается): по нему поле ветра, а по его рельефной
	// части — снег в правилах слоёв. До initialize; после — сменился (changed) — слои и маски с полем заново перестраивает Scene
	void setPrevailingWind( const DirectX::XMFLOAT3& direction );
	bool prevailingWindChanged( const DirectX::XMFLOAT3& direction ) const;
	// Поле ветра по рельефу (TerrainWind, docs/wind.md): настройки (WindSettings::field) — до buildWindField; сменились
	// (changed) — rebuildWindField, а сменились настройки рельефной части (windTerrainChanged: поток и тень — по ним снег) —
	// rebuildMasks. buildWindField — после масок и моделей уровня (obstacles — экземпляры выше нескольких метров), его зовёт
	// Scene; rebuildMasks и rebuildChannels пересчитывают поле сами. Текстура — ресурс сцены SLOT_WIND_FIELD
	// (Shaders/wind_field.sh), копия на CPU — windField()
	void setWindField( const WindFieldSettings& settings );
	bool windFieldChanged( const WindFieldSettings& settings ) const;
	bool windTerrainChanged( const WindFieldSettings& settings ) const;
	bool buildWindField( const std::vector<WindObstacle>& obstacles );
	bool rebuildWindField();
	const std::vector<TerrainWind::Cell>& windField() const { return m_wind; }
	bool hasWindField() const { return m_windView.valid(); }
	// Карта полога (TerrainCanopy) по кругам крон — на GPU ресурсом сцены SLOT_CANOPY (до неё — пустая); false — текстура не
	// создана
	bool setCanopy( const std::vector<CanopyStamp>& stamps );

	// Мазки кисти поверх правил слоёв и масок (TerrainPaint, строки TerrainPaint) — до initialize; применяются в buildMasks
	void setPaint( const std::vector<TerrainPaintTile>& tiles ) { m_paintTiles = tiles; }
	// Мазок кисти по частям (кисть — TerrainPaintTool): начало — цель «layer N», «wetness» (тонкая сетка 0,25 м: детальный
	// слой весов) или имя маски (1 м), касание каждый кадр (цель — на GPU вживую: маска сразу, плитки детального слоя весов —
	// раз в кадр (update), splat-карта — не чаще 10 раз в секунду), конец — что задето (пусто — ничего), полог поля ветра и
	// раскладка леса. false в начале — нет цели или правил (reason)
	bool beginPaintStroke( const std::string& target, TerrainPaint::Mode mode, float value, std::string& reason );
	void paintDab( float x, float z, float radius, float falloff );
	TerrainPaint::Rect endPaintStroke();
	bool painting() const { return m_paint.stroking(); }
	// Отмена и повтор мазка: плитки цели целиком
	void setPaintTiles( const std::string& target, const TerrainPaint::TileMap& tiles );
	// Цели кисти: «layer N» по TerrainLayers, мокрость, затем маски
	std::vector<std::string> paintTargets() const;
	// Луч к рельефу (итоговая карта на CPU, как ходьба): distance — до точки на земле; false — мимо
	bool raycast( const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float maxDistance, float& distance ) const;
	bool clearPaint( const std::string& target );
	const TerrainPaint& paint() const { return m_paint; }
	// Итог цели в точке: правила, поправка, итог (для проверки команд); false — нет цели
	bool paintProbe( const std::string& target, float x, float z, float& rule, float& delta, float& result ) const;
	// Маска по имени (mask_grass, mask_forest_* …, TerrainMasks) с мазками кисти: mapSize² значений, строка 0 — дальний край по z,
	// как у карты высот; nullptr — такой нет. Указатель — до перестройки масок или русел: брать заново каждый кадр
	const std::vector<float>* maskValues( const std::string& name ) const;
	// Имена масок по порядку (mask_grass, mask_forest_* …) — флажки мест певцов в окне «Soundscape»
	std::vector<std::string> maskNames() const;
	// Номер правки масок: растёт с каждой выгрузкой маски на GPU (правила заново, мазок кисти) — сборные копии масок (места
	// звука на земле, SoundAreaOverlay) сверяют его
	uint32_t maskRevision() const { return m_maskRevision; }
	// Мазок изменил маски: постоянные слои расстановки (лес) раскладываются заново — флаг для Scene, сбрасывается
	bool takePaintedMasks() { const bool changed = m_paintedMasksChanged; m_paintedMasksChanged = false; return changed; }

	// Правили «Layer rules» в редакторе — слои и маски заново (без русел), splat-карта с покраской правок; расстановку (лес)
	// перестраивает Scene. false — правил не меняли или пересчёт не удался
	bool layerRulesChanged() const;
	bool rebuildMasks();
	// Рельеф после эрозии сменился, а пересадка моделей на него ещё не сохранена (eroded\\previous.dds — прежний рельеф):
	// модели уровня пересаживаются при каждой загрузке, пока «Save level» не закрепит (confirmErosionChange).
	// Разница «самой высокой точки земли под моделью» (круг radius вокруг x, z) между новым и прежним рельефом после эрозии, м
	bool erosionChanged() const { return !m_previousEroded.empty(); }
	float erosionShift( float x, float z, float radius ) const;
	// Пересадка сохранена в базе: прежний рельеф больше не нужен
	void confirmErosionChange();
	void releaseErosionChange() { m_previousEroded.clear(); m_previousEroded.shrink_to_fit(); m_eroded.clear(); m_eroded.shrink_to_fit(); }
	// Русла, ручьи, растр статичной воды и приток — для WaterSimulation; nullptr — у уровня нет воды
	const TerrainHydrology::Result* hydrology() const { return m_hasHydrology ? &m_hydrology : nullptr; }
	// Русла уровня в R32_FLOAT DDS (команда terrain hydrology): <prefix>_lowering.dds — врез русел, м; _level.dds — уровень
	// ручьёв, м; _lake.dds — уровень озёр, м (−1e9 — озера нет); _detail.dds — детальная земля у русел, м, по 4 отсчёта на
	// тексель карты высот (−1e9 — плитки нет). false — воды у уровня нет или файл не записался (reason)
	bool exportHydrology( const std::string& prefix, std::string& reason ) const;
	// Слой материала каждой роли (TerrainLayers.role), −1 — нет: по ним фильтр слоёв земли у расстановки
	const std::array<int32_t, terrainLayerRoleCount>& layersOfRoles() const { return m_layerOf; }
	// Слой роли, если он есть в массивах материала; иначе −1
	int32_t roleLayer( TerrainLayerRole role ) const
	{
		const int32_t layer = m_layerOf[static_cast<uint32_t>( role )];
		return layer < static_cast<int32_t>( m_material.layerCount() ) ? layer : -1;
	}
	// Кривые русел, по которым построены русла; сгенерированы заново при этой загрузке — их пишет в базу Scene
	const std::vector<StreamCurve>& streamCurves() const { return m_streamCurves; }
	bool streamsRegenerated() const { return m_streamsRegenerated; }
	const std::string& streamsKey() const { return m_streamsKey; }
	// Русла заново по кривым без перезагрузки уровня (правка кривой или параметров русел в редакторе): рельеф после эрозии
	// и ручных правок — из памяти, затем русла, карта высот с мипами, маска очистки растительности и покраска splat-карты
	// (границы узлов рельефа — по новому номеру карты, CDLODTerrain::update). Воду, расстановку и частицы перестраивает Scene
	bool rebuildChannels( const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves );

	// Карта высот и её масштаб: по ним стоят расстановка травы и декора и частицы
	TerrainHeight terrainHeight() const override;
	bool surfaceHeight( float x, float z, float& height ) const override;
	const float* cpuHeights() const override { return m_cpuHeights.empty() ? nullptr : m_cpuHeights.data(); }
	// Детальные плитки у русел — квадраты 0,25 м (TerrainHydrology::DetailTile)
	std::vector<FineRegion> fineRegions() const override;
	// Уровень воды static на сетке 1 м (computeWaterLevel) в точке, как waterDepthAt
	bool waterLevel( float x, float z, float& level ) const override;

	// Для отрисовки рельефа (CDLODTerrain): размеры, итоговая карта и её мипы на CPU (границы узлов), детальные плитки,
	// материал слоёв, вода и правила дна
	float worldSize() const { return m_worldSize; }
	float texelSize() const { return m_texelSize; }
	float heightMultiplier() const { return m_heightMultiplier; }
	float heightOffset() const { return m_heightOffset; }
	uint32_t mapSize() const { return m_texelSize > 0.0f ? static_cast<uint32_t>( std::lround( m_worldSize / m_texelSize ) ) : 0; }
	const ShaderView& heightMapView() const { return m_heightMap; }
	// Мипы итоговой карты (те же, что у текстуры): нормированные высоты строками сверху вниз, мип i — size² значений
	struct HeightMip
	{
		uint32_t size = 0;
		std::vector<float> values;
	};
	const std::vector<HeightMip>& heightMips() const { return m_heightMips; }
	// Номер итоговой карты: растёт с каждой новой (загрузка, перестройка русел) — TerrainHeight::revision
	uint32_t heightRevision() const { return m_heightRevision; }
	// Детальная плитка (x, z) — по сетке листов квадродерева (detailTileCells текселей): границы нормированной высоты; nullptr —
	// плитки нет
	const DirectX::XMFLOAT2* detailBounds( uint32_t x, uint32_t z ) const;
	float detailTileSize() const { return m_detailTileSize; }
	const TerrainMaterial& material() const { return m_material; }
	const TerrainDetailSplat& detailSplat() const { return m_detailSplat; }
	bool staticWater() const { return m_staticWater; }
	// Слой гальки русел — роль channel_bed (TerrainLayers.role), −1 — нет
	int32_t channelLayer() const { return m_channelLayer; }
	const TerrainLayerRules& layerRules() const { return m_layerRules; }
	// Запас берега озёр (WaterChannelsSettings::lakeFreeboard), м: вода ниже перелива на столько — полоса берега
	// (SLOT_LAKE_SHORE); 0 — полосы нет (вода не static)
	float lakeFreeboard() const { return m_lakeFreeboard; }

	// Мазок тонкой сетки — плитки детального слоя весов раз в кадр, splat-карта — не чаще 10 раз в секунду; параметры слоёв
	// материала из окна
	void update( const FrameContext& frame ) override;
	// Ресурсы сцены кадра: детальная земля у русел (SLOT_TERRAIN_DETAIL, SLOT_TERRAIN_DETAIL_INDEX — её читают вершины и
	// нормаль террейна, расстановка и частицы, terrain_height.sh), поле ветра, слои материала и детальный слой весов
	// Поле ветра в константах кадра (cb_windFieldScale): 1 / сторона карты, если поле есть (иначе не трогает — 0)
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
	void compute( const FrameContext& frame ) override;
	// Мешей нет: рельеф рисует CDLODTerrain
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override {}

private:
	// Карта высот, смещение и шаг, каталоги кэшей, роли слоёв (TerrainLayers.role) — из строки Terrain уровня
	void applyDescription( const TerrainDescription& terrain );
	// Эрозия карты field (нормированные высоты) — из кэша eroded\ или на GPU с записью кэша и карт эрозии
	bool erode( HeightField& field, const TerrainErosionSettings& erosion );
	// Подокна: слои материала (метров на повтор, оттенок, дисплейсмент, AO по слоям TerrainLayers), «Erosion» (параметры
	// эрозии, применяются при следующей загрузке), «Layer rules» (пороги правил слоёв и масок; пересчёт вживую —
	// layerRulesChanged / rebuildMasks) и правила по его свойствам
	void addLayerProperties();
	void addErosionProperties( const TerrainErosionSettings& erosion );
	void addLayerRulesProperties( const TerrainLayerRules& rules );
	TerrainLayerRules layerRulesFromProperties() const;
	// Копия карты высот с правками рельефа, затем русла, слои и маски и итоговая карта (finishHeights); coverage — что правки
	// делают, кроме высоты
	bool buildHeights( const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water, TerrainEditCoverage& coverage );
	// Русла по m_streamCurves, слои и маски, копия на CPU, карта высот с мипами на GPU и на CPU — по карте image (R32, после
	// правок)
	bool finishHeights( const DirectX::Image& image, const WaterSimulationSettings* water, TerrainEditCoverage& coverage );
	// Слои и маски по итоговому рельефу (TerrainMasks): веса splat-карты — m_masks, маски — текстуры хранилища по имени,
	// копия — файлы в m_generatedDirectory
	bool buildMasks( const HeightField& finalField );
	// Правила с мазками: m_painted — копия m_masks с поправками целей (пересчёт цели в прямоугольнике или всего); маски и
	// splat-карта на GPU берутся отсюда (paintedMasks)
	const TerrainMasks& paintedMasks() const { return m_paintActive ? m_painted : m_masks; }
	void refreshPainted();
	void refreshPaintedTarget( const std::string& target, const TerrainPaint::Rect& rect );
	// Маска — на GPU: прямоугольник rect участком текстуры (касание кисти), без него — целиком; текстуры ещё нет — создаётся
	bool uploadMask( const TerrainMasks::Mask& mask, const TerrainPaint::Rect* rect = nullptr );
	// Цель после мазка, отмены, повтора: на GPU; маска — флаг раскладки леса, лес — полог поля ветра; цель тонкой сетки —
	// плитки детального слоя весов в прямоугольнике changed её сетки (нет плиток — создаются; нет прямоугольника — все) и
	// splat-карта
	void paintTargetChanged( const std::string& target, const TerrainPaint::Rect* changed = nullptr );
	// Детальный слой весов (TerrainDetailSplat): откуда веса; все плитки заново (русла, правила) — и плитки мазков тонкой сетки
	TerrainDetailSplat::Source detailSplatSource() const;
	bool buildDetailSplat();
	// Мазки тонкой сетки — на GPU: плитки, задетые с прошлого раза (m_dirtySplatTiles), и splat-карта — не чаще 10 раз в
	// секунду (force — сейчас)
	void flushPaint( bool force );
	// Splat-карта 1 м: правила → покраска правками рельефа и руслами → мазки тонкой сетки в среднем по клетке; мокрость — мазки
	TerrainMaterial::SplatWeights baseSplat() const;
	bool repaintSplat();
	// Уровень поверхности воды static на сетке 1 м, как mainStatic и mainSurface (water_simulation.cs, water_surface.cs): дно и
	// мокрость плиток мазков вне детальной земли (TerrainDetailSplat)
	void computeWaterLevel();
	// Берег озёр на сетке 1 м (m_lakeShore): уровень воды озера у клеток озера и полосы берега до перелива (уровень + запас
	// берега) — от клеток озера вширь по соседям, пока земля ниже перелива, и ещё на клетку; −1e9 — не у озера. По нему
	// расстановка не сажает траву луга в полосе и сажает рогоз (docs/scatter.md, «Вода»); на GPU — ресурс сцены SLOT_LAKE_SHORE
	void computeLakeShore();
	bool createLakeShoreTexture();
	// Рельефная часть поля ветра (поток и подветренная тень, без леса) по итоговому рельефу field: ключ и расчёт, если ключ
	// сменился (m_windTerrain). Её читают правила слоёв (снег) и итоговое поле
	uint64_t windTerrainKey( const HeightField& field, TerrainField& metres ) const;
	bool ensureWindTerrain( const HeightField& field );
	// Значения растровой правки (TerrainEdit::raster) из файла; false — файла нет или он не R32_FLOAT (строка в лог)
	static bool loadEditRaster( TerrainEdit& edit );
	// Маска очистки растительности правками (R8_UNORM размером с карту высот, без правок с очисткой — 1 × 1 ноль)
	bool createFoliageClearMask( const TerrainEditCoverage& coverage );
	// Детальная земля из плиток конвейера (m_hydrology.detailTiles): массив и индекс на GPU, копия на CPU; без плиток —
	// пустой индекс 1 × 1 (ресурсы сцены привязаны всегда)
	bool createDetailTiles();
	// Нормированная высота самой детальной земли в точке мира, билинейно: плитка или карта высот (как sampleFineHeight)
	float fineHeight( float x, float z ) const;

private:
	std::string m_heightMapName;	// карта высот в хранилище текстур
	float m_worldSize = 0.0f;
	float m_texelSize = 1.0f;
	float m_heightOffset = 0.0f;
	float m_heightMultiplier = 1.0f;
	PropertyContainer* m_window = nullptr;	// окно террейна (attachWindow): мазки отмечают его изменённым
	// Итоговая карта (с правками рельефа) на CPU — значения 0…1 строками сверху вниз, как текстура: высота для ходьбы
	std::vector<float> m_cpuHeights;
	uint32_t m_cpuSize = 0;
	std::vector<HeightMip> m_heightMips;	// та же карта с мипами, как у текстуры: границы узлов рельефа
	TerrainHydrology::Result m_hydrology;	// конвейер рельефа и воды: русла по итоговому рельефу
	bool m_hasHydrology = false;
	std::vector<StreamCurve> m_streamCurves;
	// Основа перестройки русел: рельеф после эрозии и ручных правок (нормированный, m_baseSize²) и покрытие правок
	std::vector<float> m_baseHeights;
	uint32_t m_baseSize = 0;
	TerrainEditCoverage m_baseCoverage;
	std::string m_streamsKey;
	bool m_streamsRegenerated = false;
	std::string m_erodedDirectory;			// кэш и карты эрозии: Textures\<каталог карты высот>\eroded
	std::string m_generatedDirectory;		// копия слоёв и масок правил: Textures\<каталог карты высот>\generated
	// Карты эрозии (размыв, отложения, осыпь) — входы правил слоёв; без эрозии — пустые
	std::vector<float> m_erosionWear;
	std::vector<float> m_erosionDeposition;
	std::vector<float> m_erosionTalus;
	TerrainPaint m_paint;
	std::vector<TerrainPaintTile> m_paintTiles;	// мазки из базы — до первой сборки масок
	TerrainMasks m_painted;
	bool m_paintActive = false;					// есть мазки — m_painted
	bool m_paintedMasksChanged = false;
	std::string m_strokeTarget;					// цель идущего мазка
	bool m_splatPending = false;				// мазки тонкой сетки сменились — splat-карта заново
	std::set<TerrainDetailSplat::TileKey> m_dirtySplatTiles;	// плитки детального слоя весов, задетые мазком с прошлой выгрузки
	std::chrono::steady_clock::time_point m_splatUploaded;
	std::vector<float> m_waterLevel;			// уровень воды static на сетке 1 м (computeWaterLevel), −1e30 — нет
	std::vector<float> m_lakeShore;				// уровень озера у клеток озера и полосы берега (computeLakeShore), −1e9 — нет
	float m_lakeFreeboard = 0.0f;				// запас берега озёр, м (0 — не static)
	Texture m_lakeShoreTexture;
	ShaderView m_lakeShoreView;
	TerrainMasks m_masks;					// веса слоёв и маски последней сборки (перестройка русел — заново)
	TerrainLayerRules m_layerRules;			// правила, по которым они собраны
	mutable uint64_t m_layerRulesRevision = ~0ull;	// номер правки «Layer rules», по которому сверены правила (layerRulesChanged)
	DirectX::XMFLOAT2 m_prevailingWind = DirectX::XMFLOAT2( 0.0f, 0.0f );	// куда дует ветер, X и Z (setPrevailingWind)
	PropertyContainer m_layerRulesProperties;	// «Layer rules»
	// Подокна «Layer rules» по группам полей (SettingField::group у terrainLayerRulesSchema): имя группы и подокно
	std::vector<std::pair<std::string, std::unique_ptr<PropertyContainer>>> m_layerRuleGroups;
	std::array<int32_t, terrainLayerRoleCount> m_layerOf{ -1, -1, -1, -1, -1, -1, -1, -1 };	// слой каждой роли (TerrainLayers.role)
	TerrainEditCoverage m_coverage;			// покрытие правок рельефа и русел последней сборки — покраска при пересчёте правил
	std::optional<TerrainErosionSettings> m_erosion;
	PropertyContainer m_erosionProperties;	// «Erosion»
	// Рельеф после эрозии, м: прежний (из кэша до пересчёта) и новый — для пересадки моделей, пока она не сделана
	std::vector<float> m_previousEroded;
	std::vector<float> m_eroded;
	PropertyContainer m_layerProperties;	// «Layer tiling»: метров на повтор по слоям
	PropertyContainer m_layerTintProperties;	// «Layer tint»: множитель альбедо по слоям (TerrainLayers.albedo_tint)
	PropertyContainer m_layerDisplacementProperties;	// «Layer displacement»: размах смещения по слоям (TerrainLayers.displacement)
	PropertyContainer m_layerOcclusionProperties;		// «Layer occlusion»: сила AO по слоям (TerrainLayers.occlusion)
	PropertyContainer m_layerRotationProperties;		// «Layer rotation»: поворот ячеек hex-tiling по слоям (TerrainLayers.hex_rotation)
	Texture m_heightMapTexture;	// копия t_heightmap в R32_FLOAT с правками рельефа и полной цепочкой мипов
	ShaderView m_heightMap;
	uint32_t m_heightRevision = 0;	// TerrainHeight::revision: растёт с каждой новой итоговой картой
	uint32_t m_maskRevision = 0;	// растёт с каждой выгрузкой маски (uploadMask)
	float m_maxHeightValue = 0.0f;	// наибольшее значение итоговой карты (TerrainHeight::maxValue)
	Texture m_foliageClearTexture;
	ShaderView m_foliageClearMask;
	// Детальная земля у русел: массив плиток (x — нормированная высота, y — врез, м; поле в тексель) и индекс плитка → срез + 1; на CPU —
	// те же значения для ходьбы и границы плиток (min / max) для отбора узлов
	Texture m_detailTexture;
	ShaderView m_detailView;
	// Поле ветра: настройки, препятствия (модели уровня), рельефная часть и её ключ (полог пересчитывается без неё), итог на
	// CPU и текстура R16G16B16A16_FLOAT
	WindFieldSettings m_windSettings;
	std::vector<WindObstacle> m_windObstacles;
	std::vector<TerrainWind::Cell> m_windTerrain;
	uint64_t m_windTerrainKey = 0;
	std::vector<TerrainWind::Cell> m_wind;
	Texture m_windTexture;
	ShaderView m_windView;
	Texture m_canopyTexture;
	ShaderView m_canopyView;
	Texture m_detailIndexTexture;
	ShaderView m_detailIndexView;
	float m_detailTileSize = 0.0f;			// сторона плитки, м; 0 — плиток нет
	int32_t m_channelLayer = -1;			// слой гальки русел — роль channel_bed (TerrainLayers.role)
	bool m_staticWater = false;				// вода режима static: дно озёр красит шейдер по глубине в точке
	uint32_t m_detailSide = 0;				// текселей плитки по стороне (с полями)
	uint32_t m_detailTilesPerSide = 0;
	std::vector<int> m_detailIndex;			// плитка (x + z · m_detailTilesPerSide) → срез, −1 — нет
	std::vector<std::vector<float>> m_detailHeights;
	std::vector<DirectX::XMFLOAT2> m_detailBounds;
	TerrainMaterial m_material;
	TerrainDetailSplat m_detailSplat;		// веса слоёв 0,25 м у русел поверх splat-карты
	bool m_initialized = false;
};

}
