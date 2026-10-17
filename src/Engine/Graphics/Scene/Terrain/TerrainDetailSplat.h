#pragma once

#include <cstdint>
#include <utility>
#include <vector>
#include "D3D\GpuResources.h"
#include "TerrainHydrology.h"

namespace GS
{

struct TerrainEditCoverage;
struct TerrainMasks;
class TerrainPaint;

// Уровень воды сетки 1 м (level: size × size клеток со стороной texel, строки сверху вниз, −1e30 — нет) в точке (x, z),
// как waterDepthAt в шейдерах: билинейно по четырём клеткам вокруг, клетка без уровня — наименьший из соседних; воды
// рядом нет — меньше −1e29
float cellWaterLevel( const std::vector<float>& level, int size, float texel, float x, float z );

// Детальный слой весов земли (docs/terrain.md, «Детальный слой весов»): веса восьми слоёв и мокрость с шагом 0,25 м —
// плитками там, где они нужны (русла и мазки кисти), поверх splat-карты 1 м, как основа и детали поверх у Landscape в UE
// (веса по вершине, мельче — Runtime Virtual Texture). Плитки — по сетке детальной земли (сторона — лист квадродерева,
// 32 м, 130 × 130 текселей с полем в тексель с каждой стороны, строки — по z мира вверх); тексели совпадают с тонкой
// сеткой кисти (TerrainPaint::fineScale): тексель i плитки x — столбец x · 128 + i − 1. На GPU — массив RGBA8 по три среза
// на место (3s — слои 0…3, 3s + 1 — 4…7, 3s + 2 — не веса: R — мокрость, G, B, A — запас под дождь), мест — с запасом
// (новая плитка мазка — без пересоздания), и индекс R16_UINT (плитка мира → место + 1, 0 — нет).
// В текселе по порядку: основа — веса сетки 1 м (правила) билинейно, покраска ручными правками рельефа, галька русел по
// врезу плитки, галька дна под водой static по глубине, мокрость от воды static (под водой и каймой 2 см) — это копия «до
// кисти» у плитки на CPU; поверх — мазки тонкой сетки (TerrainPaint): слои по порядку номеров, мокрость. Глубина — над
// точной землёй: у русел — высота детальной плитки и детальная вода (water_detail.cs, waterDepthAt в центре текселя), у
// плиток мазков — карта высот и уровень сетки 1 м (как mainSurface). Сумма весов не нормируется: terrain.ps нормирует
// четыре самых весомых. Шейдер берёт плитку, пока пиксель не крупнее двух текселей карты высот, к четырём — splat-карту
class TerrainDetailSplat
{
public:
	using TileKey = std::pair<uint32_t, uint32_t>;	// плитка мира: x, z (z — от ближнего края карты)

	// Откуда веса
	struct Source
	{
		const TerrainMasks* masks = nullptr;	// веса правил сетки 1 м (TerrainMasks::splat), строка 0 — дальний край по z
		float texelSize = 1.0f;					// клетка сетки 1 м, м
		// Покраска ручными правками рельефа (TerrainEdits.paint_layer) — поверх основы, как в splat-карте; покраски русел в
		// ней нет: в плитке её заменяет врез
		const TerrainEditCoverage* edits = nullptr;
		uint32_t layerCount = 0;				// слоёв в массивах материала: покраска слоёв дальше пропускается
		int32_t channelLayer = -1;				// слой гальки русел (роль channel_bed); −1 — не красить
		// Наносы на дне (правила «Water beds»): слои дресвы и ила (роли lake_gravel, lake_silt; −1 — нет) и пороги правил —
		// у нижних озёр по глубине, у медленных ручьёв по течению; без правил — галька, как раньше
		int32_t gravelLayer = -1;
		int32_t siltLayer = -1;
		const TerrainLayerRules* bedRules = nullptr;
		bool staticWater = false;				// вода режима static: дно под водой и мокрость
		const std::vector<TerrainHydrology::DetailTile>* hydrology = nullptr;	// детальная земля у русел
		// Точная земля и вода вне детальной земли (плитки мазков): итоговая карта высот (нормированная, строки как у масок) и
		// уровень поверхности воды сетки 1 м (−1e30 — нет; TerrainPipeline::computeWaterLevel)
		const float* heights = nullptr;
		float heightMultiplier = 1.0f;
		float heightOffset = 0.0f;
		const std::vector<float>* waterLevel = nullptr;
		const TerrainPaint* paint = nullptr;	// мазки тонкой сетки — последними
	};

	// Все плитки заново: детальная земля у русел и extra (плитки мазков); веса, массив и индекс (tilesPerSide² плиток мира)
	bool build( const Source& source, uint32_t tilesPerSide, const std::vector<TileKey>& extra );
	// Плиток нет — создать (копия «до кисти»), на GPU — новые места и индекс
	void ensure( const Source& source, const std::vector<TileKey>& tiles );
	// Итог плиток (копия «до кисти» + мазки) заново и на GPU, участками массива (DMD3D::updateTexture)
	void refresh( const Source& source, const std::vector<TileKey>& tiles );
	// Плитки мира, которые задевает прямоугольник тонкой сетки [x0, x1) × [row0, row1): border — и те, у кого он в поле
	// (тексель края — тоже их); existing — только те, что есть
	std::vector<TileKey> tilesOf( uint32_t x0, uint32_t row0, uint32_t x1, uint32_t row1, bool border, bool existing ) const;
	// Значение «до кисти» в текселе тонкой сетки: слои 0…7 — веса, wetnessChannel — мокрость; плитки нет — false
	bool sourceAt( uint32_t x, uint32_t row, uint32_t channel, float& value ) const;
	static constexpr uint32_t wetnessChannel = 8;
	// Ресурсы сцены (terrain_layers.sh): массив плиток и индекс — их читают террейн, расстановка и частицы
	void bind() const;
	// Сторона плитки, м; 0 — плиток нет
	float tileSize() const { return m_tileSize; }
	size_t tileCount() const { return m_tiles.size(); }

private:
	struct Tile
	{
		uint32_t x = 0;
		uint32_t z = 0;
		int32_t hydrology = -1;			// детальная плитка у русла (Source::hydrology) или −1 — плитка мазка
		std::vector<uint8_t> source;	// копия «до кисти»: три среза side² RGBA8 подряд
	};

	// Копия «до кисти» плитки и итог (копия + мазки) — три среза side² RGBA8 подряд
	void computeSource( Tile& tile, const Source& source ) const;
	void computeFinal( const Tile& tile, const Source& source, std::vector<uint8_t>& out ) const;
	bool createArray( const Source& source );
	bool uploadIndex();
	void upload( size_t slot, const std::vector<uint8_t>& data );

	uint32_t m_side = 0;		// текселей плитки по стороне, с полем
	float m_tileSize = 0.0f;
	uint32_t m_tilesPerSide = 0;
	uint32_t m_fineSize = 0;	// текселей тонкой сетки по стороне
	std::vector<Tile> m_tiles;	// по местам массива
	std::vector<int32_t> m_slot;	// плитка мира (x + z · m_tilesPerSide) → место, −1 — нет
	uint32_t m_capacity = 0;	// мест в массиве
	Texture m_texture;
	ShaderView m_view;
	Texture m_indexTexture;
	ShaderView m_indexView;
};

}
