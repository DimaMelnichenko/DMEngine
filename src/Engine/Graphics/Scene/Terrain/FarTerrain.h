#pragma once

#include <vector>
#include "Level\LevelSettings.h"
#include "SceneObject.h"
#include "GridMesh.h"
#include "CDLODQuadtree.h"
#include "TerrainHeightSource.h"
#include "ShaderProgram.h"
#include "D3D\DMStructuredBuffer.h"

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
// по ролям, лес — тоном земли. Только главный вид: в каскадах теней и Scene::bounds гор нет (их тени — TODO, этап 16).
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
	// Узлы — только для главного вида
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
	bool createShader();
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
	std::vector<float> m_source;		// карта гор из файла, м, строки с севера
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
	std::vector<Patch> m_patches;		// главного вида
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
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
