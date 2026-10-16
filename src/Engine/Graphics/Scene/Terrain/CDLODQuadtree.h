#pragma once

#include <cstdint>
#include <vector>
#include "DirectX.h"
#include "Shaders\cdlod_constants.h"

namespace GS
{

struct RenderView;

// Квадродерево CDLOD (Continuous Distance-Dependent Level of Detail, F. Strugar, 2009) над квадратной областью мира: таблица
// дальностей уровней и выбор узлов для вида. Узел уровня L — квадрат leafSize · 2^L от начала области; у каждого уровня свой
// диапазон расстояний, вдвое больше предыдущего; узел, до которого камера ближе диапазона следующего уровня, делится на четыре.
// У камеры лист делится ещё на уровни мельче листа (−1 … −detailLevels). Выбранный узел рисуется четырьмя патчами-четвертями
// общей сетки (Shaders/cdlod.vs). Коробки узлов (высоты — для дальности и frustum) даёт владелец: у террейна — по мипам карты
// высот (CDLODTerrain). Общее у рельефа карты и дальних гор вокруг неё
class CDLODQuadtree
{
public:
	static constexpr uint32_t maxLevels = CDLOD_MAX_LEVELS;
	static constexpr uint32_t detailLevels = CDLOD_DETAIL_LEVELS;
	static constexpr uint32_t maxPatches = 8192;	// патчей на вид: больше — не рисуются (разовая строка в логе)
	static constexpr float morphStartRatio = 0.66f;	// с какой доли диапазона уровня начинается морфинг

	// Патч — четверть узла, экземпляр общей сетки: NodeInstance в Shaders/cdlod.vs
	struct Patch
	{
		DirectX::XMFLOAT2 origin;
		float size;
		float level;		// < 0 — уровни мельче листа
	};

	struct Box
	{
		DirectX::XMFLOAT3 min;
		DirectX::XMFLOAT3 max;
	};

	// Коробки узлов в мире: node — узел уровня level (x, z — в сетке уровня от начала области), detailNode — узел уровня мельче
	// листа depth (1 — вдвое меньше листа), nx, nz — в сетке этого уровня
	class Bounds
	{
	public:
		virtual ~Bounds() = default;
		virtual Box node( uint32_t level, uint32_t x, uint32_t z ) const = 0;
		virtual Box detailNode( uint32_t depth, uint32_t nx, uint32_t nz ) const = 0;
	};

	// Дальности и морфинг уровней — и константы вершинного шейдера (morphConsts, detailMorph в Shaders/cdlod_constants.h)
	struct Lod
	{
		float ranges[maxLevels] = {};
		DirectX::XMFLOAT4 morph[maxLevels] = {};
		float detailRanges[detailLevels] = {};
		DirectX::XMFLOAT4 detailMorph[detailLevels] = {};
		uint32_t meshLevels = 0;	// уровней мельче листа рисуется: 0…detailLevels
	};

	struct LodSettings
	{
		float lodDistance = 0.0f;	// дальность листа; меньше нижней границы без трещин — не применяется
		uint32_t meshLevels = 0;	// уровней мельче листа: 0…detailLevels
		float finestRadius = 0.0f;	// самый мелкий из них — полон в этом радиусе (вокруг ног при ходьбе)
		float detailMargin = 0.0f;	// запас высоты узлов мельче листа (дисплейсмент), м
	};

	// origin — угол области с наименьшими x, z; leafSize — сторона листа, м; extent — сторона области, м. Уровней — пока корень
	// не покроет область
	void initialize( const DirectX::XMFLOAT2& origin, float leafSize, float extent );
	uint32_t levelCount() const { return m_levelCount; }
	uint32_t nodesPerSide( uint32_t level ) const { return m_nodesPerSide[level]; }
	float nodeSize( uint32_t level ) const;
	const DirectX::XMFLOAT2& origin() const { return m_origin; }
	float extent() const { return m_extent; }

	// Таблица дальностей. Трещин нет, если на границе уровней L и L + 1 вершины уровня L уже полностью морфированы, а уровня
	// L + 1 ещё не начали: граница удалена от камеры не больше чем на диапазон L плюс диагональ узла L (с перепадом высот),
	// а морфинг уровня L + 1 начинается через morphStartRatio диапазона L после конца уровня L. Отсюда нижняя граница
	// диапазона: диагональ / morphStartRatio. heightExtent — наибольший перепад высот узла каждого уровня, м
	void computeLod( const LodSettings& settings, const std::vector<float>& heightExtent );
	const Lod& lod() const { return m_lod; }

	// Узлы для вида: дальность — от отрезка «глаза — ноги» (view.lodOrigin и на lodEyeHeight ниже), отсечение — frustum вида.
	// Корень вне дальности рисуется своим уровнем: область не пропадает, как далеко ни отлетела камера
	void select( const RenderView& view, const Bounds& bounds, std::vector<Patch>& patches );

private:
	// false — узел дальше диапазона своего уровня, и его площадь рисует родитель
	bool selectNode( const RenderView& view, const Bounds& bounds, uint32_t level, uint32_t x, uint32_t z, std::vector<Patch>& patches );
	void addPatch( uint32_t level, uint32_t x, uint32_t z, uint32_t quarter, std::vector<Patch>& patches );
	bool selectDetailNode( const RenderView& view, const Bounds& bounds, uint32_t depth, uint32_t nx, uint32_t nz,
						   std::vector<Patch>& patches );
	void addDetailPatch( uint32_t depth, uint32_t nx, uint32_t nz, uint32_t quarter, std::vector<Patch>& patches );
	void push( const Patch& patch, std::vector<Patch>& patches );

	DirectX::XMFLOAT2 m_origin = DirectX::XMFLOAT2( 0.0f, 0.0f );
	float m_leafSize = 1.0f;
	float m_extent = 0.0f;
	uint32_t m_levelCount = 0;
	std::vector<uint32_t> m_nodesPerSide;
	Lod m_lod;
	bool m_overflowLogged = false;
};

}
