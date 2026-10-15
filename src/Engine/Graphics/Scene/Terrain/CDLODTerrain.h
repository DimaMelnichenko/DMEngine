#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainPipeline.h"
#include "ShaderProgram.h"
#include "D3D\DMStructuredBuffer.h"

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

	// settings — строка Terrain (её читает конвейер: TerrainPipeline::initialize), pipeline — данные рельефа; живёт дольше
	// террейна
	bool initialize( const TerrainSettings& settings, TerrainPipeline& pipeline );

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
	static constexpr uint32_t maxLevels = 16;
	static constexpr uint32_t maxPatches = 8192;
	static constexpr float morphStartRatio = 0.66f;	// с какой доли диапазона уровня начинается морфинг
	// Уровни мельче листа: до шага детальной плитки (tileLevels: 0,5 и 0,25 м) и ещё четыре — под дисплейсмент (0,125,
	// 0,0625, 0,03125 и 0,015625 м у камеры); сколько из них рисовать — «Finest mesh» (m_meshLevels), радиус самого мелкого —
	// «Finest mesh radius»
	static constexpr uint32_t detailLevels = 6;
	static constexpr uint32_t tileLevels = 2;
	static_assert( ( 1u << tileLevels ) == TerrainHydrology::detailSamples, "tile levels reach the detail tile step" );
	static_assert( gridDim == TerrainHydrology::detailTileCells, "a detail tile is a leaf node: TerrainPipeline::detailBounds" );

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

	bool createShader();
	// Минимум / максимум высоты каждого узла по мипам его уровня (мипы итоговой карты конвейера): нормированные, по уровням
	void buildNodeBounds();
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

private:
	TerrainPipeline* m_pipeline = nullptr;
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
	DMStructuredBuffer m_layerConstants;	// LayerConstants кадра — ресурс сцены (compute)
	Buffer m_constantBuffer;
	PropertyContainer m_properties;
	bool m_initialized = false;
};

}
