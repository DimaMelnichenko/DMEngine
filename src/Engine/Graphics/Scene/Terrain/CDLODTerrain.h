#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <optional>
#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainMaterial.h"
#include "TerrainHeightSource.h"
#include "TerrainHydrology.h"
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
	// карты до правок (TerrainErosion, кэш — каталог eroded\ рядом с картой высот); nullptr — карта уже готова
	bool initialize( uint32_t terrainId, const std::vector<TerrainEdit>& edits = {}, const WaterSimulationSettings* water = nullptr,
					 const TerrainErosionSettings* erosion = nullptr );
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
	// Уровней мельче листа: −1 и −2 (квад 0,5 и 0,25 текселя) — шаг детальной земли (TerrainHydrology::detailSamples)
	static constexpr uint32_t detailLevels = 2;
	static_assert( ( 1u << detailLevels ) == TerrainHydrology::detailSamples, "detail levels reach the detail tile step" );

	struct PatchInstance
	{
		DirectX::XMFLOAT2 origin;
		float size;
		float level;
	};

	struct alignas( 16 ) Parameters
	{
		float worldSize;
		float heightMultiplier;
		float heightOffset;
		float gridDim;
		DirectX::XMFLOAT4 morphConsts[maxLevels];
		DirectX::XMFLOAT4 layerScale[TerrainMaterial::splatSlices];
		float texelSize;
		float triplanarSharpness;
		float heightBlendDepth;
		float farTextureScale;
		float farBlendStart;
		float farBlendEnd;
		uint32_t layerCount;
		uint32_t showWater;		// подсветка воды симуляции (SLOT_WATER) — отладка, «Show water»
		float detailTile;		// сторона детальной плитки, м; 0 — детальной земли нет
		float padding[3];
		DirectX::XMFLOAT4 detailMorph[detailLevels];	// уровни −1, −2: начало морфинга и 1 / длина зоны
	};

	struct NodeBox
	{
		DirectX::XMFLOAT3 min;
		DirectX::XMFLOAT3 max;
	};

	bool loadSettings( uint32_t terrainId, TerrainSettings& settings, std::string& splatMap );
	// Эрозия карты field (нормированные высоты) — из кэша eroded\ или на GPU с записью кэша и карт эрозии
	bool erode( HeightField& field, const TerrainErosionSettings& erosion );
	// Подокно «Erosion»: параметры эрозии (применяются при следующей загрузке)
	void addErosionProperties( const TerrainErosionSettings& erosion );
	bool createShader();
	// Копия карты высот с правками рельефа и мипами для вершинного шейдера и минимум / максимум высоты каждого узла по
	// мипам его уровня; coverage — что правки делают, кроме высоты
	bool buildHeightBounds( const std::vector<TerrainEdit>& edits, const WaterSimulationSettings* water, TerrainEditCoverage& coverage );
	// Русла по m_streamCurves, копия на CPU, карта высот с мипами на GPU и границы узлов — по карте image (R32, после правок)
	bool finishHeights( const DirectX::Image& image, const WaterSimulationSettings* water, TerrainEditCoverage& coverage );
	// Значения растровой правки (TerrainEdit::raster) из файла; false — файла нет или он не R32_FLOAT (строка в лог)
	static bool loadEditRaster( TerrainEdit& edit );
	// Маска очистки растительности правками (R8_UNORM размером с карту высот, без правок с очисткой — 1 × 1 ноль)
	bool createFoliageClearMask( const TerrainEditCoverage& coverage );
	void calcRanges();
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
	std::optional<TerrainErosionSettings> m_erosion;
	PropertyContainer m_erosionProperties;	// «Erosion»
	// Рельеф после эрозии, м: прежний (из кэша до пересчёта) и новый — для пересадки моделей, пока она не сделана
	std::vector<float> m_previousEroded;
	std::vector<float> m_eroded;
	PropertyContainer m_layerProperties;	// «Layer tiling»: метров на повтор по слоям — подокно окна террейна
	uint32_t m_levelCount = 0;
	std::vector<uint32_t> m_nodesPerSide;
	std::vector<std::vector<DirectX::XMFLOAT2>> m_heightBounds;	// по уровням: нормированные min / max высоты узлов
	Texture m_heightMapTexture;	// копия t_heightmap в R32_FLOAT с правками рельефа и полной цепочкой мипов
	ShaderView m_heightMap;
	Texture m_foliageClearTexture;
	ShaderView m_foliageClearMask;
	float m_ranges[maxLevels] = {};
	DirectX::XMFLOAT4 m_morphConsts[maxLevels] = {};
	float m_detailRanges[detailLevels] = {};
	DirectX::XMFLOAT4 m_detailMorph[detailLevels] = {};
	// Детальная земля у русел: массив плиток (нормированные высоты, поле в тексель) и индекс плитка → срез + 1; на CPU —
	// те же значения для ходьбы и границы плиток (min / max) для отбора узлов
	Texture m_detailTexture;
	ShaderView m_detailView;
	Texture m_detailIndexTexture;
	ShaderView m_detailIndexView;
	float m_detailTileSize = 0.0f;			// сторона плитки, м; 0 — плиток нет
	uint32_t m_detailSide = 0;				// текселей плитки по стороне (с полями)
	uint32_t m_detailTilesPerSide = 0;
	std::vector<int> m_detailIndex;			// плитка (x + z · m_detailTilesPerSide) → срез, −1 — нет
	std::vector<std::vector<float>> m_detailHeights;
	std::vector<DirectX::XMFLOAT2> m_detailBounds;

	// Выбранные патчи на каждый вид кадра (RenderView::index): главный и каскады теней. Деление узлов — от lodOrigin,
	// общего у всех видов, frustum вида только отсекает, поэтому рельеф в тени и на экране один и тот же
	std::array<std::vector<PatchInstance>, maxRenderViews> m_patches;
	GridMesh m_patch;
	ShaderProgram m_shader;
	int m_materialPhase = 0;	// фазы m_shader: материал, раскраска по уровням LOD, только глубина (тени)
	int m_lodPhase = 0;
	int m_depthPhase = 0;
	TerrainMaterial m_material;
	DMStructuredBuffer m_patchBuffer;
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
