#pragma once

#include <array>
#include <vector>
#include "Level\LevelSettings.h"
#include "SceneObject.h"
#include "GridMesh.h"
#include "CDLODQuadtree.h"
#include "FarForestRelief.h"
#include "TerrainHeightSource.h"
#include "ShaderProgram.h"
#include "ComputeShader.h"
#include "D3D\DMStructuredBuffer.h"
#include "Shaders\far_terrain_constants.h"

namespace GS
{

class CDLODTerrain;
class TerrainPipeline;

// Дальние горы вокруг карты уровня (docs/far_terrain.md): кольцо рельефа по своей грубой карте высот (строка FarTerrain,
// Tools/gen_far_terrain.py), как дальние Landscape вокруг игровой зоны в UE. Узлы — на той же сетке CDLOD, что у карты
// (CDLODQuadtree с дырой под карту), таблица дальностей — общая (у карты, CDLODTerrain::setFarLevels: ближние уровни — её
// дальности, с уровня, чей квад — тексель карты гор, — своя «LOD distance»), вершина — тот же код (Shaders/cdlod_patch.sh),
// высота у края карты — её высота (Shaders/far_terrain.sh): стык без трещин. Внутри карты в карту гор запекается её мип того же
// текселя (заново по новой итоговой карте): мипы гор у края не врут. Материал — правила по высоте и уклону на слоях террейна
// по ролям, лес — тоном земли. Эрозия гор — речная (строка FluvialErosion, FluvialErosion: сеть долин), затем ледниковая
// (строка GlacialErosion, GlacialErosion: троги, кары), затем ледники наших дней (тот же лёд без эрозии: поверхность — в
// рельеф, толщина — материалу; морены — линиями течения льда от скальных стен, трещины — в шейдере) — после чтения карты,
// с кэшем на диске (каталог far_eroded\ рядом с картой гор) и заново после правки подокон «Fluvial erosion» и «Glacial
// erosion». Главный вид и съёмка сцены: в каскадах теней и Scene::bounds гор нет — их тень даёт карта края тени
// (TerrainSunShadow, farHeight).
// Уровни мельче уровня карты гор — дальности карты: камера далеко от карты над обрывами гор — там возможны трещины; камера
// дальше двух «LOD distance» от карты — трещины на краю карты (её корень не грубее своего уровня)
class FarTerrain : public SceneObject, private CDLODQuadtree::Bounds
{
public:
	FarTerrain();

	// settings — строка FarTerrain; terrain — рельеф карты (сетка узлов, таблица дальностей, константы шейдеров), pipeline —
	// его данные (карта высот, слои по ролям); живут дольше гор
	bool initialize( const FarTerrainSettings& settings, CDLODTerrain& terrain, TerrainPipeline& pipeline );
	bool initialized() const { return m_initialized; }

	// Новая итоговая карта (перестройка русел) или её множитель — запекание заново; дальность гор — в таблицу карты
	void update( const FrameContext& frame ) override;
	// Плотность леса по правилам гор (заново, когда сменились правила или горы) и ресурсы сцены тем, кто ставит что-то на
	// горы (лес гор — слой far_forest): карта гор и плотность леса (SLOT_FAR_TERRAIN_HEIGHT, SLOT_FAR_FOREST, far_ground.sh)
	void compute( const FrameContext& frame ) override;
	// Угол и сторона карты гор, переход высоты к карте, кольцо деревьев на горах (cb_farTerrain, cb_farForest)
	void frameConstants( const RenderView& view, FrameParameters& constants ) override;
	// Кольцо деревьев на горах (слой расстановки far_forest, Scatterer::farForestRing): дальняя граница и её исчезание, м — там
	// лес деревьями, тон леса в материале гор сходит на нет к ближней стороне полосы исчезания; 0 — деревьев нет
	void setForestRing( float border, float fade );
	// Узлы — на каждый вид, кроме теней (RenderView::index)
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override { return &m_properties; }
	bool savedWithLevel() const override { return true; }
	// Строка FarTerrain из окна — для сохранения уровня
	FarTerrainSettings settings() const;
	// Карта гор для тех, кто считает по рельефу и за краем карты (тень рельефа); без гор heightMap пуст
	FarTerrainHeight farHeight() const;

private:
	using Patch = CDLODQuadtree::Patch;
	using Box = CDLODQuadtree::Box;

	bool loadHeights( const FarTerrainSettings& settings );
	// Эрозия исходной карты (m_original → m_source): из кэша или прогоном — речная (CPU) и ледниковая (GPU), каждая на своей
	// грубой сетке, изменение — к карте гор кубическим B-сплайном, под льдом мелкий рельеф сглажен; затем ледники наших дней —
	// их поверхность в m_source, толщина льда — в ice (сетка карты гор; без строки GlacialErosion — пусто). Без строк — исходная
	// карта
	void applyErosion( std::vector<float>& ice );
	// Ледники наших дней для материала (m_ice, m_source): течение льда, карта морен по его линиям с шириной moraineWidth и
	// текстура RGBA8 с мипами — толщина льда, доля линий обломков, течение; без льда — тексель без льда
	bool createGlacierTexture( float moraineWidth );
	FluvialErosionSettings fluvialSettings() const;
	GlacialErosionSettings glacierSettings() const;
	bool createShader();
	// Константы материала гор (FarTerrainBuffer) по окну — материалу и плотности леса
	void fillConstants( FarTerrainBuffer& params ) const;
	// Плотность леса по правилам гор на сетке карты гор (Shaders/far_forest_density.cs)
	void bakeForestDensity( const RenderView& view );
	// Рельеф для леса (FarForestRelief) по горам после эрозии на сетке вдвое грубее карты гор (64 м): водосбор — заново после
	// эрозии, лавинные лотки — и после правки их правил в окне
	void buildForestRelief();
	// Текстура рельефа для леса — RG16F с мипами, как карта гор: log10 водосбора, м², доля лавинного лотка
	bool createForestReliefTexture();
	AvalancheRules avalancheRules() const;
	// Карта гор с мипом карты внутри неё → текстура с мипами и пирамида min / max на CPU, перепады узлов по уровням
	bool bake();
	// min / max высоты текселей уровня пирамиды k (тексель — 2^k текселей карты гор) в столбцах [x0, x1] и рядах [z0, z1] (ряд —
	// от юга), за краем — край
	DirectX::XMFLOAT2 pyramidBounds( uint32_t k, int x0, int x1, int z0, int z1 ) const;
	Box node( uint32_t level, uint32_t x, uint32_t z ) const override;
	Box detailNode( uint32_t depth, uint32_t nx, uint32_t nz ) const override;

	CDLODTerrain* m_terrain = nullptr;
	TerrainPipeline* m_pipeline = nullptr;
	FarTerrainSettings m_loaded;		// строка из базы: поля только базы — как есть
	uint32_t m_size = 0;				// текселей в стороне карты гор
	uint32_t m_mipBias = 0;				// уровень, чей квад — её тексель: мип гор уровня L — L − m_mipBias
	std::vector<float> m_original;		// карта гор из файла, м, строки с севера
	std::vector<float> m_source;		// она же после эрозии, с ледниками наших дней
	std::vector<float> m_ice;			// толщина их льда, м, как карта гор; без строки GlacialErosion — пусто
	std::vector<float> m_heights;		// она же с мипом карты внутри — то, что видят вершины
	// Пирамида min / max: уровень k (с 1) — тексель 2^k текселей, строки с севера; уровень 0 — m_heights
	std::vector<std::vector<float>> m_minPyramid;
	std::vector<std::vector<float>> m_maxPyramid;
	DirectX::XMFLOAT2 m_mapHeights = DirectX::XMFLOAT2( 0.0f, 0.0f );	// min / max высоты карты, м — узлы у её края
	std::vector<float> m_heightExtent;	// по уровням: наибольший перепад высот узла, м (с m_mipBias; ниже — 0)
	uint32_t m_bakedRevision = 0;		// итоговая карта, мип которой запечён
	uint32_t m_bakes = 0;				// запеканий с запуска — FarTerrainHeight::revision
	float m_maxHeight = 0.0f;			// наибольшая высота гор, м
	float m_bakedMultiplier = 0.0f;
	float m_bakedOffset = 0.0f;

	CDLODQuadtree m_quadtree;
	std::array<std::vector<Patch>, maxRenderViews> m_patches;	// по номеру вида: главного и съёмки (теней гор нет)
	GridMesh m_patch;
	ShaderProgram m_shader;
	int m_materialPhase = 0;			// фазы: материал, раскраска по уровням LOD, только глубина (depth prepass)
	int m_lodPhase = 0;
	int m_depthPhase = 0;
	DMStructuredBuffer m_patchBuffer;
	Buffer m_terrainConstants;			// CDLODBuffer — как у карты
	Buffer m_constants;					// FarTerrainBuffer
	Texture m_heightTexture;
	ShaderView m_heightView;
	Texture m_glacierTexture;			// ледники наших дней (как карта гор): толщина льда, линии обломков (морены), течение
	ShaderView m_glacierView;
	float m_moraineWidth = 0.0f;		// ширина, с которой построена карта морен, м
	// Плотность леса по правилам гор (R8, сетка карты гор): по ней слой far_forest ставит деревья
	Texture m_forestDensity;
	StorageView m_forestDensityWrite;
	ShaderView m_forestDensityView;
	ComputeShader m_forestDensityShader;
	uint64_t m_forestDensityKey = ~0ull;	// правки окна и запекания гор, по которым она посчитана
	FarForestRelief m_forestRelief;
	AvalancheRules m_avalancheRules;		// правила, по которым посчитаны лотки
	Texture m_forestReliefTexture;
	ShaderView m_forestReliefView;
	uint32_t m_forestReliefRevision = 0;	// текстура рельефа заново — плотность леса заново
	float m_forestRingFar = 0.0f;		// кольцо деревьев на горах (setForestRing)
	float m_forestRingFade = 0.0f;
	PropertyContainer m_properties;
	PropertyContainer m_fluvialProperties;	// подокно «Fluvial erosion» (есть, если у гор есть строка FluvialErosion)
	PropertyContainer m_glacierProperties;	// подокно «Glacial erosion» (есть, если у гор есть строка GlacialErosion)
	uint32_t m_erosionRevision = 0;		// правки подокон, по которым размыты горы
	bool m_initialized = false;
};

}
