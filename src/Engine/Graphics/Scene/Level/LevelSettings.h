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

// Материал и высота террейна — колонки строки Terrain и tiling строк TerrainLayers: читает CDLODTerrain, правки окна
// террейна пишет «Save level»
struct TerrainSettings
{
	uint32_t id = 0;					// строка Terrain
	float heightMultiplier = 1.0f;
	float triplanarSharpness = 8.0f;	// резкость смены проекций triplanar на склонах
	float heightBlend = 0.2f;			// глубина смешения слоёв по высоте
	float farTextureScale = 8.0f;		// второй масштаб текстур вдали (distance resampling)
	float farBlendStart = 40.0f;		// полоса перехода ко второму масштабу, м
	float farBlendEnd = 120.0f;
	std::vector<float> layerTiling;		// метров на повтор по номеру слоя (TerrainLayers.layer); 0 — слоя нет
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

// Строка VolumetricCloud (как Volumetric Cloud в UE5): слой облаков над долиной, Shaders/volumetric_cloud.sh
struct VolumetricCloudSettings
{
	float layerBottomAltitude = 1500.0f;	// высота основания слоя над землёй мира (y = 0), м (Layer Bottom Altitude)
	float layerHeight = 2500.0f;			// толщина слоя, м (Layer Height)
	float coverage = 0.3f;					// покрытие неба 0…1: 0 — ясно, 1 — сплошь
	float density = 0.02f;					// коэффициент ослабления облака полной плотности, 1/м
	DirectX::XMFLOAT3 albedo = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );	// доля рассеянного в ослабленном
	float shapeScale = 8000.0f;				// повтор шума формы, м: ячейка облака — четверть
	float detailScale = 800.0f;				// повтор мелкого шума краёв, м
	float weatherScale = 40000.0f;			// повтор карты погоды (просветы и гуще), м
	float windSpeed = 10.0f;				// скорость облаков, м/с; направление — ветра уровня
	float shadowStrength = 1.0f;			// тень облаков на земле: множитель оптической толщины, 0 — нет
	float tracingMaxDistance = 50000.0f;	// дальше облака не видны, м (Tracing Max Distance)
};

// Слой тумана по высоте: ниже своей высоты ровный, выше редеет по экспоненте
struct HeightFogLayer
{
	float density = 0.0f;			// коэффициент ослабления, 1/м: видимость в слое ~3 / density
	float height = 0.0f;			// до этой высоты, м, плотность ровная…
	float heightFalloff = 0.01f;	// …выше спадает: в e раз на 1 / heightFalloff м
};

// Строка ExponentialHeightFog (как Exponential Height Fog с Volumetric Fog в UE): туман уровня, Shaders/height_fog.sh
struct HeightFogSettings
{
	HeightFogLayer layer;			// дымка над долиной (Fog Density, Fog Height Falloff)
	HeightFogLayer secondLayer;		// туман, налитый в низины (Second Fog Data)
	DirectX::XMFLOAT3 albedo = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );	// доля рассеянного в ослабленном (Albedo)
	float scatteringDistribution = 0.2f;	// анизотропия g: > 0 — туман светится вокруг солнца
	bool volumetric = true;			// объём со светом ламп и тенями (Volumetric Fog), иначе только по формуле
	float viewDistance = 200.0f;	// дальность объёма по глубине взгляда, м (View Distance)
};

// Строка WaterSources: источник воды, поставленный руками (родник, ледниковое озеро), — гауссово пятно притока
struct WaterSource
{
	std::string name;
	DirectX::XMFLOAT2 position = DirectX::XMFLOAT2( 0.0f, 0.0f );	// x, z мира
	float rate = 1.0f;				// расход, л/с
	float radius = 3.0f;			// радиус пятна, м (≈ 2σ гаусса)
};

// Точка оси ручья (строка WaterStreamPoints, Tools/carve_channels.py): лента воды вдоль ручья — StreamRibbons
struct WaterStreamPoint
{
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// x, z — ось ручья; y — уровень воды, м
	float halfWidth = 1.0f;			// полуширина ленты, м: до края ложбины и под берег
	float speed = 0.0f;				// скорость течения, м/с
	float foam = 0.0f;				// пена 0…1 (крутой участок)
};

// Ручей (строка WaterStreams): точки оси от истока вниз
struct WaterStream
{
	std::vector<WaterStreamPoint> points;
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

	// Режим static (колонка mode): вода не течёт — озёра наливаются при загрузке до перелива, ручьи — ленты по точкам
	// streams (WaterStreams), их вода для травы, мокрой земли и брызг — растр staticWater (Tools/carve_channels.py).
	// Режим simulated — вода течёт по рельефу (трубы, Маннинг)
	bool staticWater = false;
	std::string staticWaterMap;		// имя текстуры в Textures: R — уровень воды ручья, м; G, B — скорость X, Z
	std::vector<WaterStream> streams;
};

// Эмиттер частиц (строки ParticleEmitters и экземпляр LevelParticleEmitters, ParticleSystem, docs/particles.md)
struct ParticleEmitterSettings
{
	// Где рождаются: в точке экземпляра, в шаре вокруг неё, на поле вокруг камеры (по маске плотности — пыльца над
	// лугом, хвоя под ельником) или на быстрой воде симуляции (брызги на перекатах)
	enum class Spawn { point, sphere, camera, water };
	// Как выглядят: мягкое пятно к камере или узкая карточка, вытянутая вдоль скорости (хвоинка)
	enum class Shape { dot, needle };

	uint32_t id = 0;				// строка ParticleEmitters — куда пишет «Save level»
	std::string name;
	Spawn spawn = Spawn::point;
	Shape shape = Shape::dot;
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// экземпляр, м мира (point, sphere)
	float radius = 1.0f;			// шар (sphere), поле вокруг камеры (camera, water), м; дальше от камеры — гибнут
	float heightMin = 0.0f;			// над рельефом (camera) или над поверхностью воды (water), м
	float heightMax = 0.0f;
	std::string mask;				// текстура плотности в координатах карты высот (camera); пусто — всюду
	float rate = 10.0f;				// рождений в секунду; у camera — на 100 м² поля
	uint32_t maxParticles = 4096;
	float lifetimeMin = 1.0f;		// с
	float lifetimeMax = 2.0f;
	float sizeStart = 0.05f;		// м
	float sizeEnd = 0.05f;
	DirectX::XMFLOAT3 color = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );	// альбедо, линейное
	float alpha = 1.0f;
	float fadeIn = 0.1f;			// доли жизни: появление и угасание
	float fadeOut = 0.2f;
	DirectX::XMFLOAT3 velocity = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// начальная, м/с
	float velocitySpread = 0.0f;	// случайная добавка, м/с
	float gravity = 0.0f;			// м/с² вниз
	float drag = 0.0f;				// 1/с: скорость стремится к скорости воздуха (ветер) и воды
	float wind = 0.0f;				// доля ветра уровня
	float curl = 0.0f;				// сила вихрей (curl noise), м/с
	float curlScale = 4.0f;			// размер вихрей, м
	float waterFlow = 0.0f;			// доля скорости воды симуляции под частицей
	float waterSpeed = 1.0f;		// water: порог скорости воды, м/с
	bool collide = false;			// с рельефом: needle ложится, dot гибнет
	float transmission = 0.0f;		// просвет на солнце (пыльца, пух светятся против солнца)
	float emissive = 0.0f;			// свечение, кд/м² (светлячки)
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
	uint32_t id = 0;				// строка ScatterLayers — куда пишет «Save level»
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

// Вариант слоя расстановки — строка ScatterLayerModels: доля ячеек и тень (то, что пишет «Save level»)
struct ScatterModelSettings
{
	uint32_t id = 0;
	float weight = 1.0f;
	bool castShadow = true;
};

// Слой расстановки для сохранения уровня: строка ScatterLayers и строки её моделей
struct ScatterLayerRecord
{
	ScatterLayerSettings settings;
	std::vector<ScatterModelSettings> models;
};

}
