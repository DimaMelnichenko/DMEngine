#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <set>
#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainMaterial.h"
#include "TerrainHeightSource.h"
#include "TerrainHydrology.h"
#include "TerrainMasks.h"
#include "TerrainWind.h"
#include "TerrainPaint.h"
#include "TerrainDetailSplat.h"
#include "ShaderProgram.h"
#include "D3D\DMStructuredBuffer.h"

namespace DirectX
{
struct Image;
}

namespace GS
{

struct TerrainEditCoverage;

// Террейн CDLOD (Continuous Distance-Dependent Level of Detail, F. Strugar, 2009).
// Квадродерево над картой высот: корень покрывает весь террейн, лист — gridDim текселей.
// У каждого уровня свой диапазон расстояний, вдвое больше предыдущего; узел, до которого камера ближе
// диапазона следующего уровня, делится на четыре. Выбранные узлы рисуются одним инстансным вызовом общего
// патча, а морфинг в вершинном шейдере (Shaders\cdlod.vs) сглаживает переходы между уровнями. У камеры — ещё два уровня
// мельче листа (квад 0,5 и 0,25 м) везде, а высота вершины — из детальной плитки у русла (TerrainHydrology::DetailTile,
// Shaders\terrain_detail.sh: U-ложе, бровки) или из карты высот: квадродерево однородно, трещин нет, как у CDLOD.
// Материал — TerrainMaterial (Shaders\terrain.ps), настройки — строка таблицы Terrain в base.db3, на которую
// ссылается уровень, слои материала — таблица TerrainLayers
class CDLODTerrain : public SceneObject, public TerrainHeightSource
{
public:
	CDLODTerrain();

	// terrainId — строка таблицы Terrain, edits — правки рельефа уровня по порядку слоёв (накладываются на копию карты
	// высот при загрузке: её читают вершины, узлы квадродерева и всё, что стоит на рельефе; их очистка растительности —
	// маска для расстановки, покраска — в splat-карту материала). water — вода уровня: после правок по итоговому рельефу
	// конвейер (TerrainHydrology) режет русла и строит ручьи и маску озёр; nullptr — воды нет. erosion — эрозия исходной
	// карты до правок (TerrainErosion, кэш — каталог eroded\ рядом с картой высот); nullptr — карта уже готова. layerRules —
	// правила слоёв и масок по рельефу (TerrainMasks)
	// Господствующий ветер — куда дует (WindSettings::direction; y не учитывается): по нему поле ветра, а по его рельефной
	// части — снег в правилах слоёв. До initialize; после — сменился (changed) — слои и маски с полем заново перестраивает Scene
	void setPrevailingWind( const DirectX::XMFLOAT3& direction );
	bool prevailingWindChanged( const DirectX::XMFLOAT3& direction ) const;
	// Поле ветра по рельефу (TerrainWind, docs/wind.md): настройки (WindSettings::field) — до buildWindField; сменились
	// (changed) — rebuildWindField, а сменились настройки рельефной части (windTerrainChanged: поток и тень — по ним снег) —
	// rebuildMasks. buildWindField — после масок и моделей уровня (obstacles — экземпляры выше нескольких метров), его зовёт
	// Scene; rebuildMasks и rebuildChannels пересчитывают поле сами. Текстура — ресурс сцены SLOT_WIND_FIELD
	// (Shaders/wind_field.sh), копия на CPU — windField()
	// Мазки кисти поверх правил слоёв и масок (TerrainPaint, строки TerrainPaint) — до initialize; применяются в buildMasks
	void setPaint( const std::vector<TerrainPaintTile>& tiles ) { m_paintTiles = tiles; }
	// Мазок из одного касания (команда terrain paint, потом — кисть вьюпорта): цель — «layer N», «wetness» (тонкая сетка
	// 0,25 м: детальный слой весов) или имя маски (1 м); текстуры цели — сразу, полог поля ветра — в конце мазка. false — нет
	// цели или правил
	bool paintStroke( const std::string& target, TerrainPaint::Mode mode, float value, float x, float z, float radius, float falloff,
					  std::string& reason );
	// Мазок кисти по частям (вьюпорт): начало, касание каждый кадр (цель — на GPU вживую: маска сразу, плитки детального слоя
	// весов — раз в кадр (update), splat-карта — не чаще 10 раз в секунду), конец — снимки плиток цели до и после для отмены
	// (EditHistory) и раскладка леса
	bool beginPaintStroke( const std::string& target, TerrainPaint::Mode mode, float value, std::string& reason );
	void paintDab( float x, float z, float radius, float falloff );
	struct PaintEdit
	{
		std::string target;
		TerrainPaint::TileMap before;
		TerrainPaint::TileMap after;
		bool changed = false;
	};
	PaintEdit endPaintStroke();
	bool painting() const { return m_paint.stroking(); }
	// Отмена и повтор мазка: плитки цели целиком
	void setPaintTiles( const std::string& target, const TerrainPaint::TileMap& tiles );
	// Кисть подокна «Paint»: цель, режим (Shift — убавить в режиме «Step», Ctrl — стереть), значение, радиус, мягкость
	struct PaintBrush
	{
		std::string target;
		TerrainPaint::Mode mode = TerrainPaint::Mode::value;
		float value = 1.0f;
		float radius = 10.0f;
		float falloff = 0.5f;
	};
	PaintBrush paintBrush( bool shift, bool ctrl ) const;
	// Режим кисти во вьюпорте (View → «Terrain paint»): пока включён, цель кисти видна поверх земли, если в подокне «Paint»
	// отмечено «Show target»
	void setPaintPreview( bool enabled ) { m_paintPreview = enabled; }
	// Цели кисти: «layer N» по TerrainLayers, мокрость, затем маски
	std::vector<std::string> paintTargets() const;
	// Луч к рельефу (итоговая карта на CPU, как ходьба): distance — до точки на земле; false — мимо
	bool raycast( const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, float maxDistance, float& distance ) const;
	bool clearPaint( const std::string& target );
	const TerrainPaint& paint() const { return m_paint; }
	// Итог цели в точке: правила, поправка, итог (для проверки команд); false — нет цели
	bool paintProbe( const std::string& target, float x, float z, float& rule, float& delta, float& result ) const;
	// Мазок изменил маски: постоянные слои расстановки (лес) раскладываются заново — флаг для Scene, сбрасывается
	bool takePaintedMasks() { const bool changed = m_paintedMasksChanged; m_paintedMasksChanged = false; return changed; }
	void setWindField( const WindFieldSettings& settings );
	bool windFieldChanged( const WindFieldSettings& settings ) const;
	bool windTerrainChanged( const WindFieldSettings& settings ) const;
	bool buildWindField( const std::vector<WindObstacle>& obstacles );
	bool rebuildWindField();
	const std::vector<TerrainWind::Cell>& windField() const { return m_wind; }
	// Для констант кадра (cb_windFieldScale): 1 / сторона карты, если поле есть, иначе 0
	float windFieldScale() const { return m_windView.valid() && m_worldSize > 0.0f ? 1.0f / m_worldSize : 0.0f; }
	bool initialize( uint32_t terrainId, const std::vector<TerrainEdit>& edits = {}, const WaterSimulationSettings* water = nullptr,
					 const TerrainErosionSettings* erosion = nullptr, const TerrainLayerRules& layerRules = {} );
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
	// и ручных правок — из памяти, затем русла, карта высот с мипами, границы узлов, маска очистки растительности и
	// покраска splat-карты. Воду, расстановку и частицы перестраивает Scene
	bool rebuildChannels( const WaterSimulationSettings& water, const std::vector<StreamCurve>& curves );
	// Карта высот и её масштаб: по ним стоят расстановка травы и декора и частицы
	TerrainHeight terrainHeight() const override;
	bool surfaceHeight( float x, float z, float& height ) const override;
	const float* cpuHeights() const override { return m_cpuHeights.empty() ? nullptr : m_cpuHeights.data(); }

	void update( const FrameContext& frame ) override;
	// Детальная земля у русел — ресурс сцены кадра (SLOT_TERRAIN_DETAIL, SLOT_TERRAIN_DETAIL_INDEX): её читают вершины и
	// нормаль террейна, расстановка и частицы (terrain_height.sh)
	void compute( const FrameContext& frame ) override;
	// Выбор узлов квадродерева для вида: LOD — от точки LOD вида, отсечение — по его frustum
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	// Пайплайны рельефа: цвет с растеризатором кадра (сплошной или каркасный) в буфер сцены; prepass и тени — только глубина
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Настройки из окна — для сохранения уровня (строка Terrain и tiling строк TerrainLayers)
	TerrainSettings settings() const;

private:
	static constexpr uint32_t gridDim = 32;			// квадов в стороне узла
	static constexpr uint32_t patchDim = gridDim / 2;	// квадов в стороне патча: узел рисуется четырьмя патчами-четвертями
	static constexpr uint32_t maxLevels = 16;
	static constexpr uint32_t maxPatches = 8192;
	static constexpr float morphStartRatio = 0.66f;	// с какой доли диапазона уровня начинается морфинг
	// Уровни мельче листа: до шага детальной плитки (tileLevels: 0,5 и 0,25 м) и ещё четыре — под дисплейсмент (0,125,
	// 0,0625, 0,03125 и 0,015625 м у камеры); сколько из них рисовать — «Finest mesh» (m_meshLevels), радиус самого мелкого —
	// «Finest mesh radius»
	static constexpr uint32_t detailLevels = 6;
	static constexpr uint32_t tileLevels = 2;
	static_assert( ( 1u << tileLevels ) == TerrainHydrology::detailSamples, "tile levels reach the detail tile step" );

	struct PatchInstance
	{
		DirectX::XMFLOAT2 origin;
		float size;
		float level;
	};

	// Константы слоёв (TerrainLayerConstants в Shaders/terrain_layer_constants.sh): выбор слоёв и дисплейсмент — ресурс сцены
	// SLOT_TERRAIN_LAYER_CONSTANTS для расстановки и частиц и копия в Parameters для шейдеров террейна; заполняет одна
	// layerConstants — смещение у всех одно
	struct LayerConstants
	{
		DirectX::XMFLOAT4 layerScale[TerrainMaterial::splatSlices];			// повторов текстуры на метр по слоям
		DirectX::XMFLOAT4 layerDisplacement[TerrainMaterial::splatSlices];	// размах дисплейсмента по слоям, м
		DirectX::XMFLOAT3 lodOrigin;	// точка LOD главного вида
		float lodEyeHeight;				// ноги ниже точки LOD, м (RenderView::lodEyeHeight): LOD — от отрезка «глаза — ноги»
		float worldSize;
		float triplanarSharpness;
		float heightBlendDepth;
		uint32_t layerCount;
		float splatDetailTile;			// сторона плитки детального слоя весов, м (TerrainDetailSplat); 0 — плиток нет
		float displacementStart;		// дисплейсмент: полный ближе, м, к displacementEnd — нет (0 — выключен)
		float displacementEnd;
		float padding;
		DirectX::XMFLOAT4 layerTint[TerrainMaterial::maxLayers];	// множитель альбедо по слоям (xyz)
	};
	static_assert( sizeof( LayerConstants ) == 240, "TerrainLayerConstants in Shaders/terrain_layer_constants.sh" );

	struct alignas( 16 ) Parameters
	{
		float worldSize;
		float heightMultiplier;
		float heightOffset;
		float gridDim;
		DirectX::XMFLOAT4 morphConsts[maxLevels];
		float texelSize;
		float farTextureScale;
		float farBlendStart;
		float farBlendEnd;
		uint32_t showWater;		// подсветка воды симуляции (SLOT_WATER) — отладка, «Show water»
		float detailTile;		// сторона детальной плитки, м; 0 — детальной земли нет
		int32_t waterBedLayer;	// слой гальки под водой режима static — по глубине в точке (−1 — нет)
		uint32_t showWind;		// поле ветра на рельефе (SLOT_WIND_FIELD) — отладка, «Show wind»
		uint32_t wetnessBaked;	// мокрость от воды static — в плитках детального слоя весов (TerrainDetailSplat)
		// Цель кисти поверх земли (режим «Terrain paint», «Show target»): −1 — нет, 0…7 — слой земли, 8 — мокрость, 9 — маска
		// (t7 пиксельного шейдера)
		int32_t paintPreview;
		float microShadows;			// микротени прямого света по AO слоёв, 0…1 («Micro shadows»)
		// Наносы на дне озёр (правила «Water beds», TerrainBedRules): слой дресвы и ила (−1 — нет), глубина воды
		// перехода гальки к дресве и дресвы к илу (x, y и z, w), уровень озера, м мира: наносы — у озёр ниже (from, to)
		int32_t lakeGravelLayer;
		DirectX::XMFLOAT4 lakeBedDepths;
		DirectX::XMFLOAT2 lakeSedimentLevel;
		int32_t lakeSiltLayer;
		float bedPadding;
		DirectX::XMFLOAT4 detailMorph[detailLevels];	// уровни −1…−6: начало морфинга и 1 / длина зоны
		DirectX::XMFLOAT4 layerOcclusion[TerrainMaterial::splatSlices];		// сила AO по слоям, 0…1
		LayerConstants layers;	// та же раскладка, что у ресурса сцены (cbuffer дешевле у каждой вершины и пикселя)
	};


	struct NodeBox
	{
		DirectX::XMFLOAT3 min;
		DirectX::XMFLOAT3 max;
	};

	bool loadSettings( uint32_t terrainId, TerrainSettings& settings );
	// Эрозия карты field (нормированные высоты) — из кэша eroded\ или на GPU с записью кэша и карт эрозии
	bool erode( HeightField& field, const TerrainErosionSettings& erosion );
	// Подокно «Erosion»: параметры эрозии (применяются при следующей загрузке)
	void addErosionProperties( const TerrainErosionSettings& erosion );
	// Подокно «Layer rules»: пороги правил слоёв и масок (пересчёт вживую — layerRulesChanged / rebuildMasks) и правила по
	// его свойствам
	void addLayerRulesProperties( const TerrainLayerRules& rules );
	TerrainLayerRules layerRulesFromProperties() const;
	bool createShader();
	// Копия карты высот с правками рельефа и мипами для вершинного шейдера и минимум / максимум высоты каждого узла по
	// мипам его уровня; coverage — что правки делают, кроме высоты
	bool buildHeightBounds( const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water, TerrainEditCoverage& coverage );
	// Русла по m_streamCurves, слои и маски, копия на CPU, карта высот с мипами на GPU и границы узлов — по карте image
	// (R32, после правок)
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
	void addPaintProperties();
	// Рельефная часть поля ветра (поток и подветренная тень, без леса) по итоговому рельефу field: ключ и расчёт, если ключ
	// сменился (m_windTerrain). Её читают правила слоёв (снег) и итоговое поле
	uint64_t windTerrainKey( const HeightField& field, TerrainField& metres ) const;
	bool ensureWindTerrain( const HeightField& field );
	// Значения растровой правки (TerrainEdit::raster) из файла; false — файла нет или он не R32_FLOAT (строка в лог)
	static bool loadEditRaster( TerrainEdit& edit );
	// Маска очистки растительности правками (R8_UNORM размером с карту высот, без правок с очисткой — 1 × 1 ноль)
	bool createFoliageClearMask( const TerrainEditCoverage& coverage );
	void calcRanges();
	// Константы слоёв для вида: точка LOD — его (у видов теней — главного)
	LayerConstants layerConstants( const RenderView& view ) const;
	NodeBox nodeBox( uint32_t level, uint32_t x, uint32_t z ) const;
	float nodeSize( uint32_t level ) const;
	// false — узел дальше диапазона своего уровня, и его площадь рисует родитель
	bool selectNode( const RenderView& view, uint32_t level, uint32_t x, uint32_t z, std::vector<PatchInstance>& patches );
	void addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<PatchInstance>& patches );
	// Уровни мельче листа: depth 1 — узел вдвое меньше листа (−1), 2 — вчетверо (−2); nx, nz — узел в сетке этого уровня.
	// false — узел дальше диапазона своего уровня: его площадь рисует родитель
	bool selectDetailNode( const RenderView& view, uint32_t depth, uint32_t nx, uint32_t nz, std::vector<PatchInstance>& patches );
	void addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<PatchInstance>& patches );
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
	uint32_t m_terrainId = 0;
	// Итоговая карта (с правками рельефа) на CPU — значения 0…1 строками сверху вниз, как текстура: высота для ходьбы
	std::vector<float> m_cpuHeights;
	uint32_t m_cpuSize = 0;
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
	PropertyContainer m_paintProperties;		// подокно «Paint» — кисть
	std::string m_strokeTarget;
	TerrainPaint::TileMap m_strokeBefore;
	bool m_paintPreview = false;				// режим кисти во вьюпорте: цель кисти поверх земли
	bool m_splatPending = false;				// мазки тонкой сетки сменились — splat-карта заново
	std::set<TerrainDetailSplat::TileKey> m_dirtySplatTiles;	// плитки детального слоя весов, задетые мазком с прошлой выгрузки
	std::chrono::steady_clock::time_point m_splatUploaded;
	std::vector<float> m_waterLevel;			// уровень воды static на сетке 1 м (computeWaterLevel), −1e30 — нет
	TerrainMasks m_masks;					// веса слоёв и маски последней сборки (перестройка русел — заново)
	TerrainLayerRules m_layerRules;			// правила, по которым они собраны
	DirectX::XMFLOAT2 m_prevailingWind = DirectX::XMFLOAT2( 0.0f, 0.0f );	// куда дует ветер, X и Z (setPrevailingWind)
	PropertyContainer m_layerRulesProperties;	// «Layer rules»
	// Подокна «Layer rules» по группам полей (TerrainLayerRuleField::group): имя группы и подокно
	std::vector<std::pair<std::string, std::unique_ptr<PropertyContainer>>> m_layerRuleGroups;
	std::array<int32_t, terrainLayerRoleCount> m_layerOf{ -1, -1, -1, -1, -1, -1, -1, -1 };	// слой каждой роли (TerrainLayers.role)
	TerrainEditCoverage m_coverage;			// покрытие правок рельефа и русел последней сборки — покраска при пересчёте правил
	std::optional<TerrainErosionSettings> m_erosion;
	PropertyContainer m_erosionProperties;	// «Erosion»
	// Рельеф после эрозии, м: прежний (из кэша до пересчёта) и новый — для пересадки моделей, пока она не сделана
	std::vector<float> m_previousEroded;
	std::vector<float> m_eroded;
	PropertyContainer m_layerTintProperties;	// «Layer tint»: множитель альбедо по слоям (TerrainLayers.albedo_tint)
	PropertyContainer m_layerDisplacementProperties;	// «Layer displacement»: размах смещения по слоям (TerrainLayers.displacement)
	PropertyContainer m_layerOcclusionProperties;		// «Layer occlusion»: сила AO по слоям (TerrainLayers.occlusion)
	PropertyContainer m_layerProperties;	// «Layer tiling»: метров на повтор по слоям — подокно окна террейна
	uint32_t m_levelCount = 0;
	std::vector<uint32_t> m_nodesPerSide;
	std::vector<std::vector<DirectX::XMFLOAT2>> m_heightBounds;	// по уровням: нормированные min / max высоты узлов
	Texture m_heightMapTexture;	// копия t_heightmap в R32_FLOAT с правками рельефа и полной цепочкой мипов
	ShaderView m_heightMap;
	uint32_t m_heightRevision = 0;	// TerrainHeight::revision: растёт с каждой новой итоговой картой
	Texture m_foliageClearTexture;
	ShaderView m_foliageClearMask;
	float m_ranges[maxLevels] = {};
	DirectX::XMFLOAT4 m_morphConsts[maxLevels] = {};
	float m_detailRanges[detailLevels] = {};
	uint32_t m_meshLevels = detailLevels;		// уровней мельче листа рисуется («Finest mesh»): tileLevels…detailLevels
	DirectX::XMFLOAT4 m_detailMorph[detailLevels] = {};
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

	// Выбранные патчи на каждый вид кадра (RenderView::index): главный и каскады теней. Деление узлов — от lodOrigin (и
	// отрезка до ног — lodEyeHeight), общих у всех видов, frustum вида только отсекает, поэтому рельеф в тени и на экране один
	std::array<std::vector<PatchInstance>, maxRenderViews> m_patches;
	GridMesh m_patch;
	ShaderProgram m_shader;
	int m_materialPhase = 0;	// фазы m_shader: материал, раскраска по уровням LOD, только глубина (тени)
	int m_lodPhase = 0;
	int m_depthPhase = 0;
	TerrainMaterial m_material;
	TerrainDetailSplat m_detailSplat;		// веса слоёв 0,25 м у русел поверх splat-карты
	DMStructuredBuffer m_patchBuffer;
	DMStructuredBuffer m_layerConstants;	// LayerConstants кадра — ресурс сцены (compute)
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
