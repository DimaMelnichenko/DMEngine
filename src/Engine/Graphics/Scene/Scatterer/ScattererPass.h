#pragma once
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "ComputeShader.h"
#include "RenderView.h"
#include "Shaders\scatter_constants.h"

namespace GS
{

// Буферы и параметры слоя расстановки. Экземпляр растения раскладывается один раз за кадр в пул инстансов; у каждого вида
// кадра (главная камера и каскады теней, maxRenderViews) — свои списки индексов «вариант × LOD»: экземпляр входит в
// список вида, если виден в нём, LOD — по расстоянию до точки LOD с множителем вида. При смене LOD у экземпляра
// меняется только меш — положение, поворот и размер в пуле те же. Секции LOD (меши со своими материалами: стебель и
// лепестки) рисуют один список; секции с одним материалом и состоянием по всем спискам — группа (Scatterer::groups):
// на группу и вид — один ExecuteIndirect со счётчиком, команды которого пишет Shaders\scatter.cs (buildCommands):
// {начало списка индексов — root-константа b9 вершинного шейдера, DrawIndexedInstanced}. Экземпляры в полосе смены LOD
// (Dithered LOD Transition в UE, Shaders/lod_transition.h) лежат в пуле перехода с долей перехода: такой экземпляр есть
// в списках перехода обоих LOD, и каждый рисует свою долю пикселей.
// Постоянный слой (лес) раскладывается один раз на всю карту (place) в постоянный пул кластерами по clusterCells²
// ячеек, а каждый кадр (cull) экземпляры видимых кластеров идут в тот же пул кадра и списки видов, что у кольцевого слоя
class ScatterPass
{
public:
	static constexpr uint32_t maxLods = SCATTER_MAX_LODS;
	static constexpr uint32_t maxVariants = SCATTER_MAX_VARIANTS;
	static constexpr uint32_t pairCount = maxVariants * maxLods;	// пар «вариант × LOD»
	static constexpr uint32_t maxLists = pairCount * 2;				// обычные списки и списки перехода
	static constexpr uint32_t maxSections = SCATTER_MAX_SECTIONS;
	static constexpr uint32_t maxViews = maxRenderViews;
	static constexpr uint32_t maxGroups = maxLists * maxSections;	// секций списков — команд на вид
	static_assert( maxLists == SCATTER_MAX_LISTS && maxGroups == SCATTER_MAX_GROUPS, "Shaders\\scatter_constants.h" );
	// Ёмкость пула инстансов слоя и списков индексов вида; делится между списками по ожидаемому числу инстансов
	static constexpr uint32_t capacity = 262144;
	// Постоянный слой: ячеек по стороне кластера (PLACED_CLUSTER_CELLS в Shaders\scatter.cs) и наибольший постоянный пул
	static constexpr uint32_t clusterCells = 8;
	static constexpr uint32_t maxPlaced = 4194304;

	// Модель слоя для раскладки: доля по весу и дальности LOD (ModelProperties.range; последний — до конца кольца)
	struct Variant
	{
		float weight = 1.0f;
		uint32_t lodCount = 1;
		float lodEnd[maxLods] = {};
		uint32_t sectionCount[maxLods] = { 1, 1, 1, 1 };	// секций у LOD, не больше maxSections
		// Сфера модели (LOD0) в её координатах: xyz — центр, w — радиус. По ней — отсечение экземпляра и длина его тени
		DirectX::XMFLOAT4 bounds = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
		// След основания модели на земле (LOD0, AbstractMesh::baseBounds): x, y — центр по X и Z, z — радиус по XZ. По нему
		// раскладка проверяет русла и воду под всей моделью, а не только в точке экземпляра
		DirectX::XMFLOAT4 footprint = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
		// С этого расстояния виды теней берут последний LOD (у леса — импостер), без перехода дизерингом, м; 0 — LOD
		// как у главного вида
		float shadowLastLodDistance = 0.0f;
		// Своя раскладка (ScatterVariantPlacement): × размер слоя, разброс размера (< 0 — слоя), наклон по земле (< 0 —
		// слоя, 0 / 1) и группа куртин (номер узора шума: у моделей одного вида — один)
		float sizeMultiplier = 1.0f;
		float sizeVariation = -1.0f;
		float alignToTerrain = -1.0f;
		uint32_t patchGroup = 0;
		// Предпочтение слоёв земли: бит — слой материала террейна (0 — нет) и сила 0…1: вес × (1 − сила + сила × доля слоёв)
		uint32_t groundLayers = 0;
		float groundPreference = 0.0f;
	};

	ScatterPass();
	ScatterPass( const ScatterPass& ) = delete;
	~ScatterPass();

	// Параметры слоя (populateParams) уже заданы: по кольцу и дальностям LOD делится ёмкость. cellBound — ёмкость списка
	// по числу ячеек в полосе его LOD (не больше экземпляра на ячейку): у постоянного слоя кольцо — вся дальность леса,
	// и доля от capacity у ближних LOD была бы меньше, чем в них помещается деревьев
	bool createBuffers( const std::vector<Variant>& variants, bool cellBound = false );
	// Список пары «вариант × LOD»: обычный или перехода (экземпляры в полосе смены LOD)
	static uint32_t listIndex( uint32_t variant, uint32_t lod, bool transition = false )
	{
		return ( transition ? pairCount : 0 ) + variant * maxLods + lod;
	}
	// Смена LOD дизерингом у варианта (у всех его секций материал с DitheredLODTransition): без неё списки перехода
	// пусты и LOD сменяется мгновенно на дальности экземпляра. Буфер вариантов обновляется, когда флаг меняется
	void setDitheredLodTransition( uint32_t variant, bool dithered );
	// Дальность последнего LOD у видов теней (Variant::shadowLastLodDistance) — из GUI; буфер вариантов — при смене
	void setShadowLastLodDistance( uint32_t variant, float distance );
	// LOD варианта у видов теней (из GUI каждый кадр): границы LOD модели (первые modelBoundaries — без перехода к
	// импостеру и конца кольца) × scale, детальнее minLod — нет (границы 0), с castingLods — экземпляра у теней нет
	// (импостер без тени). Со своими дальностями переход у видов теней — без дизеринга
	struct ShadowLods
	{
		float scale = 1.0f;					// DMModel::shadowLodScale × «Shadow LOD scale» набора
		uint32_t minLod = 0;				// DMModel::shadowMinLod
		uint32_t modelBoundaries = 0;
		uint32_t castingLods = maxLods;
	};
	void setShadowLods( uint32_t variant, const ShadowLods& lods );
	// Своя раскладка варианта (Variant::sizeMultiplier … groundPreference) — из GUI; буфер вариантов — при смене. Группа
	// куртин и предпочтение слоёв меняют ёмкость списков — после них буферы заново (createBuffers)
	void setVariantPlacement( uint32_t variant, const Variant& placement );
	// Секция списка: её меш и группа (0…maxGroups − 1, Scatterer::groups); таблицы секций и групп уходят на GPU в
	// populate, только когда менялись
	void setSectionArgs( uint32_t list, uint32_t section, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset, uint32_t group );
	// Секции списка нет (LOD без неё): команды на неё не будет
	void clearSectionArgs( uint32_t list, uint32_t section );
	// Счётчики пулов, списков и групп — в ноль перед раскладкой (ClearUnorderedAccessViewUint)
	void resetCounters();
	// Объявление прохода раскладки (GpuPass.h): что слой пишет — счётчики, пулы, индексы, команды; что читает, добавляет Scatterer
	PassDesc passDesc( const char* name ) const;
	// Расставляет инстансы слоя: gridDim × gridDim ячеек сетки вокруг камеры
	void populate( ComputeShader& shader, uint16_t gridDim );
	// После расстановки: команды ExecuteIndirect по видам и группам (Shaders\scatter.cs, buildCommands). Последняя запись
	// слоя в кадре: пулы, списки видов, команды и счётчики дальше только читаются — их барьеры встают в очередь
	// (DMD3D::prepareShaderRead) и уходят группой в начале следующего прохода, а не перед первым ExecuteIndirect в каскаде
	void buildCommands( ComputeShader& shader );

	// Постоянный слой: пул и кластеры на мир worldSize × worldSize м (после createBuffers); margin — наибольший радиус
	// экземпляра, запас границ кластера по XZ; capacity — ёмкость пула (0 — экземпляр на ячейку мира, как у раскладки по маске)
	bool createPersistent( float worldSize, float margin, uint32_t capacity = 0 );
	bool persistent() const { return m_populateParams.clusterCount > 0; }
	// Объявление прохода раскладки на карту (пишет постоянный пул, кластеры и их счётчик) и сама раскладка (placeWorld)
	PassDesc placeDesc( const char* name ) const;
	void place( ComputeShader& shader );
	// Экземпляр постоянного слоя, разложенный не по маске, а на CPU (камни русел): положение основания, размер — множитель
	// модели, поворот кватернионом, вариант и своя доля дальностей LOD
	struct PlacedInstance
	{
		DirectX::XMFLOAT3 position;
		float size;
		DirectX::XMFLOAT4 rotation;
		uint32_t variant;
		float lodScale;
	};
	// Вместо place: экземпляры — кластерами по сетке мира в постоянный пул, их высоты — по сферам вариантов; запас границ
	// кластера — по самому широкому экземпляру. Сколько не поместилось в пул — в возврат (0 — все)
	size_t placeInstances( const std::vector<PlacedInstance>& instances, const std::vector<Variant>& variants );
	// Каждый кадр: экземпляры видимых кластеров — в пул кадра и списки видов (cullPlaced); что читает — placedReads
	void cull( ComputeShader& shader );
	std::vector<PassDesc::Read> placedReads() const;

	// Для вершинного шейдера: пул инстансов (InstanceParam с INST_POS, INST_SCALE и INST_ROTATE — Shaders\instance.sh),
	// пул перехода (ещё LOD_DITHER) и списки индексов всех видов (SLOT_INSTANCE_INDICES; начало списка — root-константа
	// команды)
	const ShaderView& items() const { return m_items.srv; }
	const ShaderView& transitions() const { return m_transitions.srv; }
	const ShaderView& indices() const { return m_indices.srv; }
	// Команды и счётчики групп для DMD3D::drawIndexedInstancedIndirectCount
	const Buffer& commands() const { return m_commands; }
	const Buffer& counters() const { return m_counters; }
	uint32_t commandsOffset( uint32_t view, uint32_t group ) const { return ( view * maxGroups + m_groupBase[group] ) * commandStride; }
	static uint32_t groupCountOffset( uint32_t view, uint32_t group )
	{
		return 8 + maxViews * maxLists * 4 + ( view * maxGroups + group ) * 4;
	}
	uint32_t groupCapacity( uint32_t group ) const { return m_groupCapacity[group]; }

public:
	// Параметры слоя для Shaders\scatter.cs — ScatterLayerBuffer (b4, Shaders\scatter_constants.h)
	using PopulateParams = ScatterLayerBuffer;
	PopulateParams m_populateParams = {};

	PopulateParams& populateParams();

private:

	// Совпадает с InstanceParam в Shaders\instance.sh при INST_POS, INST_SCALE и INST_ROTATE (INST_SCATTER)
	struct ScatterItem
	{
		DirectX::XMFLOAT3 position;
		float size;
		DirectX::XMFLOAT4 rotation;	// кватернион
		uint32_t ground[4] = {};	// цвет земли под экземпляром (Shaders\scatter_ground.sh); нули — свой цвет
	};
	static_assert( sizeof( ScatterItem ) == 48, "ScatterItem in Shaders\\scatter.cs" );

	// Экземпляр постоянного пула — PlacedItem в Shaders\scatter.cs: размер без исчезания у краёв кольца, вариант и своя
	// доля дальностей LOD
	struct PlacedItem
	{
		ScatterItem item;
		uint32_t variant;
		float lodScale;
		float padding[2];
	};

	// Экземпляр пула перехода: то же и доля смены LOD — InstanceParam с LOD_DITHER
	struct ScatterTransitionItem
	{
		ScatterItem item;
		float lodDither;	// (0; 1) — уходящий LOD, (−1; 0) — приходящий (Shaders\lod_dither.sh)
		DirectX::XMFLOAT3 padding;
	};

	static constexpr uint32_t commandStride = 24;	// root-константа + D3D12_DRAW_INDEXED_ARGUMENTS (COMMAND_STRIDE)
	static constexpr uint32_t countersSize = ( 2 + maxViews * maxLists + maxViews * maxGroups ) * sizeof( uint32_t );

	// Варианты и списки — ScatterVariantsBuffer (b7, Shaders\scatter_constants.h)
	using VariantsBuffer = ScatterVariantsBuffer;

	// Буфер с UAV для раскладки и SRV для вершинного шейдера
	struct PoolBuffer
	{
		Buffer buffer;
		StorageView uav;
		ShaderView srv;
	};
	bool createPool( PoolBuffer& pool, uint32_t stride, uint32_t count, const char* name );
	// Таблица групп по таблице секций: команды группы — подряд в командах вида
	void rebuildGroups();
	// Параметры слоя, варианты и таблицы секций и групп — на GPU (что менялось) и в слоты b4, b7
	void bindParams();

	VariantsBuffer m_variants = {};
	bool m_variantsChanged = false;
	PoolBuffer m_items;
	PoolBuffer m_transitions;
	PoolBuffer m_indices;
	// Постоянный слой: экземпляры кластерами, кластеры (начало, число, высоты) и счётчик постоянного пула
	PoolBuffer m_placed;
	PoolBuffer m_clusters;
	Buffer m_placedCounter;
	StorageView m_placedCounterUAV;
	Buffer m_counters;
	StorageView m_countersUAV;
	Buffer m_commands;
	StorageView m_commandsUAV;
	// Секции списков (x — индексов, y — начало индексов, z — начало вершин, w — группа + 1) и группы (x — начало команд
	// в командах вида, y — ёмкость): копия на CPU и буферы на GPU, обновляемые при смене
	uint32_t m_sectionArgs[maxLists * maxSections][4] = {};
	uint32_t m_groups[maxGroups][4] = {};
	uint32_t m_groupBase[maxGroups] = {};
	uint32_t m_groupCapacity[maxGroups] = {};
	bool m_tablesChanged = true;
	Buffer m_sectionArgsBuffer;
	ShaderView m_sectionArgsSRV;
	Buffer m_groupsBuffer;
	ShaderView m_groupsSRV;
	Buffer m_populateParamsBuffer;
	Buffer m_variantsBuffer;
};

}
