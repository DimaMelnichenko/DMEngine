#pragma once
#include <vector>
#include "DirectX.h"
#include "Utils\utilites.h"
#include "DMComputeShader.h"
#include "RenderView.h"

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
	static constexpr uint32_t maxLods = 4;
	static constexpr uint32_t maxVariants = 8;
	static constexpr uint32_t pairCount = maxVariants * maxLods;	// пар «вариант × LOD»
	static constexpr uint32_t maxLists = pairCount * 2;				// обычные списки и списки перехода; MAX_LISTS в Shaders\scatter.cs
	static constexpr uint32_t maxSections = 4;	// MAX_SECTIONS в Shaders\scatter.cs
	static constexpr uint32_t maxViews = maxRenderViews;	// MAX_VIEWS
	static constexpr uint32_t maxGroups = maxLists * maxSections;	// MAX_GROUPS: секций списков — команд на вид
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
	// Своя раскладка варианта (Variant::sizeMultiplier … patchGroup) — из GUI; буфер вариантов — при смене. Группа куртин
	// меняет ёмкость списков при куртинах — после неё буферы заново (createBuffers)
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
	void populate( DMComputeShader& shader, uint16_t gridDim );
	// После расстановки: команды ExecuteIndirect по видам и группам (Shaders\scatter.cs, buildCommands)
	void buildCommands( DMComputeShader& shader );

	// Постоянный слой: пул и кластеры на мир worldSize × worldSize м (после createBuffers); margin — наибольший радиус
	// экземпляра, запас границ кластера по XZ; capacity — ёмкость пула (0 — экземпляр на ячейку мира, как у раскладки по маске)
	bool createPersistent( float worldSize, float margin, uint32_t capacity = 0 );
	bool persistent() const { return m_populateParams.clusterCount > 0; }
	// Объявление прохода раскладки на карту (пишет постоянный пул, кластеры и их счётчик) и сама раскладка (placeWorld)
	PassDesc placeDesc( const char* name ) const;
	void place( DMComputeShader& shader );
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
	void cull( DMComputeShader& shader );
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
	// Параметры слоя для Shaders\scatter.cs (cbuffer ScatterLayerBuffer, b4)
	struct alignas( 16 ) PopulateParams
	{
		float nearBorder;		// кольцо вокруг камеры, метры
		float farBorder;
		float nearFade;			// ширина плавного исчезания у ближней и дальней границы
		float farFade;
		float sizeMultiplier;
		float cellSize;			// шаг сетки, метры
		float jitter;			// смещение внутри ячейки, доля шага
		float alignToTerrain;	// 1 — ось Y инстанса по нормали террейна
		DirectX::XMFLOAT3 rotationRange;	// предел случайного поворота вокруг осей X, Y, Z, радианы
		float castShadow;		// 1 — слой отбрасывает тень солнца (Cast Shadow в UE): инстансы попадают в списки видов теней
		uint32_t variantCount;	// заполняет createBuffers()
		uint32_t itemCapacity;		// ёмкость пула инстансов — createBuffers()
		uint32_t transitionCapacity;// ёмкость пула перехода — createBuffers()
		uint32_t indexStride;		// индексов на вид — createBuffers()
		// Постоянный слой — createPersistent(): кластеров по X и всего (0 — слой кольцевой), ёмкость постоянного пула,
		// запас границ кластера по XZ, м
		uint32_t clustersX;
		uint32_t clusterCount;
		uint32_t placedCapacity;
		float clusterMargin;
		float sizeVariation;	// разброс размера: случайный множитель от 1 − разброс до 1
		// Цвет земли у растения (Shaders\scatter_ground.sh): доля у корня, высота перехода, м, доля вдали, разброс яркости
		float groundColor;
		float groundColorHeight;
		float groundColorFar;
		float colorVariation;
		// Края кольца и прореживание (Shaders\scatter.cs: ringFade, thinOut): разброс своих дальностей исчезания по полосе
		// исчезания (0…1), начало прореживания, м, и доля убранных у дальней границы
		float edgeSpread;
		float thinStart;
		float thinning;
		float ditherFade;	// 1 — у краёв кольца исчезание долей пикселей (дизеринг): экземпляры полосы — в пуле перехода
		// Куртины (ScatterLayerSettings::patchScale, patchStrength): размер пятен шума, м (0 — без куртин), и сила 0…1
		float patchScale;
		float patchStrength;
		float padding;
	} m_populateParams;
	static_assert( sizeof( PopulateParams ) == 128, "ScatterLayerBuffer layout" );

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

	// cbuffer ScatterVariantsBuffer в Shaders\scatter.cs (b7)
	struct alignas( 16 ) VariantsBuffer
	{
		// x — накопленная доля варианта (0…1), y — число LOD, z — 1: смена LOD дизерингом, w — с этого расстояния у
		// видов теней последний LOD (Variant::shadowLastLodDistance; 0 — как у главного)
		DirectX::XMFLOAT4 variants[maxVariants];
		DirectX::XMFLOAT4 lodEnd[maxVariants];		// дальности LOD 0…2 варианта, м
		DirectX::XMFLOAT4 shadowLodEnd[maxVariants];	// они же у видов теней (setShadowLods)
		DirectX::XMFLOAT4 shadowCasting[maxVariants];	// x — с этого LOD у видов теней экземпляра нет (setShadowLods)
		DirectX::XMFLOAT4 bounds[maxVariants];		// сфера модели варианта (Variant::bounds)
		DirectX::XMFLOAT4 footprints[maxVariants];	// след основания варианта (Variant::footprint)
		// Своя раскладка: x — вес (≥ 0), y — × размер слоя, z — разброс размера (< 0 — слоя), w — наклон по земле (< 0 —
		// слоя, 0 / 1)
		DirectX::XMFLOAT4 placement[maxVariants];
		uint32_t patchGroups[maxVariants][4];		// x — группа куртин (номер узора шума)
		// y — ёмкость списка индексов (на вид), z — число секций, w — начало списка в индексах вида
		uint32_t lists[maxLists][4];
	};

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
