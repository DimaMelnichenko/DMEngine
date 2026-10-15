#pragma once

#include "WaterField.h"
#include "SceneObject.h"
#include "DMComputeShader.h"
#include "ShaderProgram.h"
#include "Terrain\GridMesh.h"

namespace GS
{

class StaticWater;

// Поверхность воды — свой вызов в проходе transparent (читает цвет и глубину сцены). Уровень поверхности и границы тайлов
// по глубине поля (water_surface.cs, mainSurface) — после шагов труб или расчёта статичной воды; каждый кадр — видимые тайлы
// главного вида и детальные плитки у русел ближе detailDistance (mainTiles) в списки экземпляров двух косвенных вызовов.
// Рисует water.vs / water.ps: поглощение по толщине, экранные отражения, блик, рябь (карта WaterRipples, плитками по
// течению) и пена; берег — пересечение продолженной за урез плоскости воды с рельефом
class WaterSurface
{
public:
	bool initialize( WaterField& field );
	// Уровень поверхности и границы тайлов (settings — константы поля)
	void build( const WaterSimulationSettings& settings );
	// Шаги труб были — уровень поверхности пересчитать
	bool dirty() const { return m_dirty; }
	void setDirty() { m_dirty = true; }
	// Видимые тайлы главного вида и детальные плитки статичной воды — в списки экземпляров косвенных вызовов
	void cull( const RenderView& view, const WaterSimulationSettings& settings, const StaticWater& water );
	void render( const RenderContext& context, const WaterSimulationSettings& settings, const StaticWater& water );
	void warmPipelines( const PassStates& states );
	// Уровень поверхности, м (−1e30 — сухо): ресурс сцены SLOT_WATER_LEVEL — глубина в точке, урез внутри клетки
	const ShaderView& levelView() const { return m_levelView; }

private:
	// Раскладка — cbuffer WaterTilesBuffer (b5) в water_surface.cs
	struct alignas( 16 ) TilesParameters
	{
		DirectX::XMFLOAT4 planes[6];
		uint32_t indexCount;
		uint32_t tilesPerSide;
		float visibleDepth;
		float worldSize;
		DirectX::XMFLOAT3 cameraPosition;	// главного вида: ближе detailDistance — детальная поверхность
		float detailDistance;				// м; 0 — детальной воды нет
		uint32_t detailIndexCount;			// индексов сетки детальной плитки
		uint32_t detailPadding[3];
	};

	// Раскладка — cbuffer WaterSurfaceBuffer (b2) в Shaders/water_surface.sh
	struct alignas( 16 ) SurfaceParameters
	{
		uint32_t size;
		float cellSize;
		float worldSize;
		uint32_t tilesPerSide;
		DirectX::XMFLOAT3 absorption;
		float rippleStretch;
		DirectX::XMFLOAT3 scatterColor;
		float rippleScale;
		float rippleStrength;
		float calmRipple;
		float refraction;
		float roughness;
		float foamSpeed;
		float foamShear;
		float detailDistance;	// м: ближе — детальная поверхность у русел (0 — её нет)
		float foamSlope;		// пена слива — где поверхность детальной воды круче, м на м (WaterChannelsSettings::foamSlope)
		// Точная земля — урез по ней (water.ps): карта высот (слот 3) в метры и детальная плитка (terrain_detail.sh)
		float terrainHeightMultiplier;
		float terrainHeightOffset;
		float terrainDetailTile;	// сторона детальной плитки, м; 0 — её нет
		float surfacePadding;
	};

	WaterField* m_field = nullptr;
	DMComputeShader m_surfaceShader;
	DMComputeShader m_tilesResetShader;
	DMComputeShader m_tilesShader;
	ShaderProgram m_surfaceProgram;
	int m_surfacePhase = -1;
	int m_detailPhase = -1;
	GridMesh m_tileMesh;
	GridMesh m_detailMesh;			// сетка детальной плитки: вершина на тексель (с полем)
	uint32_t m_tilesPerSide = 0;
	bool m_dirty = true;			// шаги труб были — уровень поверхности пересчитать
	Texture m_level;				// R32_FLOAT: уровень поверхности, м; −1e30 — сухо
	StorageView m_levelUAV;
	ShaderView m_levelView;
	Buffer m_tileBounds;			// float2 на тайл: наименьший и наибольший уровень
	StorageView m_tileBoundsUAV;
	ShaderView m_tileBoundsView;
	Buffer m_tileList;				// uint на видимый тайл — экземпляры вызова
	StorageView m_tileListUAV;
	ShaderView m_tileListView;
	Buffer m_drawArgs;				// команда ExecuteIndirect (24 байта) и число команд
	StorageView m_drawArgsUAV;
	Buffer m_detailTileList;		// uint на видимую детальную плитку — экземпляры её вызова
	StorageView m_detailTileListUAV;
	ShaderView m_detailTileListView;
	Buffer m_detailDrawArgs;		// команда ExecuteIndirect и число команд
	StorageView m_detailDrawArgsUAV;
	Buffer m_tilesBuffer;			// TilesParameters
	Buffer m_surfaceBuffer;			// SurfaceParameters
	Texture m_ripple;				// карта ряби (WaterRipples)
	ShaderView m_rippleView;
};

}
