#pragma once

#include <cstdint>
#include <vector>
#include "D3D\GpuResources.h"
#include "TerrainHydrology.h"

namespace GS
{

struct TerrainEditCoverage;
struct TerrainMasks;

// Детальный слой весов земли (docs/terrain.md, «Детальный слой весов»): веса восьми слоёв с шагом 0,25 м — плитками там,
// где они нужны (русла), поверх splat-карты 1 м, как основа и детали поверх у Landscape UE (веса по вершине, мельче —
// Runtime Virtual Texture). Плитки — по сетке детальной земли (TerrainHydrology::DetailTile: сторона — лист квадродерева,
// 32 м, 130 × 130 текселей с полем в тексель с каждой стороны, строки — по z мира вверх). На GPU — массив RGBA8 по два
// среза на плитку (2t — слои 0…3, 2t + 1 — 4…7) и индекс R16_UINT (плитка мира → номер + 1, 0 — нет).
// Веса в текселе по порядку: основа — веса сетки 1 м (правила с мазками) билинейно, покраска ручными правками рельефа,
// галька русел по врезу плитки. Сумма не нормируется: terrain.ps нормирует четыре самых весомых. Шейдер берёт плитку,
// пока пиксель не крупнее двух текселей карты высот, к четырём — splat-карту 1 м (в ней галька русел по клеткам). Дно под водой
// (режим static) — не здесь, а в шейдере по глубине в точке нарисованного рельефа: урез — там же, где его видит вода
// (вдали рельеф грубее плитки, и вес по её высоте разошёлся бы с краем воды)
class TerrainDetailSplat
{
public:
	// Откуда веса: сетка 1 м и слой гальки
	struct Source
	{
		const TerrainMasks* masks = nullptr;	// веса слоёв сетки 1 м (TerrainMasks::splat), строка 0 — дальний край по z
		float texelSize = 1.0f;					// клетка сетки 1 м, м
		// Покраска ручными правками рельефа (TerrainEdits.paint_layer) — поверх основы, как в splat-карте; покраски русел в
		// ней нет: в плитке её заменяет врез
		const TerrainEditCoverage* edits = nullptr;
		uint32_t layerCount = 0;				// слоёв в массивах материала: покраска слоёв дальше пропускается
		int32_t channelLayer = -1;				// слой гальки русел (роль channel_bed); −1 — не красить
	};

	// Все плитки заново по детальной земле tiles: веса, массив и индекс (tilesPerSide² плиток мира)
	bool build( const std::vector<TerrainHydrology::DetailTile>& tiles, uint32_t tilesPerSide, const Source& source );
	// Веса сетки 1 м сменились в прямоугольнике клеток [x0, x1) × [row0, row1) (мазок слоя, отмена): плитки, которые он
	// задевает, — заново, участками массива (DMD3D::updateTexture). tiles — те же, что у build
	void refresh( const std::vector<TerrainHydrology::DetailTile>& tiles, const Source& source, uint32_t x0, uint32_t row0, uint32_t x1,
				  uint32_t row1 );
	// Пиксельному шейдеру террейна (terrain.ps): t5 — массив плиток, t6 — индекс
	void bind() const;
	// Сторона плитки, м; 0 — плиток нет
	float tileSize() const { return m_tileSize; }

private:
	// Веса плитки — два среза side² RGBA8 подряд в out
	void computeTile( const TerrainHydrology::DetailTile& tile, const Source& source, uint8_t* out ) const;

	uint32_t m_side = 0;		// текселей плитки по стороне, с полем
	float m_tileSize = 0.0f;
	size_t m_tileCount = 0;
	Texture m_texture;
	ShaderView m_view;
	Texture m_indexTexture;
	ShaderView m_indexView;
};

}
