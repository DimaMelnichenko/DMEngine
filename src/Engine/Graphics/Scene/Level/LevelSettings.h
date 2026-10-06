#pragma once

#include <string>
#include <vector>
#include "DirectX.h"

// Настройки окружения и расстановки уровня — данные строк base.db3 без объектов сцены: их читает и пишет LibraryLoader,
// а объекты (SkyAtmosphere, HDRIBackdrop, PostProcess, Wind, SunPosition, Scatterer) принимают при создании и отдают
// для сохранения. У объектов те же структуры — под именами SkyAtmosphere::Settings и т. п.
namespace GS
{

// Точка правки рельефа (строка TerrainEditPoints): положение (y — высота, м, или у относительной правки — смещение от
// рельефа под точкой), ширина плоской части и полоса перехода к исходному рельефу с каждой стороны, м
struct TerrainEditPoint
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );
	float width = 4.0f;
	float falloff = 4.0f;
};

// Правка рельефа (строка TerrainEdits, как сплайн Landscape Splines с Raise / Lower Terrain и слой Edit Layers в UE):
// кривая через точки — русло, насыпь тропы; одна точка — круглая площадка. Слои — по порядку (TerrainEdits.layer),
// накладывает Scene/Terrain/TerrainEdits. Та же полоса (с полосой перехода) убирает растительность и красит слой
// splat-карты — как Paint Layer у Landscape Splines
struct TerrainEdit
{
	std::string name;
	bool raise = true;		// рельеф ниже кривой поднимается к ней
	bool lower = true;		// выше — опускается
	bool relative = false;	// y точек — смещение от рельефа под точкой (до этой правки), а не высота
	bool smooth = true;		// Catmull-Rom через точки, иначе ломаная
	float clearFoliage = 0.0f;	// 0…1: какую долю растительности (расстановка: трава, лес) правка убирает
	int paintLayer = -1;		// слой материала террейна (TerrainLayers.layer), которым правка красит splat-карту; −1 — нет
	std::vector<TerrainEditPoint> points;
	// Растровая правка (как растровые Edit Layers в UE) вместо кривой: файл карты сдвига высоты, м (< 0 — опустить),
	// R32_FLOAT квадратом, от Textures\ (TerrainEdits.raster; русла — Tools/carve_channels.py). Значения читает
	// CDLODTerrain перед наложением; размер может отличаться от карты высот — выборка билинейная
	std::string raster;
	std::vector<float> rasterValues;
	uint32_t rasterSize = 0;
};

// Строка SkyAtmosphere; без неё — значения по умолчанию
struct SkyAtmosphereSettings
{
	float skyIntensity = 1.0f;	// множитель рассеянного света неба, 1 — по модели
	float haze = 1.0f;			// плотность дымки (аэрозоли Ми)
	float groundAlbedo = 0.25f;	// отражение земли под горизонтом
	// Во сколько раз воздух между камерой и точкой кажется толще (Aerial Perspective View Distance Scale в UE):
	// 1 — по модели (дымка на 1 км — несколько процентов), больше — небольшой мир выглядит как большой, 0 — выключено
	float aerialPerspectiveViewDistanceScale = 1.0f;
	// Свечение ночного неба в зените, кд/м²: собственное свечение атмосферы (airglow), суммарный свет звёзд и
	// зодиакальный свет — ~2·10⁻⁴ на тёмном небе; к горизонту ярче. Видно, когда солнце и луна не светят
	float nightSkyLuminance = 2e-4f;
};

// Строка HDRIBackdrop
struct HDRIBackdropSettings
{
	std::string texture;		// файл от Textures\: .hdr, .exr, .dds
	float intensity = 1.0f;		// кд/м² на единицу значения панорамы (Intensity в UE)
	float rotation = 0.0f;		// поворот панорамы вокруг вертикали, градусы
	float maxLuminance = 0.0f;	// срез яркости для освещения окружением, в единицах панорамы; 0 — без среза
};

enum class Tonemapper : int32_t
{
	none = 0,	// только ограничение 0…1 — для отладки
	aces = 1,	// ACES (подгонка RRT + ODT, S. Hill) — основа Filmic tonemapper UE
	agx = 2		// AgX — стандартное отображение Blender 4+, мягче уводит яркие цвета в белый
};

// Metering Mode в UE: экспозиция задана или замеряется по гистограмме кадра
enum class MeteringMode : int32_t
{
	manual = 0,
	autoHistogram = 1
};

// Ключей кривой компенсации экспозиции не больше — MAX_CURVE_KEYS в Shaders/exposure_adapt.cs
constexpr uint32_t maxExposureCurveKeys = 8;

// Строка PostProcessSettings; без неё — значения по умолчанию (как у Post Process Volume в UE)
struct PostProcessSettings
{
	MeteringMode meteringMode = MeteringMode::autoHistogram;
	float manualEV100 = 15.0f;			// Manual; с него же начинается автоэкспозиция
	float exposureCompensation = 0.0f;	// EV: +1 — вдвое ярче
	// Exposure Compensation Curve: ключи (EV100 сцены, поправка EV) по возрастанию EV100, между ними — линейно,
	// за крайними — значение крайнего; пусто — без кривой. Только для автоэкспозиции
	std::vector<DirectX::XMFLOAT2> exposureCompensationCurve;
	float minEV100 = -10.0f;			// пределы автоэкспозиции и диапазон гистограммы
	float maxEV100 = 20.0f;
	float histogramLowPercent = 10.0f;	// процентили гистограммы: темнее и ярче — не в среднем
	float histogramHighPercent = 90.0f;
	float speedUp = 3.0f;				// скорость адаптации к более яркой сцене, 1/с
	float speedDown = 1.0f;				// к более тёмной
	Tonemapper tonemapper = Tonemapper::agx;
	float bloomIntensity = 0.05f;		// Bloom Intensity — доля энергии над порогом в свечении; 0 — без bloom
	float bloomThreshold = 1.0f;		// Bloom Threshold, яркость после экспозиции; < 0 — без порога
	float purkinjeShift = 1.0f;			// сила ночного зрения 0…1; 0 — цвет как днём при любой яркости
};

// Имена в базе: тонмаппинг None, ACES, AgX (другое — AgX); замер Manual, AutoHistogram (другое — AutoHistogram)
Tonemapper tonemapperFromName( const std::string& name );
const char* tonemapperName( Tonemapper tonemapper );
MeteringMode meteringModeFromName( const std::string& name );
const char* meteringModeName( MeteringMode mode );
// Кривая в базе — текст «EV100,EV; EV100,EV; …»: ключи сортируются, лишние сверх maxExposureCurveKeys отбрасываются
std::vector<DirectX::XMFLOAT2> curveFromText( const std::string& text );
std::string curveText( const std::vector<DirectX::XMFLOAT2>& curve );

// Строка Wind
struct WindSettings
{
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3( 0.0f, 0.0f, 1.0f );	// куда дует, горизонтально (y не учитывается)
	float strength = 0.0f;			// сила изгиба: сдвиг верха растения высотой h — strength · WindWeight · порыв · h²
	float speed = 4.0f;				// скорость волн порывов, м/с
	float minGustAmount = 0.3f;		// порыв между волнами и на гребне волны — доли силы (Min / Max Gust Amount в UE)
	float maxGustAmount = 1.0f;
	float gustSize = 25.0f;			// длина волны порыва, м
};

// Строка WaterSources: источник воды, поставленный руками (родник, ледниковое озеро), — гауссово пятно притока
struct WaterSource
{
	std::string name;
	DirectX::XMFLOAT2 position = DirectX::XMFLOAT2( 0.0f, 0.0f );	// x, z мира
	float rate = 1.0f;				// расход, л/с
	float radius = 3.0f;			// радиус пятна, м (≈ 2σ гаусса)
};

// Строка WaterSimulation: вода на сетке карты высот (WaterSimulation, docs/water.md). Источники — по карте водосбора
// (flow.dds из Tools/gen_heightmap.py): приток нарастает от flowStart до flowFull м² водосбора
struct WaterSimulationSettings
{
	std::string flowMap;			// имя текстуры водосбора в Textures (R32_FLOAT, м²), размер — как у карты высот
	float sourceRate = 0.1f;		// приток клетки при полном водосборе, л/с
	float flowStart = 2000.0f;		// водосбор, с которого начинается приток, м²
	float flowFull = 20000.0f;		// водосбор, при котором приток полный, м²
	float sourceRadius = 3.0f;		// размытие источников вокруг линий стока, м: ручей рождается из пятна, а не из клетки
	float rain = 0.0f;				// дождь — равномерный приток, мм/ч
	float evaporation = 2.0f;		// испарение, мм/ч
	float manning = 0.04f;			// шероховатость дна по Маннингу, с/м^(1/3): горный ручей 0,04–0,07, гладкое дно 0,02
	float timeStep = 0.1f;			// шаг симуляции, с
	float warmupTime = 1800.0f;		// просчёт при загрузке до установившегося течения, с времени симуляции
	float timeScale = 1.0f;			// скорость симуляции в игре относительно времени кадра

	// Материал поверхности (Shaders/water.ps)
	DirectX::XMFLOAT3 absorption = DirectX::XMFLOAT3( 0.45f, 0.09f, 0.06f );	// поглощение по каналам, 1/м
	DirectX::XMFLOAT3 scatterColor = DirectX::XMFLOAT3( 0.25f, 1.0f, 1.0f );	// цвет рассеяния в толще
	float scatterStrength = 0.016f;	// доля освещённости неба, рассеянная толщей к камере
	float roughness = 0.06f;		// шероховатость поверхности: размытие отражения, ширина блика
	float rippleScale = 6.0f;		// метров на повтор ряби
	float rippleStrength = 1.0f;	// рябь быстрой воды
	float calmRipple = 0.25f;		// рябь стоячей (ветер)
	float refraction = 0.02f;		// сдвиг преломления, доля высоты экрана на метр толщины
	float flowPeriod = 1.5f;		// период фазы течения текстуры, с (Vlachos 2010)
	float foamSpeed = 1.0f;			// пена — быстрее этого, м/с
	float foamShear = 1.5f;			// и где сдвиг скорости больше, 1/с

	std::vector<WaterSource> sources;	// включённые строки WaterSources этой строки WaterSimulation
};

// Строка SunPosition
struct SunPositionSettings
{
	float latitude = 0.0f;		// градусы, север > 0
	float longitude = 0.0f;		// градусы, восток > 0
	float timeZone = 0.0f;		// часы от UTC; летнее время — ещё час
	float northOffset = 0.0f;	// поворот севера от +Z вокруг вертикали (к +X), градусы
	int32_t year = 2026;
	int32_t month = 6;
	int32_t day = 21;
	float timeOfDay = 12.0f;	// часы по местным часам, 0…24 (Solar Time в UE)
	float timeScale = 0.0f;		// во сколько раз игровое время быстрее реального; 0 — время стоит
};

// Параметры слоя расстановки — колонки строки ScatterLayers; Scatterer::addLayer переводит их в константы раскладки
// (ScatterPass::PopulateParams)
struct ScatterLayerSettings
{
	float cellSize = 1.0f;			// шаг сетки, метры
	float nearBorder = 0.0f;		// кольцо вокруг камеры, метры
	float farBorder = 0.0f;
	float nearFade = 0.0f;			// ширина плавного исчезания у ближней и дальней границы
	float farFade = 0.0f;
	float sizeMultiplier = 1.0f;
	float jitter = 0.0f;			// смещение внутри ячейки, доля шага
	DirectX::XMFLOAT3 rotationRange = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// предел случайного поворота вокруг осей X, Y, Z, градусы
	bool alignToTerrain = false;	// ось Y инстанса по нормали террейна
	bool castShadow = false;		// слой отбрасывает тень солнца (Cast Shadow в UE)
	// Постоянный слой (лес): экземпляры раскладываются один раз на всю карту кластерами, а не кольцом каждый кадр;
	// кольцо near_border…far_border — дальность прорисовки
	bool persistent = false;
	// Дальше этого расстояния экземпляр рисуется импостером (ImpostorMaterial — последний LOD варианта, запекается при
	// загрузке), м; 0 — без импостера
	float impostorDistance = 0.0f;
	// У видов теней импостер — уже с этого расстояния (ближе — LOD как в кадре): тень дальнего дерева карточкой к свету
	// много дешевле, а собственная тень ближних крон не меняется, м; 0 — как в кадре
	float shadowImpostorDistance = 0.0f;
	// Запекание импостера: множитель доли покрытия (> 1 — крона плотнее: вдали хвоя сливается в сплошную массу, а
	// альфа-тест отбрасывает тексели, закрытые меньше чем наполовину) и сила затенения окружающего света внутри кроны (0…1)
	float impostorDensity = 1.0f;
	float impostorOcclusion = 0.0f;
};

}
