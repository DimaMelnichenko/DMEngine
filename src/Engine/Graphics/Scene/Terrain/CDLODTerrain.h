#pragma once

#include "Level\LevelSettings.h"
#include <array>
#include <vector>
#include "SceneObject.h"
#include "GridMesh.h"
#include "TerrainMaterial.h"
#include "TerrainHeightSource.h"
#include "ShaderProgram.h"
#include "D3D\DMStructuredBuffer.h"

namespace GS
{

struct TerrainEditCoverage;

// Террейн CDLOD (Continuous Distance-Dependent Level of Detail, F. Strugar, 2009).
// Квадродерево над картой высот: корень покрывает весь террейн, лист — gridDim текселей.
// У каждого уровня свой диапазон расстояний, вдвое больше предыдущего; узел, до которого камера ближе
// диапазона следующего уровня, делится на четыре. Выбранные узлы рисуются одним инстансным вызовом общего
// патча, а морфинг в вершинном шейдере (Shaders\cdlod.vs) сглаживает переходы между уровнями.
// Материал — TerrainMaterial (Shaders\terrain.ps), настройки — строка таблицы Terrain в base.db3, на которую
// ссылается уровень, слои материала — таблица TerrainLayers
class CDLODTerrain : public SceneObject, public TerrainHeightSource
{
public:
	CDLODTerrain();

	// terrainId — строка таблицы Terrain, edits — правки рельефа уровня по порядку слоёв (накладываются на копию карты
	// высот при загрузке: её читают вершины, узлы квадродерева и всё, что стоит на рельефе; их очистка растительности —
	// маска для расстановки, покраска — в splat-карту материала)
	bool initialize( uint32_t terrainId, const std::vector<TerrainEdit>& edits = {} );
	// Карта высот и её масштаб: по ним стоят расстановка травы и декора и частицы
	TerrainHeight terrainHeight() const override;

	void update( const FrameContext& frame ) override;
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
	};

	struct NodeBox
	{
		DirectX::XMFLOAT3 min;
		DirectX::XMFLOAT3 max;
	};

	bool loadSettings( uint32_t terrainId, TerrainSettings& settings, std::string& splatMap );
	bool createShader();
	// Копия карты высот с правками рельефа и мипами для вершинного шейдера и минимум / максимум высоты каждого узла по
	// мипам его уровня; coverage — что правки делают, кроме высоты
	bool buildHeightBounds( const std::vector<TerrainEdit>& edits, TerrainEditCoverage& coverage );
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

private:
	std::string m_heightMapName;	// карта высот в хранилище текстур
	float m_worldSize = 0.0f;
	float m_texelSize = 1.0f;
	float m_heightOffset = 0.0f;
	float m_heightMultiplier = 1.0f;
	uint32_t m_terrainId = 0;
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
