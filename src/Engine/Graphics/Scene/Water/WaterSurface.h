#pragma once

#include "WaterField.h"
#include "SceneObject.h"
#include "ComputeShader.h"
#include "ShaderProgram.h"
#include "Terrain\GridMesh.h"
#include "Shaders\water_tiles_constants.h"
#include "Shaders\water_surface_constants.h"

namespace GS
{

class StaticWater;
class WaterFoam;

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
	// foam — поле пены статичной воды (пусто — пена по скорости и сдвигу в пикселе); nearCells — ячеек по стороне окна
	// подробного течения у камеры (NearWaterFlow::windowCells; 0 — окна нет): в окне поверхность рисует его сетка
	void render( const RenderContext& context, const WaterSimulationSettings& settings, const StaticWater& water, const WaterFoam& foam,
				 uint32_t nearCells );
	// Проход reflectionPrepass: та же поверхность — глубина и нормаль без ряби для экранных отражений (water_reflection.ps)
	void renderReflectionSurface( const WaterSimulationSettings& settings, const StaticWater& water, const WaterFoam& foam,
								  uint32_t nearCells );
	void warmPipelines( const PassStates& states );
	// Уровень поверхности, м (−1e30 — сухо): ресурс сцены SLOT_WATER_LEVEL — глубина в точке, урез внутри клетки
	const ShaderView& levelView() const { return m_levelView; }
	// Узор пены (WaterFoamLace): ресурс сцены SLOT_WATER_FOAM_PATTERN — клочкам взбитой воды (частицы, shape froth)
	const ShaderView& foamPatternView() const { return m_foamLaceView; }

private:
	// Константы поверхности (WaterSurfaceBuffer) — у обоих проходов одни
	void updateParameters( const WaterSimulationSettings& settings, const StaticWater& water, const WaterFoam& foam );
	// Сетка окна у камеры — cells² вершин (заново при смене стороны окна); false — окна нет
	bool prepareNearMesh( const StaticWater& water, uint32_t cells );

	WaterField* m_field = nullptr;
	ComputeShader m_surfaceShader;
	ComputeShader m_tilesResetShader;
	ComputeShader m_tilesShader;
	ShaderProgram m_surfaceProgram;
	int m_surfacePhase = -1;
	int m_detailPhase = -1;
	int m_reflectionPhase = -1;			// поверхность для экранных отражений: тайлы 1 м и детальные плитки
	int m_reflectionDetailPhase = -1;
	int m_nearPhase = -1;				// окно подробного течения у камеры: цвет и поверхность для отражений
	int m_reflectionNearPhase = -1;
	GridMesh m_tileMesh;
	GridMesh m_detailMesh;			// сетка детальной плитки: вершина на тексель (с полем)
	GridMesh m_nearMesh;			// сетка окна у камеры: вершина на ячейку окна
	uint32_t m_nearMeshCells = 0;	// её вершин по стороне (0 — её нет)
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
	Texture m_foamLace;				// узор пены — кружево (WaterFoamLace); ресурс сцены SLOT_WATER_FOAM_PATTERN — у WaterSimulation
	ShaderView m_foamLaceView;
};

}
