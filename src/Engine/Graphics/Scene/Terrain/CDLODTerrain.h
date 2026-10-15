#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainPipeline.h"
#include "TerrainPaintTool.h"
#include "ShaderProgram.h"
#include "D3D\DMStructuredBuffer.h"
#include "Shaders\cdlod_constants.h"

namespace GS
{

// Террейн CDLOD (Continuous Distance-Dependent Level of Detail, F. Strugar, 2009) — отрисовка рельефа; данные (карта высот,
// русла, слои и маски, материал слоёв, поле ветра) — у конвейера рельефа и воды (TerrainPipeline).
// Квадродерево над картой высот: корень покрывает весь террейн, лист — gridDim текселей.
// У каждого уровня свой диапазон расстояний, вдвое больше предыдущего; узел, до которого камера ближе
// диапазона следующего уровня, делится на четыре. Выбранные узлы рисуются одним инстансным вызовом общего
// патча, а морфинг в вершинном шейдере (Shaders\cdlod.vs) сглаживает переходы между уровнями. У камеры — ещё уровни
// мельче листа везде, а высота вершины — из детальной плитки у русла (TerrainHydrology::DetailTile,
// Shaders\terrain_detail.sh: U-ложе, бровки) или из карты высот: квадродерево однородно, трещин нет, как у CDLOD.
// Материал — TerrainMaterial конвейера (Shaders\terrain.ps), настройки — строка таблицы Terrain в base.db3, на которую
// ссылается уровень; окно — его поля и части окна конвейера (TerrainPipeline::attachWindow)
class CDLODTerrain : public SceneObject
{
public:
	CDLODTerrain();

	// settings — строка Terrain (её читает конвейер: TerrainPipeline::initialize), pipeline — данные рельефа, paint — кисть
	// (её подокно — в окне террейна, цель — поверх земли); живут дольше террейна
	bool initialize( const TerrainSettings& settings, TerrainPipeline& pipeline, TerrainPaintTool& paint );

	// Множитель высоты из окна — в конвейер; границы узлов — по новой итоговой карте (перестройка русел)
	void update( const FrameContext& frame ) override;
	// Константы слоёв кадра — ресурс сцены SLOT_TERRAIN_LAYER_CONSTANTS: выбор слоёв и дисплейсмент у шейдеров террейна,
	// расстановки и частиц по одним данным. Точка LOD — главного вида (у видов теней та же)
	void compute( const FrameContext& frame ) override;
	// Выбор узлов квадродерева для вида: LOD — от точки LOD вида, отсечение — по его frustum
	void collectMeshes( const RenderView& view, MeshCollector& collector ) override;
	void renderCustom( const RenderContext& context ) override;
	// Пайплайны рельефа: цвет с растеризатором кадра (сплошной или каркасный) в буфер сцены; prepass и тени — только глубина
	void warmPipelines( const PassStates& states ) override;
	PropertyContainer* properties() override;
	bool savedWithLevel() const override { return true; }
	// Строка Terrain из окна — для сохранения уровня (части конвейера — TerrainPipeline::settings)
	TerrainSettings settings() const;

private:
	static constexpr uint32_t gridDim = 32;			// квадов в стороне узла
	static constexpr uint32_t patchDim = gridDim / 2;	// квадов в стороне патча: узел рисуется четырьмя патчами-четвертями
	static constexpr uint32_t maxLevels = CDLOD_MAX_LEVELS;
	static constexpr uint32_t maxPatches = 8192;
	static constexpr float morphStartRatio = 0.66f;	// с какой доли диапазона уровня начинается морфинг
	// Уровни мельче листа: до шага детальной плитки (tileLevels: 0,5 и 0,25 м) и ещё четыре — под дисплейсмент (0,125,
	// 0,0625, 0,03125 и 0,015625 м у камеры); сколько из них рисовать — «Finest mesh» (m_meshLevels), радиус самого мелкого —
	// «Finest mesh radius»
	static constexpr uint32_t detailLevels = CDLOD_DETAIL_LEVELS;
	static constexpr uint32_t tileLevels = 2;
	static_assert( ( 1u << tileLevels ) == TerrainHydrology::detailSamples, "tile levels reach the detail tile step" );
	static_assert( gridDim == TerrainHydrology::detailTileCells, "a detail tile is a leaf node: TerrainPipeline::detailBounds" );

	struct PatchInstance
	{
		DirectX::XMFLOAT2 origin;
		float size;
		float level;
	};




	struct NodeBox
	{
		DirectX::XMFLOAT3 min;
		DirectX::XMFLOAT3 max;
	};

	bool createShader();
	// Минимум / максимум высоты каждого узла по мипам его уровня (мипы итоговой карты конвейера): нормированные, по уровням
	void buildNodeBounds();
	void calcRanges();
	// Константы слоёв для вида: точка LOD — его (у видов теней — главного)
	TerrainLayerConstants layerConstants( const RenderView& view ) const;
	NodeBox nodeBox( uint32_t level, uint32_t x, uint32_t z ) const;
	float nodeSize( uint32_t level ) const;
	// false — узел дальше диапазона своего уровня, и его площадь рисует родитель
	bool selectNode( const RenderView& view, uint32_t level, uint32_t x, uint32_t z, std::vector<PatchInstance>& patches );
	void addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<PatchInstance>& patches );
	// Уровни мельче листа: depth 1 — узел вдвое меньше листа (−1), 2 — вчетверо (−2); nx, nz — узел в сетке этого уровня.
	// false — узел дальше диапазона своего уровня: его площадь рисует родитель
	bool selectDetailNode( const RenderView& view, uint32_t depth, uint32_t nx, uint32_t nz, std::vector<PatchInstance>& patches );
	void addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<PatchInstance>& patches );

private:
	TerrainPipeline* m_pipeline = nullptr;
	const TerrainPaintTool* m_paintTool = nullptr;
	uint32_t m_terrainId = 0;
	uint32_t m_levelCount = 0;
	std::vector<uint32_t> m_nodesPerSide;
	std::vector<std::vector<DirectX::XMFLOAT2>> m_heightBounds;	// по уровням: нормированные min / max высоты узлов
	uint32_t m_boundsRevision = 0;		// номер итоговой карты конвейера, по которой посчитаны границы
	float m_ranges[maxLevels] = {};
	DirectX::XMFLOAT4 m_morphConsts[maxLevels] = {};
	float m_detailRanges[detailLevels] = {};
	uint32_t m_meshLevels = detailLevels;		// уровней мельче листа рисуется («Finest mesh»): tileLevels…detailLevels
	DirectX::XMFLOAT4 m_detailMorph[detailLevels] = {};

	// Выбранные патчи на каждый вид кадра (RenderView::index): главный и каскады теней. Деление узлов — от lodOrigin (и
	// отрезка до ног — lodEyeHeight), общих у всех видов, frustum вида только отсекает, поэтому рельеф в тени и на экране один
	std::array<std::vector<PatchInstance>, maxRenderViews> m_patches;
	GridMesh m_patch;
	ShaderProgram m_shader;
	int m_materialPhase = 0;	// фазы m_shader: материал, раскраска по уровням LOD, только глубина (тени)
	int m_lodPhase = 0;
	int m_depthPhase = 0;
	DMStructuredBuffer m_patchBuffer;
	DMStructuredBuffer m_layerConstants;	// TerrainLayerConstants кадра — ресурс сцены (compute)
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
