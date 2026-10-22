#pragma once

#include "Properties\SettingsSchema.h"
#include <optional>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Engine\Audio\SoundRules.h"

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
	// R32_FLOAT квадратом, от Textures\ (TerrainEdits.raster); русла — такая же правка в памяти (TerrainHydrology). Значения читает
	// TerrainPipeline перед наложением; размер может отличаться от карты высот — выборка билинейная
	std::string raster;
	std::vector<float> rasterValues;
	uint32_t rasterSize = 0;
};

// Эрозия карты высот — строка TerrainErosion, на которую ссылается Terrain.erosion (NULL — карта уже готова, эрозии нет):
// первая ступень конвейера рельефа и воды (TerrainErosion, docs/terrain.md, «Эрозия»). Применяется при загрузке уровня
struct TerrainErosionSettings
{
	uint32_t id = 0;				// строка TerrainErosion
	int32_t droplets = 1000000;		// капель воды
	uint32_t seed = 7;
	int32_t lifetime = 80;			// шагов капли (тексель за шаг)
	float inertia = 0.3f;			// доля прежнего направления
	float capacity = 1.0f;			// ёмкость переноса: уклон · скорость · вода · capacity
	float minSlope = 0.01f;			// уклон в ёмкости — не меньше (капля несёт и по ровному)
	float erodeSpeed = 0.1f;		// доля недобора ёмкости, размываемая за шаг (capacity · erode_speed ≪ 1: иначе гребни — плато)
	float depositSpeed = 0.2f;		// доля излишка, откладываемая за шаг
	float evaporation = 0.01f;		// вода испаряется за шаг
	float gravity = 4.0f;			// разгон вниз по склону
	int32_t radius = 5;				// кисть размыва, тексели
	float rainScale = 250.0f;		// размер пятен дождя, м
	float rainMin = 0.25f;			// доля дождя в сухих местах (1 — дождь всюду поровну)
	float talusAngle = 38.0f;		// угол естественного откоса осыпей, градусы
	int32_t thermalIterations = 40;	// шагов осыпания
	float thermalRate = 0.25f;		// доля излишка, сползающая за шаг
};
// Поля строки TerrainErosion (SettingsSchema), подокно «Erosion»
const SettingsSchema<TerrainErosionSettings>& terrainErosionSchema();

// Правила слоёв и масок по рельефу (TerrainMasks, docs/terrain.md, «Слои и маски по рельефу») — строка TerrainLayerRules
// террейна (нет строки или колонки — значения по умолчанию), подокно «Layer rules» с пересчётом вживую. Высоты — доли
// height_multiplier (0…1, как исходная карта), уклоны — градусы; диапазон — переход smoothstep от from к to
struct TerrainLayerRules
{
	using Range = SettingRange;
	// Скала и снег
	Range rockSlope{ 27.0f, 37.0f };		// скала — крутые склоны, уклон ± шум rockNoise, градусы
	float rockNoise = 10.0f;
	Range bedrockWear{ 1.0f, 3.0f };		// и коренная порода в промоинах (размыв эрозии) на средних уклонах — доля bedrockShare
	Range bedrockSlope{ 14.0f, 24.0f };
	float bedrockShare = 0.8f;
	Range snowHeight{ 0.80f, 0.88f };		// снег — по высоте ± шум snowNoise, на северных склонах ниже на snowNorth
	float snowNorth = 0.06f;
	float snowNoise = 0.08f;
	float rockClearsSnow = 0.7f;			// доля снега, которую убирает скала
	// Камни и галька (слой осыпей)
	Range talusMap{ 0.15f, 0.8f };			// осыпь эрозии у подножий скал
	Range boulderSlope{ 18.0f, 25.0f };		// и камни на крутоватых склонах (уклон ± шум boulderNoise) — доля boulderShare
	float boulderNoise = 6.0f;
	float boulderShare = 0.7f;
	Range gullyFlow{ 3.3f, 4.0f };			// галька промоин: log10 водосбора, м², на склонах gullySlope — доля gullyShare
	Range gullySlope{ 6.0f, 12.0f };
	float gullyShare = 0.8f;
	Range pebblePatches{ 0.62f, 0.72f };	// пятна гальки по шуму (0…1) на склонах pebblePatchSlope — доля pebblePatchShare
	Range pebblePatchSlope{ 8.0f, 16.0f };
	float pebblePatchShare = 0.6f;
	// Травы и цветы
	Range moistureTwi{ 4.0f, 8.0f };		// влажность — TWI = ln( a / tan β )
	Range wetGrass{ 0.4f, 0.6f };			// сочная трава: влажность × wetGrassMoisture + шум × ( 1 − wetGrassMoisture )
	float wetGrassMoisture = 0.6f;
	Range flowers{ 0.62f, 0.72f };			// цветы (mask_camomile): шум + влажность × flowersMoisture
	float flowersMoisture = 0.15f;
	// Лес
	Range treeline{ 0.72f, 0.79f };			// граница леса: высота + шум × treelineNoise + север × treelineNorth
	float treelineNoise = 0.08f;
	float treelineNorth = 0.04f;
	Range forestSlope{ 30.0f, 38.0f };		// круче (± шум forestSlopeNoise) — леса нет
	float forestSlopeNoise = 6.0f;
	float stonesClearForest = 0.8f;			// доля леса, которую убирают осыпи
	Range valleyFloorHeight{ 0.32f, 0.22f };// дно долины — луга с редкими рощами
	Range valleyFloorSlope{ 9.0f, 4.0f };
	Range clearings{ 0.17f, 0.33f };		// поляны: шум − дно долины × valleyClearings; ниже from — поляна
	float valleyClearings = 0.42f;
	Range spruceHeight{ 0.25f, 0.55f };		// ель — выше (и на северных склонах)
	Range pineSlope{ 8.0f, 20.0f };			// сосна — на средних сухих склонах к солнцу
	Range pineHeight{ 0.66f, 0.45f };
	Range birchMoisture{ 0.2f, 0.7f };		// берёза — во влажных низинах
	Range birchHeight{ 0.45f, 0.25f };
	Range birchSlope{ 20.0f, 8.0f };
	float standContrast = 3.0f;				// степень доли типа: больше — чище древостои, уже смешанная полоса
	// Кусты
	float bankWidth = 3.0f;					// полоса берегов — на столько текселей шире русла
	Range riparianDensity{ 0.30f, 0.50f };	// кусты у русел — пятнами по шуму + влажности × 0,2
	Range riparianGrass{ 0.25f, 0.55f };	// высокая трава у воды — та же полоса, шум только прореживает
	Range riparianHeight{ 0.78f, 0.70f };	// кусты и трава у воды — ниже этой высоты
	// Отступ от воды, м: маска нарастает от 0 у воды до полной на этом расстоянии (0 — до самого уреза: в воде и в русле
	// растения убирает расстановка по глубине и врезу)
	float bankGrassGap = 0.0f;
	float bankShrubsGap = 2.0f;
	// Дно под водой (наносы; не маски, а покраска дна — TerrainDetailSplat и terrain.ps): у нижних озёр мелочь, которую волны
	// не вымывают, ложится глубже уреза — галька у берега, глубже дресва (lake_gravel), ещё глубже ил (lake_silt); у
	// медленных участков ручьёв дно глубже краёв — дресва. Диапазоны — от 0 при from к 1 при to
	Range lakeSedimentHeight{ 0.35f, 0.25f };	// наносы у озёр с уровнем воды ниже, доля высоты: верхние озёра каменистые
	Range lakeGravelDepth{ 0.10f, 0.25f };		// галька → дресва по глубине воды, м
	Range lakeSiltDepth{ 0.50f, 0.90f };		// дресва → ил по глубине воды, м
	Range streamGravelSpeed{ 0.50f, 0.30f };	// дресва на дне ручья, где течение медленнее, м/с (плёсы, подпор у озера)
	Range streamGravelDepth{ 0.05f, 0.15f };	// и вода глубже (середина дна, не края), м
	// Берег озёр (docs/scatter.md, «Вода»): полоса от уреза до перелива (запас берега lake_freeboard) — то, что вода заливает
	// в паводок: трава луга там не растёт (слои с водой «Above the lake overflow»), рогоз — в ней и на мелководье (слои
	// «Shallows») до этой глубины воды, м: от полной плотности до нуля
	Range lakeShallowDepth{ 0.5f, 0.9f };
	// Полоса — не шире этого от воды, м: на плоском дне долины земля ниже перелива тянется на десятки метров, и трава луга
	// пропадала бы на всей низине
	float lakeShoreWidth = 3.0f;
	// Мокрая полоса над водой (docs/terrain.md, «Детальный слой весов»): на столько над урезом мокро, м, выше — сухо с резким
	// краем шириной wetBankEdge (летнее солнце быстро сушит камни); у падения воды — выше ещё на wetSplashHeight × доля по
	// высоте уступа (wetSplashFall: перепад уровня между соседними текселями 0,25 м, от → до): брызги только у водопадов
	float wetBankHeight = 0.04f;
	float wetBankEdge = 0.01f;
	float wetSplashHeight = 0.2f;
	Range wetSplashFall{ 0.08f, 0.2f };
	float wetRoughness = 0.2f;		// шероховатость мокрой земли (вода в порах — гладкая, блик)
	Range krummholzHeight{ 0.73f, 0.78f };	// стланик над границей леса — от, до верха krummholzTop
	Range krummholzTop{ 0.84f, 0.79f };
	Range dryGapsSlope{ 12.0f, 20.0f };		// кусты прогалин сухих склонов — доля dryGapsShare
	float dryGapsShare = 0.5f;
	// Ветер: поле ветра по рельефу (TerrainWind — поток и подветренная тень, без леса) и гребни / ложбины — высота над
	// сглаженным рельефом (TPI). Снег сдувает, где ветер быстрее ветра уровня, и наметает, где тише (за гребнем) и в
	// ложбинах; лес на открытых гребнях реже и ниже
	Range snowScourSpeed{ 1.25f, 1.7f };		// множитель скорости ветра: от — начинает сдувать, до — сдувает целиком
	Range snowDriftSpeed{ 0.9f, 0.6f };		// и наметает
	float ridgeRadius = 20.0f;				// гребни и ложбины — против рельефа, сглаженного на столько, м
	Range ridge{ 0.5f, 3.0f };				// гребень — выше сглаженного, м
	Range hollow{ -0.5f, -3.0f };			// ложбина — ниже, м
	float snowScour = 0.04f;				// снег выше на открытом (сдуло), доля высоты
	float snowDrift = 0.03f;				// и ниже в укрытии и ложбинах (надуло), доля высоты
	float windTreeline = 0.04f;				// граница леса ниже на открытых наветренных гребнях, доля высоты
	float windThinsForest = 0.5f;			// и лес там реже (и ниже — размер по маске)
	// Форма: сглаживание (тексели) и масштабы шумов (множитель длин волн)
	float slopeBlur = 3.0f;
	float moistureBlur = 6.0f;
	float erosionBlur = 2.0f;				// карты эрозии — размыв и осыпь
	float layerNoiseScale = 1.0f;			// шум скалы, снега и камней, пятна гальки
	float meadowNoiseScale = 1.0f;			// граница двух трав и цветы
	float forestNoiseScale = 1.0f;			// граница леса, поляны, древостои, кусты
};

// Поля правил слоёв (SettingsSchema): колонки строки TerrainLayerRules (у диапазона — <колонка>_from и <колонка>_to), группы
// полей — подокна окна «Layer rules». Одно описание для окна, загрузки, «Save level», проверки, сменились ли правила, и ключа
// кэша масок: новое поле правил — поле в TerrainLayerRules и строка описания (и колонка в базе)
const SettingsSchema<TerrainLayerRules>& terrainLayerRulesSchema();

// Плитка ручной правки слоя земли или маски поверх правил (TerrainPaint, строки таблицы TerrainPaint): цель — «layer N»
// или имя маски; поправки TerrainPaint::tileSize² по строкам (строка 0 — дальний край по z, как у масок), −127…127 → −1…1
struct TerrainPaintTile
{
	std::string target;
	int32_t tileX = 0;
	int32_t tileZ = 0;
	std::vector<int8_t> delta;
};

// Материал и высота террейна — колонки строки Terrain и tiling строк TerrainLayers: читает TerrainPipeline, правки окна
// террейна пишет «Save level»
struct TerrainSettings
{
	uint32_t id = 0;					// строка Terrain
	float heightMultiplier = 1.0f;
	float triplanarSharpness = 8.0f;	// резкость смены проекций triplanar на склонах
	float heightBlend = 0.2f;			// глубина смешения слоёв по высоте
	float farTextureScale = 8.0f;		// второй масштаб текстур вдали (distance resampling)
	SettingRange farBlend{ 40.0f, 120.0f };		// полоса перехода ко второму масштабу, м
	// Стохастический повтор текстур слоёв (hex-tiling, hex_tiling.sh): расстояние между центрами ячеек, повторов текстуры (0 —
	// выключен), контраст стыков ячеек (0,5 — плавно, больше — уже) и доля высоты выборки в весах ячеек
	float hexCell = 0.3f;
	float hexContrast = 0.75f;
	float hexHeightWeight = 0.6f;
	SettingRange displacement{ 12.0f, 24.0f };	// дисплейсмент: полный ближе from, м, к to — нет (to 0 — выключен)
	float microShadows = 1.0f;			// микротени прямого света по AO слоёв, 0…1
	// Мокрая земля темнее: альбедо × это при полной мокрости (вода в порах — свет уходит вглубь и поглощается, Lekner, Dorf
	// 1988; мокрый песок и земля вдвое-втрое темнее сухих), блик — по шероховатости 0,2
	float wetDarkening = 0.35f;
	float forestFloor = 1.0f;			// подстилка под кронами: доля полога, которая уходит в слой forest_floor, 0…1
	float forestFloorEdge = 1.5f;		// рваный край подстилки: на сколько шум сдвигает границу кроны, м
	float forestMoss = 0.5f;			// доля подстилки, которая уходит мху (роль forest_moss) там, где влажно, 0…1
	float forestMossDry = 0.3f;			// на сухом — эта доля от неё, 0…1
	// Сетка у камеры: самый мелкий рисуемый уровень мельче листа (номер «Finest mesh»: 0 — 25 см … 4 — 1,6 см) и радиус, в
	// котором он полон (вокруг ног при ходьбе), м
	int32_t finestMesh = 4;
	float finestMeshRadius = 1.5f;
	std::vector<float> layerTiling;		// метров на повтор по номеру слоя (TerrainLayers.layer); 0 — слоя нет
	std::vector<DirectX::XMFLOAT3> layerTint;	// множитель альбедо по номеру слоя (TerrainLayers.albedo_tint)
	std::vector<float> layerDisplacement;		// размах дисплейсмента по номеру слоя, м (TerrainLayers.displacement)
	std::vector<float> layerOcclusion;			// сила AO по номеру слоя, 0…1 (TerrainLayers.occlusion)
	std::vector<float> layerHexRotation;		// доля случайного поворота ячеек hex-tiling по номеру слоя, 0…1 (TerrainLayers.hex_rotation)
	std::optional<TerrainErosionSettings> erosion;	// строка TerrainErosion — правки подокна «Erosion»
	TerrainLayerRules layerRules;					// строка TerrainLayerRules — правки подокна «Layer rules»
	std::vector<TerrainPaintTile> paint;			// мазки кисти — строки TerrainPaint террейна
};
// Поля строки Terrain (SettingsSchema), окно террейна: материал и сетка у камеры. Не в описании — карта высот, смещение и шаг
// (TerrainDescription, в окне их нет), слои (строки TerrainLayers по номеру слоя), эрозия и правила (свои
// описания), мазки
const SettingsSchema<TerrainSettings>& terrainSchema();

// Слой материала террейна — строка TerrainLayers (номер слоя — место в TerrainDescription::layers): файлы альбедо с высотой и
// нормали, метров на повтор, множитель альбедо, размах дисплейсмента, м, сила AO, доля поворота ячеек hex-tiling и роль в правилах слоёв (TerrainLayers.role,
// TerrainLayerRole; пусто — никакой). Пустые файлы — слой не описан, вместо текстур заглушки
struct TerrainLayerDescription
{
	std::string name;
	std::string albedo;
	std::string normal;
	float tiling = 1.0f;
	DirectX::XMFLOAT3 tint = DirectX::XMFLOAT3( 1.0f, 1.0f, 1.0f );
	float displacement = 0.0f;
	float occlusion = 1.0f;
	float hexRotation = 1.0f;
	std::string role;
};

// Строка Terrain уровня и его слои — читает LibraryLoader::loadLevel, конвейер рельефа (TerrainPipeline) и материал
// (TerrainMaterial) о базе не знают
struct TerrainDescription
{
	TerrainSettings settings;	// поля окна террейна (terrainSchema), id — строка Terrain
	std::string heightMap;		// карта высот — имя текстуры (Terrain.heightmap → Textures.name)
	// Её файл (Textures.file): кэш и карты эрозии, копия слоёв и масок — рядом, в каталогах eroded и generated
	std::string heightMapFile;
	float heightOffset = 0.0f;	// Terrain.height_offset, м
	float texelSize = 1.0f;		// Terrain.width_multiplier: м на тексель карты высот
	// Слои по номеру (TerrainLayers.layer) — до наибольшего описанного; пропуск в номерах — слой без файлов
	std::vector<TerrainLayerDescription> layers;
};

// Строка FluvialErosion (FarTerrain.fluvial_erosion): речная эрозия гор вокруг карты до ледниковой (FluvialErosion,
// закон stream power схемой FastScape) — сеть долин, по которой потом идут ледники
struct FluvialErosionSettings
{
	// Значения по умолчанию — подбор 2026-10-17 с Dimok: сетка 128 м (на 64 м массивы «тощие»), рельеф заново из подъёма
	uint32_t id = 0;					// строка FluvialErosion
	int32_t resolution = 512;			// клеток сетки по стороне (512, 1024, 2048): у гор 64 км — 128, 64, 32 м
	float erodibility = 6.0e-6f;		// K закона stream power, 1/год (при m = 0,5)
	float areaExponent = 0.55f;			// m: врез ∝ водосбор^m · уклон
	float years = 2.0e6f;				// лет
	int32_t steps = 100;					// шагов (схема неявная — устойчива при любом)
	float hillslopeDiffusion = 0.05f;	// склоны сглаживаются, м²/год
	float routingNoise = 10.0f;			// шум выбора стока, ± м: без него на ровном склоне борозды вдоль осей сетки
	// Рельеф заново из подъёма (Cordonnier и др. 2016): огибающая — карта, размытая на upliftScale, м (0 — нет: реки режут
	// готовый рельеф); подъём — огибающая / upliftTime, м/год
	float upliftScale = 3000.0f;
	float upliftTime = 650000.0f;
	float upliftFloor = 150.0f;			// низины ниже (сглаженные) не поднимаются — котловина, долины, море, м
	SettingRange mapFade{ 3000.0f, 8000.0f };	// у карты уровня эрозия гаснет: полная — дальше to от её края, м
};
const SettingsSchema<FluvialErosionSettings>& fluvialErosionSchema();

// Строка GlacialErosion (FarTerrain.glacial_erosion): ледниковая эрозия гор вокруг карты (GlacialErosion,
// docs/far_terrain.md, «Ледниковая эрозия») — лёд ледникового периода на ложе гор: где копится и как течёт, сколько стирает
struct GlacialErosionSettings
{
	// Значения по умолчанию — подбор 2026-10-17 с Dimok: ледники долинами (~5 % кольца), а не шапкой; зубцы и над долиной
	uint32_t id = 0;					// строка GlacialErosion
	int32_t resolution = 512;			// клеток сетки льда по стороне (256, 512, 1024): у гор 64 км — 256, 128, 64 м
	float ela = 2200.0f;				// линия равновесия, м: выше снег копится, ниже лёд тает
	float balanceGradient = 10.0f;		// баланс массы, м/год на км высоты над линией равновесия
	float maxAccumulation = 1.0f;		// наибольшее накопление, м/год
	float flow = 1.0f;					// множитель течения льда (закон Глена, тёплый лёд)
	float sliding = 1.0f;				// множитель скольжения по ложу
	float iceYears = 400.0f;			// лет набора льда до эрозии
	float erosionYears = 800.0f;		// лет эрозии (ускоренной: размыв за шаг — как за тысячи лет)
	float erosion = 0.001f;				// размыв ложа, м на м пути скольжения
	float maxErosion = 800.0f;			// ложе ниже исходного не больше, м
	SettingRange mapFade{ 3000.0f, 8000.0f };	// у карты уровня эрозия гаснет: полная — дальше to от её края, м
	float polish = 0.7f;				// мелкий рельеф под льдом сглаживается, 0…1
	// Морозное выветривание: острые голые зубцы по выпуклым гребням выше линии льда (как на ориентире — много мелких пиков по
	// кромке хребта), форму массива не меняют
	float frostPeaks = 220.0f;			// высота зубцов, м; 0 — нет
	float frostPeakSpacing = 180.0f;	// их шаг вдоль гребня, м
	SettingRange frostPeakHeight{ 1200.0f, 2000.0f };	// зубцы набирают высоту от from до to над морем, м
	// У карты зубцы гаснут только у самого стыка (форму массива они не меняют): полные — дальше to от её края, м
	SettingRange frostPeakFade{ 500.0f, 1500.0f };
	// Ледники наших дней: тот же лёд без эрозии по готовому рельефу (iceYears лет) — кары и долинные ледники под вершинами;
	// выше вершин — ледников нет. 2000 м — выбор Dimok 2026-10-17 по сетке снимков (лёд заполняет долины южного хребта, ~4 %
	// гор): ниже ela ледникового периода — его лёд за короткий прогон не доходит до настоящего размаха
	float presentEla = 2000.0f;			// линия равновесия наших дней, м; она же линия фирна на леднике
};
const SettingsSchema<GlacialErosionSettings>& glacialErosionSchema();

// Значения по умолчанию правил — вариант C сетки снимков против картинки-ориентира (выбор Dimok 2026-10-17: снега больше,
// лес ниже). Строка FarTerrain (Levels.far_terrain): дальние горы вокруг карты уровня — своя грубая карта высот (Tools/gen_far_terrain.py)
// на той же сетке узлов CDLOD, что карта; у карты высота переходит в её высоту, материал — правила по высоте и уклону на
// слоях террейна по ролям (TerrainLayers.role). Карта высот, её место и масштаб — только в базе; окно — дальность, полосы
// стыка и правила
struct FarTerrainSettings
{
	uint32_t id = 0;					// строка FarTerrain
	std::string heightMap;				// имя текстуры (Textures.name): R16 от 0 до 1, строки сверху вниз — с севера
	std::string heightMapFile;			// её файл (Textures.file) — читает загрузчик
	DirectX::XMFLOAT2 origin = DirectX::XMFLOAT2( -32768.0f, -32768.0f );	// угол с наименьшими x, z, м: кратен узлу карты
	float extent = 65536.0f;			// сторона, м
	float heightMultiplier = 1.0f;		// метры = значение · множитель + сдвиг
	float heightOffset = 0.0f;
	float lodDistance = 14000.0f;		// дальность уровня, чей квад — тексель карты гор; дальше — вдвое на уровень, м
	float blendDistance = 256.0f;		// от края карты: высота карты → высота гор, м
	float materialBlend = 500.0f;		// от края карты: слои карты (splat) → правила, м
	SettingRange rockSlope{ 35.0f, 50.0f };		// скала по уклону, градусы
	SettingRange screeSlope{ 25.0f, 38.0f };	// осыпи по уклону…
	SettingRange screeHeight{ 900.0f, 1300.0f };	// …выше этой высоты, м
	SettingRange meadowHeight{ 200.0f, 600.0f };	// трава долины → сухой луг, м
	SettingRange snowHeight{ 1400.0f, 1700.0f };	// снег по высоте (на склоне к югу), м
	float snowNorth = 500.0f;			// на склоне к северу снег ниже на столько, м
	float snowNoise = 250.0f;			// граница снега гуляет пятнами на столько, м
	SettingRange snowSlope{ 45.0f, 60.0f };		// круче — снег не держится, градусы
	SettingRange forestLine{ 1400.0f, 1700.0f };	// лес (тон земли) — ниже, к верху полосы сходит на нет, м
	SettingRange forestSlope{ 28.0f, 38.0f };	// круче — леса нет, градусы
	DirectX::XMFLOAT3 forestColor = DirectX::XMFLOAT3( 0.03f, 0.05f, 0.03f );	// цвет леса издали, линейный
	// Лес по рельефу (docs/far_terrain.md, «Лес на горах»): гуще в долинах (водосбор от from до to, км²: дно долины и нижние
	// склоны) и на пологом, выше по склонам и на гребнях — доля forestUpland; поляны — доля forestGlades; ближе forestGlacierGap
	// метров к ледникам — нет (морены, свежая земля из-под льда)
	SettingRange forestValleyArea{ 0.05f, 1.0f };
	float forestUpland = 0.6f;
	float forestGlades = 0.2f;
	float forestGlacierGap = 300.0f;
	// Лавинные лотки (FarForestRelief, AvalancheRules): отрыв на склонах круче avalancheSlope, градусы, выше avalancheHeight, м;
	// лавина идёт вниз до склона положе avalancheRunout; где сошло с площади отрыва больше avalancheArea га — леса нет
	SettingRange avalancheSlope{ 30.0f, 40.0f };
	float avalancheHeight = 900.0f;
	float avalancheRunout = 15.0f;
	SettingRange avalancheArea{ 3.0f, 12.0f };
	// Ледники наших дней (строка GlacialErosion, presentEla): лёд толще from…to — слой снега на любом уклоне; ниже линии фирна
	// и там, где снег не держится, — голый лёд: альбедо слоя снега × оттенок льда
	SettingRange glacierCover{ 5.0f, 30.0f };	// толщина льда, м
	DirectX::XMFLOAT3 glacierIceTint = DirectX::XMFLOAT3( 0.55f, 0.72f, 0.9f );
	// Трещины и морены — вариант A сетки снимков (выбор Dimok 2026-10-17: средние, морены полосами по белому льду).
	// Трещины поперёк течения льда (вниз по уклону его поверхности) там, где ледник круче: ледопады
	SettingRange crevasseSlope{ 8.0f, 20.0f };	// уклон поверхности льда, градусы
	float crevasseSpacing = 40.0f;				// шаг трещин вдоль течения, м
	DirectX::XMFLOAT3 crevasseColor = DirectX::XMFLOAT3( 0.05f, 0.1f, 0.16f );	// альбедо в трещине (тень во льду), линейное
	// Морены: обломки со скальных стен над ледником лёд несёт по линиям течения — боковые вдоль краёв, срединные от слияния
	// потоков; видны ниже линии фирна слоем осыпей
	float moraineWidth = 60.0f;					// ширина морены, м (пересчёт карты морен)
	SettingRange moraineCover{ 0.3f, 0.6f };	// доля линий обломков: от from — морена набирает силу до to
	SettingRange macroDistance{ 800.0f, 2500.0f };	// дальше текстуры слоёв — в третьем масштабе (повтор не виден сеткой), м
	float macroTextureScale = 8.0f;		// во столько раз крупнее дальнего масштаба
	int32_t shadowResolution = 512;		// текселей тени солнца на всё кольцо по стороне (TerrainSunShadow)
	std::optional<FluvialErosionSettings> fluvial;	// строка FluvialErosion — речная эрозия; NULL — нет
	std::optional<GlacialErosionSettings> glacier;	// строка GlacialErosion — ледниковая эрозия; NULL — нет
};
const SettingsSchema<FarTerrainSettings>& farTerrainSchema();

// Строка SkyAtmosphere; без неё — значения по умолчанию
struct SkyAtmosphereSettings
{
	float skyIntensity = 1.0f;	// множитель рассеянного света неба, 1 — по модели
	float haze = 1.0f;			// плотность дымки (аэрозоли Ми)
	float groundAlbedo = 0.25f;	// отражение земли под горизонтом
	// Во сколько раз воздух между камерой и точкой кажется толще (Aerial Perspective View Distance Scale в UE):
	// 1 — по модели (дымка на 1 км — несколько процентов), больше — небольшой мир выглядит как большой, 0 — выключено
	float aerialPerspectiveViewDistanceScale = 1.0f;
	// До какого расстояния от камеры воздух растянут в aerialPerspectiveViewDistanceScale раз, м; дальше — настоящий путь:
	// долина в километр — в дымке, а горы за картой в десятках километров в ней не тонут. 0 — растянут весь путь
	float aerialPerspectiveScaledDistance = 0.0f;
	// Свечение ночного неба в зените, кд/м²: собственное свечение атмосферы (airglow), суммарный свет звёзд и
	// зодиакальный свет — ~2·10⁻⁴ на тёмном небе; к горизонту ярче. Видно, когда солнце и луна не светят
	float nightSkyLuminance = 2e-4f;
	// Млечный Путь в фоне ночью: карта яркости по RA/Dec J2000 (файл от Textures\, равнопромежуточная, RA растёт влево —
	// NASA SVS «Deep Star Maps 2020», фон без звёзд Hipparcos/Tycho); пусто — его нет. Яркость — средняя по всей сфере,
	// кд/м²: свет звёзд слабее каталога Stars. У полюсов Галактики весь свет звёзд — ~28 S10 (Asaad et al. 1979), из них
	// каталог — ~10, а карта там — 0,17 своего среднего: среднее ≈ 100 S10 ≈ 8·10⁻⁵ кд/м² (1 S10 — 8,35·10⁻⁷ кд/м²)
	std::string milkyWay;
	float milkyWayLuminance = 8e-5f;
};

// Звезда фона неба — строка таблицы Stars (Yale Bright Star Catalogue, Tools/star_catalog.py): положение на эпоху J2000
// и блеск. Каталог общий для всех уровней с атмосферой
struct StarRecord
{
	float rightAscension = 0.0f;	// прямое восхождение, градусы
	float declination = 0.0f;		// склонение, градусы
	float magnitude = 0.0f;			// видимая звёздная величина V
	float colorIndex = 0.65f;		// показатель цвета B − V (0,65 — как у Солнца)
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
// Экранное затенение окружением (GTAO, AmbientOcclusion) — колонки ambient_occlusion_* строки PostProcessSettings, как
// Ambient Occlusion в Post Process Volume UE; NULL — значения по умолчанию
struct AmbientOcclusionSettings
{
	float intensity = 1.0f;	// 0 — без затенения (и без расчёта)
	float radius = 1.0f;	// м: на каком расстоянии от точки поверхность её заслоняет
	float power = 1.5f;		// видимость^power: сильнее в щелях, слабее на открытом
};

// Цветокоррекция — колонки строки PostProcessSettings, как Color Grading в Post Process Volume UE (ColorGradingLut): часть
// кадра — тени, полутона, света или весь кадр; значения — каналы R, G, B и общий множитель (w), как Vector4 у UE
struct ColorGradingPart
{
	DirectX::XMFLOAT4 saturation = DirectX::XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );	// 0 — серый, больше — насыщеннее
	DirectX::XMFLOAT4 contrast = DirectX::XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );	// вокруг среднего серого 0,18
	DirectX::XMFLOAT4 gamma = DirectX::XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );		// больше — светлее полутона
	DirectX::XMFLOAT4 gain = DirectX::XMFLOAT4( 1.0f, 1.0f, 1.0f, 1.0f );		// множитель
	DirectX::XMFLOAT4 offset = DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );		// сдвиг (каналы + w)
};

struct ColorGradingSettings
{
	// Баланс белого, как у камеры (White Balance в UE): цветовая температура света, который выглядит белым; выше — кадр
	// теплее, 6500 К — как есть; оттенок поперёк температуры: + пурпурнее, − зеленее
	float whiteTemp = 6500.0f;
	float whiteTint = 0.0f;
	ColorGradingPart global;		// весь кадр: множитель частей (сдвиг — добавка)
	ColorGradingPart shadows;
	ColorGradingPart midtones;
	ColorGradingPart highlights;
	// Границы частей по яркости цвета после экспозиции (средний серый — 0,18): доля теней — 1 − smoothstep( 0, shadowsMax, Y ),
	// светов — smoothstep( highlightsMin, highlightsMax, Y ), полутона — остаток
	float shadowsMax = 0.09f;
	float highlightsMin = 0.5f;
	float highlightsMax = 1.0f;
};

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
	// Лучи-звёзды вокруг ярких бликов (дифракция на лепестках диафрагмы, Kawase 2003): яркость луча у блика — доля его
	// избытка над порогом (0 — лучей нет), порог — яркость после экспозиции (1 — белое; искры солнца на воде — сотни),
	// предел избытка (мягкий: солнце в тысячи раз ярче искры), число лучей, поворот звезды, длина луча — доля высоты кадра
	float starIntensity = 0.05f;
	float starOpacity = 1.0f;		// видимость звёзд: размер тот же, звезда смешивается с кадром после тонмаппинга
	float starThreshold = 500.0f;
	float starLimit = 512.0f;
	int32_t starRays = 6;
	float starAngle = 15.0f;
	float starLength = 0.06f;
	float starDispersion = 2.0f;		// радужная кайма лучей: разброс длины по каналам (0 — белые, 1 — по длинам волн)
	// Лучи солнца — god rays (как Light Shafts у направленного света в UE, экранные, Mitchell 2007): сила (0 — нет), порог —
	// яркость неба после экспозиции, предел избытка (мягкий: диск солнца), радиус маски у солнца и длина следа — доли высоты
	// кадра и пути к солнцу, оттенок. Не физика: в ясную сухую погоду лучей в воздухе не видно (их даёт туман уровня), но
	// кадр с ними красивее
	float lightShaftIntensity = 2.5f;
	float lightShaftThreshold = 0.3f;
	float lightShaftLimit = 4.0f;
	float lightShaftRadius = 0.6f;
	float lightShaftLength = 0.9f;
	DirectX::XMFLOAT3 lightShaftTint = DirectX::XMFLOAT3( 1.0f, 0.92f, 0.8f );
	// Лучи — свет в воздухе перед тем, что за пикселем: доля 1 − e^(−глубина / это), м; перед ближним (ветка у камеры)
	// дымки нет; 0 — по всей глубине одинаково
	float lightShaftDistance = 8.0f;
	float lightShaftDecay = 0.5f;		// вес света в конце следа (у солнца) — спад вдоль луча, Decay у Mitchell; 1 — без спада
	// Круговое затухание лучей вокруг солнца: радиус — доля высоты кадра (0 — без него, лучи до края кадра), ширина полосы
	// затухания — доля радиуса
	float lightShaftFadeRadius = 0.8f;
	float lightShaftFadeWidth = 0.7f;
	AmbientOcclusionSettings ambientOcclusion;
	ColorGradingSettings colorGrading;
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
// Поле ветра по рельефу (TerrainWind, docs/wind.md, «Поле ветра по рельефу») — колонки field_* строки Wind, подокно
// «Field» окна «Wind». Ветер уровня — над ровным открытым местом; рельеф, лес и постройки меняют его в каждой точке
struct WindFieldSettings
{
	// Рельеф — массово-согласованный поток в слое воздуха над рельефом, сглаженным на layerSmoothing: толщина слоя над
	// сглаженным — layerHeight; где рельеф выше сглаженного (гребень, скала у прохода), слой тоньше — ветер быстрее
	float layerHeight = 100.0f;			// м
	float layerSmoothing = 300.0f;		// м
	float maxSpeedup = 2.0f;			// множитель скорости — не больше
	// Подветренная тень: угол горизонта навстречу ветру Sx (как у снега в правилах слоёв) на reach — от shelterFrom до
	// shelterTo градусов скорость падает до leeSlowdown, а порывистость растёт. На пологом подветренном склоне поток
	// прилегает к рельефу, отрывается — за перегибами круче ~15–20°
	float reach = 100.0f;				// м: ближняя зона за гребнем; дальше поток снова прилегает к рельефу
	float shelterFrom = 15.0f;			// градусы
	float shelterTo = 30.0f;
	float leeSlowdown = 0.5f;
	// Лес и постройки (полог): в полном лесу скорость × ( 1 − forestDamping ); к этому — за forestLength по ветру, обратно к
	// скорости рельефа над полем и водой — за recoveryLength
	float forestDamping = 0.7f;
	float forestLength = 15.0f;			// м
	float recoveryLength = 60.0f;		// м
	float turbulence = 1.0f;			// порывистость за гребнями и на опушках, 0…1
};

// Поле настроек поля ветра: колонка строки Wind, свойство подокна «Field», границы, единица, подсказка — одна таблица
// для окна, загрузки, «Save level» и проверки, сменились ли настройки

struct WindSettings
{
	DirectX::XMFLOAT3 direction = DirectX::XMFLOAT3( 0.0f, 0.0f, 1.0f );	// куда дует, горизонтально (y не учитывается)
	float strength = 0.0f;			// сила изгиба: сдвиг верха растения высотой h — strength · WindWeight · порыв · h²
	float speed = 4.0f;				// скорость волн порывов, м/с
	float minGustAmount = 0.3f;		// порыв между волнами и на гребне волны — доли силы (Min / Max Gust Amount в UE)
	float maxGustAmount = 1.0f;
	float gustSize = 25.0f;			// длина волны порыва, м
	WindFieldSettings field;
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
// Поля строки VolumetricCloud: колонки, окно «Volumetric cloud» (SettingsSchema)
const SettingsSchema<VolumetricCloudSettings>& volumetricCloudSchema();

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
	// Туман над фоном неба — как над точкой на этом расстоянии, м (было — дальняя плоскость, 4 км: с ней в 100 км горизонт
	// тонул бы в тумане)
	float skyDistance = 4000.0f;
};

// Строка WaterSources: источник воды, поставленный руками (родник, ледниковое озеро), — гауссово пятно притока
struct WaterSource
{
	std::string name;
	DirectX::XMFLOAT2 position = DirectX::XMFLOAT2( 0.0f, 0.0f );	// x, z мира
	float rate = 1.0f;				// расход, л/с
	float radius = 3.0f;			// радиус пятна, м (≈ 2σ гаусса)
};

// Точка кривой русла: x, z мира; расход — м³/с, 0 — по водосбору под точкой (ручная кривая)
struct StreamCurvePoint
{
	DirectX::XMFLOAT2 position = DirectX::XMFLOAT2( 0.0f, 0.0f );
	float discharge = 0.0f;
};

// Кривая русла — данные, а не побочный результат (строки Streams и StreamPoints, docs/water.md, «Кривые русел»): ось ручья
// от истока вниз, как Landscape Splines в UE. Генератор конвейера (TerrainHydrology::generate) предлагает кривые по стоку
// и пишет их в базу; правленная (edited) и ручная (generated = false) при новой генерации остаются, сгенерированные в их
// полосе обрезаются. Множители — к общим параметрам WaterChannels (1 — как у всех)
struct StreamCurve
{
	uint32_t id = 0;				// строка Streams; 0 — ещё не в базе
	std::string name;
	bool generated = true;
	bool edited = false;
	bool enabled = true;
	float widthScale = 1.0f;		// ширина дна
	float depthScale = 1.0f;		// врез
	float freeboardScale = 1.0f;	// запас бровки над водой
	float bankSlopeScale = 1.0f;	// пологость бортов
	float thalwegScale = 1.0f;		// тальвег
	float roughnessScale = 1.0f;	// шероховатость дна (Маннинг): глубина воды
	float dischargeScale = 1.0f;	// расход
	float stoneScale = 1.0f;		// камней в русле (и валунов)
	float outletWidthScale = 1.0f;	// ширина истока из озера (у скального озера — меньше: русло уже и глубже)
	float outletLengthScale = 1.0f;	// длина перехода истока к руслу ручья
	std::vector<StreamCurvePoint> points;
	bool pointsChanged = false;		// точки правили в редакторе — «Save level» перепишет StreamPoints (в базе колонки нет)
};
// Поля строки Streams (SettingsSchema): включение и множители — подокно кривой в «Streams»
const SettingsSchema<StreamCurve>& streamCurveSchema();

// Русла и ручьи конвейера рельефа и воды (TerrainHydrology) — строка WaterChannels той же строки WaterSimulation;
// без строки — значения по умолчанию. Применяются при загрузке уровня
struct WaterChannelsSettings
{
	float minDischarge = 0.002f;	// м³/с: отсюда начинается русло
	float widthCoef = 4.0f;			// ширина дна w = a · Q^0,5, м
	float minWidth = 0.5f;			// самое узкое дно, м (у русел — детальная земля 0,25 м)
	float depthCoef = 1.0f;			// врез d = c · Q^0,4, м
	float minIncision = 0.4f;		// самый мелкий врез, м
	float bankSlope = 1.0f;			// борт: метров по горизонтали на метр вреза
	int32_t smooth = 6;				// проходов сглаживания оси вдоль течения
	float meanderLength = 35.0f;	// длина волны извилин вдоль ручья, м
	float meanderAmplitude = 2.5f;	// сдвиг поперёк течения при Q ≥ 0,1 м³/с, м
	float meanderMaxSlope = 0.06f;	// круче (м на м) — без извилин
	float minSlope = 0.002f;		// дно вниз по течению — не меньше, м на м
	float lakeDepth = 0.05f;		// низина глубже — озеро: не режется, ручей в нём кончается, м
	float minLakeArea = 50.0f;		// низина меньше — не озеро, а часть русла (ямка на дне ручья), м²
	float thalweg = 0.3f;			// тальвег: добавка вреза к середине дна, доля вреза
	float manning = 0.06f;			// шероховатость дна ручья по Маннингу, с/м^(1/3) (галька)
	float minWaterSlope = 0.005f;	// уклон в глубине воды — не меньше, м на м
	float minSpeed = 0.1f;			// течение ручья — не медленнее (подпор у озера, ровные места), м/с
	float minWaterDepth = 0.3f;		// вода над дном — не мельче, м
	float freeboard = 0.4f;			// вода ниже бровки (русло прорезано под паводок), м
	float sourceTaper = 20.0f;		// от родника наименьшие ширина, врез, вода и запас нарастают на столько метров
	// Профиль русла (TerrainHydrology, docs/water.md, «Профиль русла»)
	float bedShape = 2.0f;			// форма дна: 2 — V (тальвег острый), больше — плоский U
	float bankRoundness = 1.0f;		// борт: 1 — скруглённые бровка и подошва, 0 — прямой откос с резкой бровкой
	float bendAsymmetry = 0.7f;		// на изгибах: внешний берег глубже и круче, внутренний — пологая отмель (0 — симметрично)
	float poolDepth = 0.3f;			// плёсы глубже на долю вреза (0 — без плёсов)
	float poolSpacing = 6.0f;		// плёс и перекат — через столько ширин русла
	float bedNoise = 0.08f;			// неровность дна и бортов, м
	float bedNoiseScale = 1.5f;		// размер неровностей, м
	// Ступени и плёсы на крутом (step-pool и каскад, Montgomery, Buffington 1997): уступ высотой stepHeight, шаг — чтобы
	// крутизна ступеней H / L была stepSteepness уклона (Abrahams, Atkinson 1995: 1–1,5), но 0,5…4 ширины дна; положе
	// stepMinSlope ступеней нет (там плёсы и перекаты)
	float stepHeight = 0.4f;		// м; 0 — без ступеней
	float stepSteepness = 1.25f;
	float stepMinSlope = 0.03f;		// м на м
	// Чаша озера: отмель, затем свал к глубине depthRatio · √площади в пределах min…max
	float lakeDepthRatio = 0.05f;	// наибольшая глубина на √площади озера
	float lakeMinDepth = 1.0f;		// м
	float lakeMaxDepth = 8.0f;		// м
	float lakeShelfWidth = 3.0f;	// отмель у берега (литораль), м
	float lakeShelfDepth = 0.5f;	// глубина у края отмели, м
	float lakeDropSlope = 0.4f;		// свал за отмелью, м на м
	// Запас берега озера: вода стоит ниже перелива низины на столько, м. Рельеф (берег, чаша) — по переливу, поэтому урез
	// уходит внутрь по отмели, а земля у озера выше воды, как у настоящего озера; исток прорезает порог на этот запас
	float lakeFreeboard = 0.12f;
	// Исток из озера (docs/water.md, «Исток из озера»): у озера русло шире в outletWidth раз ширины по расходу (w = a · Q^0,5;
	// меньше 1 — уже и глубже, как в скальных породах), вода на пороге — критической глубины, бровка — на запас берега выше
	// воды; к outletLength метрам от озера ширина, вода и запас бровки — как у ручья
	float outletWidth = 1.5f;
	float outletLength = 30.0f;		// м
	// Камни русла (TerrainHydrology, docs/water.md, «Камни в русле»): раскладывает конвейер, ставит слой расстановки
	// с placement stream_stones, течение тормозится о них
	float stoneDensity = 1.5f;		// камней на метр ручья (дно и борта); на перекатах и на крутом — гуще
	float stoneSize = 0.4f;			// типичный поперечник камня, м
	float bankStones = 0.35f;		// доля камней у бортов, у уреза
	float boulderDensity = 0.05f;	// валунов на метр ручья (на крутом — больше)
	float boulderSize = 1.0f;		// поперечник валуна, м
};
// Поля строки WaterChannels (SettingsSchema), подокно «Channels». Параметры генератора кривых русел — группа
// streamGeneratorGroup: их правка генерирует кривые заново при следующей загрузке (они же — отпечаток StreamGeneration,
// TerrainHydrology::generationKey), правка остальных перестраивает русла сразу
constexpr const char* streamGeneratorGroup = "generator";
const SettingsSchema<WaterChannelsSettings>& waterChannelsSchema();

// Строка WaterSimulation: вода на сетке карты высот (WaterSimulation, docs/water.md). Приток — по водосбору, который
// считает конвейер рельефа и воды (TerrainHydrology) по итоговому рельефу: нарастает от flowStart до flowFull м²
struct WaterSimulationSettings
{
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
	float rippleScale = 2.5f;		// метров на повтор ряби
	float rippleStrength = 1.0f;	// рябь быстрой воды
	float calmRipple = 0.25f;		// рябь стоячей (ветер)
	float refraction = 0.02f;		// сдвиг преломления, доля высоты экрана на метр толщины
	float rippleStretch = 2.5f;		// рябь быстрой воды вытянута вдоль течения во столько раз
	float standingWaves = 1.0f;		// сила стоячих волн над перекатами (0 — нет)
	// Искры: светило в гранях ряби мельче пикселя — доля блика в искрах (0 — ровный блик), шероховатость граней (ширина
	// россыпи вокруг отражения светила) и размер грани, м
	float sparkle = 1.0f;
	float sparkleRoughness = 0.3f;
	float sparkleSize = 0.005f;
	float foamSpeed = 1.5f;			// пена — быстрее этого, м/с
	float foamShear = 1.5f;			// и где сдвиг скорости больше, 1/с
	float foamLifetime = 3.0f;		// пена, которую несёт течение, тает за это время, с (WaterFoam) — на быстрой воде
	float foamStillLifetime = 20.0f;	// и в стоячей (плёсы, заводи, озёра), с
	// Пена падения со ступени (Shaders/water_foam.sh, traceFall): стенка — поверхность вдоль течения круче foamFallSlope (м на
	// м); ближе foamFallStart к кромке (по высоте, м) вода чистая, ниже пена растёт к подошве, у подошвы и в котле под ней —
	// foamFall (падение ниже foamFallHeight, м, — меньше), в котле гаснет к foamPlungeLength от подошвы, м
	float foamFallSlope = 0.5f;
	float foamFallStart = 0.1f;
	float foamFall = 2.0f;
	float foamFallHeight = 0.3f;
	float foamPlungeLength = 0.3f;
	// Узор пены — кружево (WaterFoamLace): метров на повтор поперёк течения (вдоль — длиннее, как рябь), ширина края по маске
	// — тонкие нити полупрозрачны — и растворение: где доля пены сходит на нет, порог сдвинут шумом узора, и пена тает кусками
	// на разных расстояниях (0 — по линии)
	float foamTextureRepeat = 0.5f;
	float foamSoftness = 0.35f;
	float foamDissolve = 0.5f;
	// Густая пена — белая вода сливов и котлов под ступенями: с этой доли пены кружево переходит в неё (полностью — при 1);
	// 1 — только кружево
	float foamDense = 0.5f;
	// Плавучая пена: в медленной воде (плёс, котёл, заводь) пена — то, что принесло течение, — кружевом и пятнами, а не
	// белой водой: покрывает не больше этой доли поверхности и без густой пены; на быстрой воде (от foam_speed) — полностью
	float foamFloatCover = 0.45f;
	// Стоячие волны над перекатами: доля длинных гребней через всё русло (по фазе вдоль оси ручья; в окне у камеры — и
	// геометрия), остальное — короткие гребни на плитках ряби
	float standingCrest = 0.7f;
	// Ближе — поверхность воды у русел рисует детальная плитка (0,25 м, режим static), м; 0 — только сетка 1 м
	float detailDistance = 150.0f;

	// Течение статичной воды (Shaders/water_flow.cs): установившийся расчёт при загрузке — струя ручья в озере, тяга к
	// истоку; уровень не меняется
	struct Flow
	{
		float settleTime = 600.0f;	// время расчёта до установившегося течения, с
		float timeStep = 0.5f;		// шаг, с
		float viscosity = 0.05f;	// вихревая вязкость, м²/с: шире и короче струя
		float manning = 0.03f;		// шероховатость дна озёр по Маннингу, с/м^(1/3)
		int32_t iterations = 20;	// итераций давления за шаг
		// Толщина текущего слоя, м: ручей растекается по озеру у поверхности, а не всей толщей — глубже течение не
		// считается (средняя по всей глубине скорость озера в разы меньше видимой)
		float layer = 0.3f;
		// Течение на детальной сетке у русел (0,25 м, вложено в решение 1 м — оно начало и граница): время расчёта, с; 0 —
		// течение детальной воды — решение 1 м
		float detailSettleTime = 30.0f;
	} flow;

	// Подробное течение у камеры (NearWaterFlow, docs/water.md): окно мелкой сетки вокруг камеры, каждый кадр, вложено в
	// течение детальной сетки 0,25 м — обтекание камней (камень выше воды — препятствие), повороты и завихрения
	struct NearFlow
	{
		bool enabled = true;
		// Мелкая вода (water_near_shallow.cs): высота воды в окне переменна — волны, бугор перед камнем, прыжок под ступенью;
		// нет — течение под «жёсткой крышкой» с поверхностью по давлению (water_flow.cs с FLOW_NEAR). Переключатель — на время
		// сравнения (docs/water.md, «Мелкая вода у камеры»)
		bool shallow = false;
		// Мелкая вода: камень выше половины глубины — проницаем, сопротивление его части столба воды, 1/м (Форхгеймер:
		// u /= 1 + dt · это · доля столба в камне · |u|): больше — вода обходит камень, меньше — сочится сквозь ряд камней
		float stoneDrag = 8.0f;
		// Мелкая вода: шероховатость дна ручья по Маннингу в окне, с/м^(1/3). n русел (0,06) — и галька, и сопротивление
		// формы камней и уступов; в окне камни и уступы — само дно (дисплейсмент, модели), и с тем же n сопротивление
		// считалось дважды: узкий крутой ручей шёл медленнее, чем по Маннингу (Dimok, 2026-10-21). Здесь — только зерно
		float streamRoughness = 0.035f;
		float size = 10.0f;			// сторона окна, м
		float cell = 0.0625f;		// сторона ячейки, м
		int32_t steps = 1;			// шагов за кадр (время шага — время игры кадра / шагов)
		int32_t iterations = 10;	// итераций давления за шаг
		float viscosity = 0.005f;	// вихревая вязкость, м²/с: меньше — мельче вихри
		// Камень выше воды: проводимость сквозь него — эта доля глубины без камня (вода обходит его, а ряд камней без
		// просвета сочится, не запружая ручей)
		float passage = 0.05f;
		float eddies = 1.0f;		// удержание завихрённости, 1/с: вихри за камнями и на поворотах держатся дольше
		// Поверхность в окне по давлению течения (вспучивание перед камнем, просадка за ним) — × это; 0 — ровная
		float surface = 1.0f;
		// Доля своей пены окна в материале воды (рождается у камней и на сдвиге, её несёт течение окна); 0 — пена поля 0,25 м
		float foam = 1.0f;
		// Узор пены в окне — по его течению (координаты узора несёт течение, слой обновляется раз в patternPeriod, с): доля;
		// 0 — плитки
		float pattern = 1.0f;
		float patternPeriod = 1.5f;
		float patternStretch = 2.0f;	// слой узора, растянутый (или сжатый) во столько раз, весит вдвое меньше
		// Мокрость у воды в окне: запомненный уровень видимой воды опускается не быстрее этого, м/с — намокает сразу, сохнет
		// постепенно (волны мелкой воды не двигают мокрую линию)
		float wetDrying = 0.01f;
	} nearFlow;

	std::vector<WaterSource> sources;	// включённые строки WaterSources этой строки WaterSimulation

	// Режим static (колонка mode): вода не течёт — озёра наливаются при загрузке до перелива, ручьи — ленты, их вода для
	// травы, мокрой земли и брызг — растр; всё это строит конвейер (TerrainHydrology). Режим simulated — вода течёт по
	// рельефу (трубы, Маннинг)
	bool staticWater = false;
	WaterChannelsSettings channels;
	// Кривые русел (строки Streams) и отпечаток, по которому они сгенерированы (StreamGeneration): не совпал с нынешним
	// рельефом и параметрами — генерация заново
	std::vector<StreamCurve> streams;
	std::string streamsKey;
};
// Поля строки WaterSimulation (SettingsSchema): части окна "" — приток и испарение, "surface" — подокно «Surface»; шаг и
// прогрев — только в базе. Течение — колонки flow_*, подокно «Flow» (и ключ кэша течения). Не в описании — режим (mode),
// источники, русла и кривые (свои строки)
const SettingsSchema<WaterSimulationSettings>& waterSimulationSchema();
const SettingsSchema<WaterSimulationSettings::Flow>& waterFlowSchema();
// Поля подокна «Near flow» (колонки near_* строки WaterSimulation): подробное течение у камеры, правка — сразу
const SettingsSchema<WaterSimulationSettings::NearFlow>& waterNearFlowSchema();

// Эмиттер частиц (строки ParticleEmitters и экземпляр LevelParticleEmitters, ParticleSystem, docs/particles.md)
struct ParticleEmitterSettings
{
	// Где рождаются: в точке экземпляра, в шаре вокруг неё, на поле вокруг камеры (по маске плотности — пыльца над
	// лугом, хвоя под ельником), на быстрой воде симуляции (брызги на перекатах) или в окне подробного течения у камеры по
	// пене падения — у подошвы ступени и в котле (брызги бурления)
	enum class Spawn { point, sphere, camera, water, fall };
	// Как выглядят: мягкое пятно к камере, узкая карточка, вытянутая вдоль скорости (хвоинка), или клочок взбитой воды —
	// пятно с рваным краем и пузырями внутри (брызги бурления)
	enum class Shape { dot, needle, froth };

	uint32_t id = 0;				// строка ParticleEmitters — куда пишет «Save level»
	uint32_t instanceId = 0;		// строка LevelParticleEmitters (экземпляр уровня) — туда «Save level» пишет position
	std::string name;
	Spawn spawn = Spawn::point;
	Shape shape = Shape::dot;
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// экземпляр, м мира (point, sphere)
	float radius = 1.0f;			// шар (sphere), поле вокруг камеры (camera, water), м; дальше от камеры — гибнут
	float heightMin = 0.0f;			// над рельефом (camera), над поверхностью воды (water, fall), м
	float heightMax = 0.0f;
	std::string mask;				// текстура плотности в координатах карты высот (camera); пусто — всюду
	// Спрайт клочка (froth): покрытие, рельеф и плотность из текстуры (Tools/pack_particle_sprite.py), пусто — узор пены
	std::string sprite;
	float rate = 10.0f;				// рождений в секунду; у camera — на 100 м² поля
	uint32_t maxParticles = 4096;
	float lifetimeMin = 1.0f;		// с
	float lifetimeMax = 2.0f;
	float sizeStart = 0.05f;		// м
	float sizeEnd = 0.05f;
	// С рождения размер растёт от birthSize до sizeStart за growTime, с (0 — сразу sizeStart), дальше — к sizeEnd
	float growTime = 0.0f;
	float birthSize = 0.01f;
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
	// fall (бурление): рождается на поверхности (heightMin…heightMax над ней; ниже нуля — под водой, и тогда всплывает со
	// скоростью velocity.y) на высоте sprayHeightMin…sprayHeightMax над уровнем всплытия и тянется там с течением
	float sprayHeightMin = 0.0f;
	float sprayHeightMax = 0.01f;
	// froth со спрайтом: самозатенение массы — плотность для солнца (тень внутри массы) и доля света неба, которой нет в ядре
	float selfShadow = 1.2f;
	float coreOcclusion = 0.3f;
	// fall: всплывший клочок держится центром ниже воды на эту долю своего полуразмера (клочок — лицом к камере; 0 — центр
	// на воде, 1 — верх на воде), но не глубже половины глубины воды
	float sink = 0.7f;
	// froth: клочок сплющен по вертикали до этой доли размаха (масса бурления широкая и низкая); 1 — квад к камере как есть
	float flatten = 1.0f;
	float hold = 0.5f;				// fall: с рождения столько секунд течение клочок не несёт (стоит у подошвы), с
	bool collide = false;			// с рельефом: needle ложится, dot гибнет
	float transmission = 0.0f;		// просвет на солнце (пыльца, пух светятся против солнца)
	float emissive = 0.0f;			// свечение, кд/м² (светлячки)
};
// Поля строки ParticleEmitters (SettingsSchema), подокно эмиттера; часть "placed" — место экземпляра (у эмиттера в точке и в
// шаре; в базе — LevelParticleEmitters.position). Не в описании — имя, способ рождения и форма (имена в базе)
const SettingsSchema<ParticleEmitterSettings>& particleEmitterSchema();

// Карта отражений места — строка LevelReflectionCaptures (как Sphere Reflection Capture в UE; ReflectionCaptures,
// docs/reflection_captures.md)
struct ReflectionCaptureSettings
{
	uint32_t id = 0;				// строка LevelReflectionCaptures — куда пишет «Save level»
	std::string name;
	DirectX::XMFLOAT3 position = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// точка съёмки, м мира
	float influenceRadius = 100.0f;	// м: внутри — отражения окружения берут карту (луч по её расстояниям) вместо неба
	float transition = 0.2f;		// доля радиуса у края, где карта переходит в небо
	int32_t faceSize = 256;			// сторона грани, текселей
	float brightness = 1.0f;		// множитель карты, как Brightness у Reflection Capture в UE
};
// Поля строки LevelReflectionCaptures (SettingsSchema), подокно карты в окне «Reflection captures». Не в описании — имя
const SettingsSchema<ReflectionCaptureSettings>& reflectionCaptureSchema();

// Звуки окружения уровня — строка AmbientSound (Levels.ambient_sound; AmbientSound, docs/audio.md): синтез ветра у камеры,
// шелеста травы под ногами и крон леса вокруг по ветру уровня, полю ветра и маскам, журчания ручьёв по растру воды. Громкости —
// доли (амплитуда), полная громкость слоя — около −20 дБ FS
struct AmbientSoundSettings
{
	uint32_t id = 0;				// строка AmbientSound — куда пишет «Save level»
	float volume = 1.0f;			// общая громкость звуков окружения
	// Части: выключенная молчит и голосов не держит
	bool windEnabled = true;
	bool grassEnabled = true;
	bool crownEnabled = true;
	bool streamEnabled = true;
	float windVolume = 0.5f;		// гул и свист ветра у камеры
	float windReferenceSpeed = 8.0f;	// при такой скорости ветра у камеры, м/с, — полная громкость (ветер — по скорости^1,5,
									// шелест — по скорости^0,75)
	float windFrequency = 120.0f;	// середина полосы гула в штиль, Гц; растёт со скоростью ветра
	float grassVolume = 0.5f;		// шелест травы вокруг камеры
	float grassRadius = 5.0f;		// трава в этом круге, м
	float grassHeight = 12.0f;		// камера выше земли на столько, м, — травы не слышно (от 2,5 м тише)
	float grassFrequency = 3500.0f;	// середина полосы шелеста травы, Гц
	float crownVolume = 0.6f;		// шум крон леса вокруг
	float crownReferenceStrength = 0.5f;	// при такой силе ветра уровня (Wind.strength — изгиб деревьев) — полная громкость
	float crownRadius = 80.0f;		// лес в этом круге, м
	float crownFalloff = 15.0f;		// лес на таком расстоянии слышен вдвое тише, дальше — по квадрату расстояния, м
	float crownGustSwing = 18.0f;	// в затишье между порывами кроны тише, чем в порыв, на столько, дБ
	float crownFrequency = 1200.0f;	// середина полосы шума крон, Гц
	float streamVolume = 1.2f;		// журчание ручьёв: громкость шумного места воды уровня (громче 95 % мест) в 3 м
	int32_t streamVoices = 8;		// мест воды, слышных сразу (самых громких у камеры); остальные молчат
	float streamDistance = 250.0f;	// дальше вода не слышна, м
	// Записи мест воды (Sounds.name через запятую): спокойная вода и пороги — место с уклоном воды от streamRapidsFrom (доля
	// перепада ступени 0…1); записей нет — синтез пузырьков. Место берёт одну запись списка — по своему номеру
	std::string streamSound;
	std::string streamRapidsSound;
	float streamRapidsFrom = 0.5f;
};
// Поля строки AmbientSound (SettingsSchema): основное окно «Ambient sound» и подокна «Wind», «Grass», «Crowns», «Streams»
const SettingsSchema<AmbientSoundSettings>& ambientSoundSchema();

// Запись звука — строка Sounds (как Sound Wave в UE; собирает Tools/pack_sound.py): файл WAV в Sounds\, фразы, автор и лицензия,
// громкость записи — множитель у всех слоёв, что её поют (окно «Sounds»)
struct SoundRecord
{
	uint32_t id = 0;				// строка Sounds — куда пишет «Save level»
	float volume = 1.0f;
	std::string name;
	std::string file;
	std::string phrases;			// «начало-конец;…», с
	std::string author;
	std::string license;
};
// Поля строки Sounds, которые правит окно «Sounds» (громкость); имя, файл, фразы и источник — Tools/pack_sound.py
const SettingsSchema<SoundRecord>& soundRecordSchema();

// Слой певцов уровня — строка SoundscapeLayers (как Soundscape в UE; Soundscape, docs/audio.md, «Певцы»): певцы сидят вокруг
// слушателя там, где их место по маскам, и поют фразы одной из записей слоя с паузами, когда велят правила слоя
struct SoundscapeLayerSettings
{
	uint32_t id = 0;				// строка SoundscapeLayers — куда пишет «Save level»; 0 — слой добавлен в редакторе
	std::string name;
	bool enabled = true;			// выключенный слой не поёт
	std::string sound;				// записи — Sounds.name через запятую: у каждого певца — одна из них
	std::string habitat;			// маски места певца через запятую (mask_forest_spruce …); пусто — где угодно
	std::vector<SoundRule> rules;	// строки SoundRules слоя: активность — их произведение (часы суток — правило «hours»)
	int32_t singers = 2;			// певцов сразу при полной активности
	SettingRange distance{ 15.0f, 80.0f };	// от слушателя, м
	SettingRange height{ 3.0f, 12.0f };		// над землёй, м
	SettingRange bout{ 30.0f, 120.0f };		// поёт с одного места, с; потом улетает, появляется другой
	SettingRange pause{ 2.0f, 6.0f };		// между фразами, с; меньше нуля — внахлёст с переходом (хор по кругу без шва)
	SettingRange arrival{ 2.0f, 8.0f };		// место певца опустело — следующий прилетает через столько, с (кукушка — минуты)
	float volume = 0.5f;			// громкость певца в 10 м
	float pitch = 0.05f;			// у каждого певца своя высота: выше или ниже до этой доли
};
// Поля строки SoundscapeLayers (SettingsSchema), подокно слоя в окне «Soundscape». Записи и маски места — текстом в базе, в
// окне — флажками подокон «Sounds» и «Habitat» (Soundscape); правила — строки SoundRules, подокно «Rules»
const SettingsSchema<SoundscapeLayerSettings>& soundscapeLayerSchema();

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
// Откуда слой расстановки берёт экземпляры (ScatterLayers.placement): по маске плотности на сетке мира (mask) или камни
// русел (stream_stones: их раскладывает конвейер рельефа и воды — TerrainHydrology, — слой постоянный, маска не нужна)
enum class ScatterPlacement
{
	mask,
	streamStones,
	// Деревья на дальних горах вокруг камеры (кольцевой слой): за краем карты, по плотности леса гор (FarTerrain), у края — по
	// маске слоя
	farForest
};

// Строка ScatterSets — набор расстановки целиком
struct ScatterSetSettings
{
	uint32_t id = 0;
	// Множитель дальностей LOD у видов теней: < 1 — тень более грубым LOD (как смещение LOD теней в UE), 1 — как в кадре
	float shadowLodScale = 1.0f;
};
// Поля строки ScatterSets (SettingsSchema), окно набора
const SettingsSchema<ScatterSetSettings>& scatterSetSchema();

struct ScatterLayerSettings
{
	uint32_t id = 0;				// строка ScatterLayers — куда пишет «Save level»
	ScatterPlacement placement = ScatterPlacement::mask;
	float cellSize = 1.0f;			// шаг сетки, метры
	float nearBorder = 0.0f;		// кольцо вокруг камеры, метры
	float farBorder = 0.0f;
	float nearFade = 0.0f;			// ширина плавного исчезания у ближней и дальней границы
	float farFade = 0.0f;
	float sizeMultiplier = 1.0f;
	float sizeVariation = 0.3f;		// разброс размера: экземпляр — sizeMultiplier × случайное от 1 − разброс до 1
	// Цвет земли у растения (кольцевой слой): доля цвета земли под ним у корня (0 — свой цвет), высота, к которой она сходит
	// на нет, м, доля по всему растению у дальней границы кольца и разброс яркости по экземплярам (±доля)
	float groundColor = 0.0f;
	float groundColorHeight = 0.0f;
	float groundColorFar = 0.0f;
	float colorVariation = 0.0f;
	// Края кольца: доля полосы исчезания, по которой разбросаны свои дальности экземпляров (0 — все уменьшаются вместе,
	// 1 — каждый исчезает у своего порога); прореживание вдали: с какого расстояния, м, и доля убранных у дальней границы
	// (0 — нет; оставшиеся крупнее)
	float edgeSpread = 0.0f;
	float thinStart = 0.0f;
	float thinning = 0.0f;
	// Исчезание у краёв кольца долей пикселей (дизеринг, как Foliage в UE), а не размером: трава не «вырастает» на краю.
	// Нужен вариант со сменой LOD дизерингом (материалы с DitheredLODTransition); иначе — размером
	bool ditherFade = false;
	// Куртины (несколько видов в слое): доля вида — его вес × e^(4 · сила · шум), шум мира — гладкий, с пятнами размером
	// patchScale, свой у каждой группы куртин (ScatterVariantPlacement::patchGroup): виды растут пятнами, а не вперемешку
	// по ячейкам; размер, м (0 — без куртин), и сила 0…1
	float patchScale = 0.0f;
	float patchStrength = 0.0f;
	// Фильтр по слоям земли (как Landscape Grass Type в UE): роли слоёв материала террейна (TerrainLayers.role) через
	// запятую, на которых слой растёт, — «grass_dry,grass_wet»; пусто — растёт на любых. Порог — доля этих слоёв в весах
	// земли, при которой плотность вдвое меньше; от удвоенной — полная, к нулю — нет (мазок снега по лугу гасит траву)
	std::string layerFilter;
	float layerFilterThreshold = 0.0f;
	float jitter = 0.0f;			// смещение внутри ячейки, доля шага
	DirectX::XMFLOAT3 rotationRange = DirectX::XMFLOAT3( 0.0f, 0.0f, 0.0f );	// предел случайного поворота вокруг осей X, Y, Z, градусы
	bool alignToTerrain = false;	// ось Y инстанса по нормали террейна
	// Флаговые деревья на открытых гребнях: где сдувает (маска открытости ветру TerrainMasks::windExposureMask, 0…1),
	// экземпляр наклонён по ветру уровня на ridgeLean × открытость, градусы, и ниже на долю ridgeShrink × открытость
	float ridgeLean = 0.0f;
	float ridgeShrink = 0.0f;
	// Полог (TerrainCanopy, docs/scatter.md, «Полог»): постоянный слой (лес) кладёт кроны в карту полога — радиус кроны LOD0
	// модели × canopy (0 — не кладёт); слои под ним редеют — плотность под полным пологом × underCanopy (1 — как на открытом)
	float canopy = 0.0f;
	float underCanopy = 1.0f;
	// Посадка по следу основания (как Embed у PCG UE): до самой низкой точки земли под следом, и глубже на groundEmbed ×
	// размер, м — пни и брёвна не висят краем над ямкой
	bool groundFit = false;
	float groundEmbed = 0.0f;
	// Вода (docs/scatter.md, «Вода»): 0 — растёт до уреза (в воде расстановку убирает глубина в точке), 1 — только выше
	// перелива озёр (полоса берега от уреза до перелива — без травы луга), 2 — мелководье: полоса берега озёр и вода до
	// «Lake shallows depth» (рогоз)
	int32_t water = 0;
	// Заросли (docs/scatter.md, «Заросли»): слой растёт только в пятнах шума мира размером standSize, м, — они занимают
	// примерно долю standCover площади; 0 — без зарослей (плотность — маска). Рогоз — густыми куртинами, а не полосой
	float standSize = 0.0f;
	float standCover = 0.3f;
	// Под пологом — пятнами по шуму мира: размер пятен, м, и сила 0…1 (0 — ровно редко)
	float underCanopyPatchSize = 2.0f;
	float underCanopyPatches = 0.0f;
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
	// Сторона кадра импостера, текселей (impostor_frame_size; NULL — 128): 8 × 8 кадров по стольку текселей. Пучку травы в
	// полметра вдали хватит 64 — вчетверо меньше памяти (~4 МБ вместо ~17) и времени запекания
	uint32_t impostorFrameSize = 128;
	// Импостер отбрасывает тень (у травы вдали — нет: тень дальнего кольца не видна, а каскады дешевле) и затемнение его
	// низа: цвет у корня × (1 − shade), к верху модели — без затемнения (0…1)
	bool impostorCastShadow = true;
	float impostorBaseShade = 0.0f;
	// Самотень кроны импостера (impostor_self_shadow; NULL — 1): сила тени солнца от самой модели по её кадрам, 0…1. Без неё
	// дальше дистанции теней крона освещена со всех сторон — бледная и плоская рядом с затенёнными каскадами ближними
	float impostorSelfShadow = 1.0f;
};

// Поля строки ScatterLayers (SettingsSchema): части окна слоя — "" (кольцо, размер, цвет, края), "ground" (порог фильтра
// по слоям земли — при фильтре), "placement" (куртины, поворот, наклон, тень), "impostor" (у слоя с импостером),
// "impostorSelfShadow" (у импостера со смещением глубины); только в базе — persistent, layer_filter (в окне — флажки
// ролей, Scatterer). Не в описании: id, placement, mask
const SettingsSchema<ScatterLayerSettings>& scatterLayerSchema();

// Своя раскладка вида (модели) слоя расстановки — колонки ScatterLayerModels; не задано (NULL в базе) — как у слоя
struct ScatterVariantPlacement
{
	float sizeMultiplier = 1.0f;	// × размер слоя (size_multiplier)
	float sizeVariation = -1.0f;	// свой разброс размера 0…1 (size_variation); < 0 — как у слоя
	int32_t alignToTerrain = -1;	// −1 — как у слоя, 0 — ось Y вертикально, 1 — по нормали террейна (align_to_terrain)
	// Группа куртин (patch_group): модели одной группы — один вид, растут в одних пятнах; < 1 — своя группа у модели
	int32_t patchGroup = 0;
	// Предпочтение слоёв земли (ground_layers, ground_preference): роли слоёв материала (TerrainLayers.role) через запятую,
	// где вид растёт охотнее, — вес вида × (1 − сила + сила × доля этих слоёв в точке); пусто — вес везде одинаков
	std::string groundLayers;
	float groundPreference = 1.0f;
};

// Вариант слоя расстановки — строка ScatterLayerModels: доля ячеек, тень и своя раскладка (то, что пишет «Save level»)
struct ScatterModelSettings
{
	uint32_t id = 0;
	float weight = 1.0f;
	bool castShadow = true;
	std::vector<float> lodDistances;	// свои границы LOD модели у слоя, м; пусто — дальности модели (NULL в базе)
	ScatterVariantPlacement placement;
};

// Слой расстановки для сохранения уровня: строка ScatterLayers и строки её моделей
struct ScatterLayerRecord
{
	ScatterLayerSettings settings;
	std::vector<ScatterModelSettings> models;
};

// Описания полей строк уровня (SettingsSchema, LevelSettings.cpp): колонки базы и окна. Не в описании и у объекта вручную —
// то, что хранится иначе, чем показывается: у ветра направление (в окне — Yaw), у солнца дата (в окне — месяц и день,
// в базе — текст), у постобработки перечисления (имена) и кривая компенсации (текст), у неба и панорамы — файлы
const SettingsSchema<HeightFogSettings>& heightFogSchema();
const SettingsSchema<SkyAtmosphereSettings>& skyAtmosphereSchema();
const SettingsSchema<HDRIBackdropSettings>& hdriBackdropSchema();
// Части окна «Post process» — подокна по эффектам (PostProcess): "exposure" — экспозиция до кривой компенсации,
// "exposureAdaptation" — пределы и адаптация (то же подокно «Exposure» после кривой), "bloom", "stars", "lightShafts",
// "nightVision", "ambientOcclusion", "colorGrading*" — цветокоррекция; имена полей — короткие, в своём подокне
const SettingsSchema<PostProcessSettings>& postProcessSchema();
const SettingsSchema<SunPositionSettings>& sunPositionSchema();
const SettingsSchema<WindSettings>& windSchema();
// Поле ветра по рельефу — колонки field_* строки Wind, подокно «Field» окна «Wind»
const SettingsSchema<WindFieldSettings>& windFieldSchema();

}
