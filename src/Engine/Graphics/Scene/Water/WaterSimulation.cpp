#include "WaterSimulation.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>
#include <DirectXPackedVector.h>
#include "System.h"
#include "ConstantBuffers.h"
#include "Shaders\slots.h"
#include "Logger\Logger.h"
#include "Utils\SlowTask.h"
#include "Utils\Fingerprint.h"
#include "D3D\TextureImages.h"
#include "WaterRipples.h"

namespace GS
{

namespace
{

constexpr uint32_t groupSize = 8;				// numthreads в water_simulation.cs
constexpr uint32_t maxStepsPerFrame = 16;		// больше за кадр не догоняем: симуляция отстаёт, но кадр не растёт
constexpr uint32_t warmupStepsPerSubmit = 500;
// Итераций заполнения низин и растекания метки озера: уровень и метка проходят за итерацию хотя бы ячейку, обычно —
// много (запись на месте); с запасом на путь от края карты до середины и через озеро
constexpr uint32_t fillIterations = 2048;
constexpr uint32_t lakeGrowIterations = 512;
constexpr uint32_t iterationsPerSubmit = 512;
constexpr uint32_t validationWarmupSteps = 600;	// при загрузке — порциями, чтобы одна отправка GPU не шла секундами
constexpr int32_t maxSourceRadius = 8;			// ячеек: размытие источников — цикл (2r + 1)² в шейдере
constexpr float gravity = 9.81f;
constexpr uint32_t tileCells = 32;				// WATER_TILE в water_surface.cs / water_surface.sh
constexpr float visibleDepth = 0.01f;			// м: мельче воды не видно — только мокрая земля
constexpr uint32_t rippleSize = 256;			// текселей карты ряби по стороне (WaterRipples)
constexpr float wetDepth = 0.01f;				// м: ячейка с водой в сводке (logWaterSummary)
constexpr float mmPerHour = 0.001f / 3600.0f;	// мм/ч → м/с
// Течение статичной воды (water_flow.cs)
constexpr uint32_t flowStepsPerSubmit = 50;		// шагов в одной отправке GPU при загрузке
constexpr uint32_t validationFlowSteps = 20;	// под GPU-based validation шаг в десятки раз дольше
// Детальная сетка под GPU-based validation: первый вызов переноса и вязкости слой обрабатывает минутами (их байткод велик —
// грани читают ячейки через индекс плиток), поэтому под ней — без шагов, только начало, грани и итог на первых блоках.
// Шаги проверены под валидацией 2026-10-11 — ошибок нет; после правки этих проходов — снова (validationDetailFlowSteps 2)
constexpr uint32_t validationDetailFlowSteps = 0;
constexpr uint32_t validationDetailFlowBlocks = 256;
constexpr uint32_t flowTile = 8;				// тайл счёта течения — группа потоков 8 × 8 (water_flow.cs)
constexpr float flowMinDepth = 0.05f;			// м: мельче — берег (скорость 0)
constexpr float flowScreen = 1e-3f;				// экранирование давления, м: озеро без истока тоже решается
constexpr float maxViscosityShare = 0.2f;		// ν · dt / l² — не больше: явная вязкость устойчива
// Детальная сетка: шаг — доля шага сетки 1 м (ячейка вчетверо мельче, перенос за шаг — не дальше пары ячеек при 2 м/с)
constexpr float detailTimeStepShare = 0.5f;
// Кэш течения на диске (solveStaticFlow): каталог и версия расчёта — поднять при правке расчёта в C++ (константы, порядок
// проходов, состав сохранённого); правку шейдера течения ключ видит сам. Хранятся последние flowCacheFiles файлов
constexpr const char* flowCacheDirectory = "cache\\water";
constexpr uint32_t flowCacheVersion = 1;
constexpr size_t flowCacheFiles = 4;
// Камни русла в течении: высота над дном — доля поперечника (камень утоплен на четверть, Scatterer::stoneEmbed), и
// коэффициент сопротивления (обтекаемый камень — 0,5–1,2)
constexpr float stoneHeight = 0.5f;
constexpr float stoneDragCoefficient = 1.0f;
constexpr int stoneSpread = 2;	// клеток: среднее сопротивление камней — по квадрату (2 · 2 + 1)² клеток ручья вокруг
constexpr float flowRelaxation = 1.8f;			// верхняя релаксация давления: сходится быстрее Гаусса-Зейделя

bool sameFlow( const WaterSimulationSettings::Flow& a, const WaterSimulationSettings::Flow& b )
{
	return a.settleTime == b.settleTime && a.timeStep == b.timeStep && a.viscosity == b.viscosity && a.manning == b.manning &&
		   a.iterations == b.iterations && a.layer == b.layer && a.detailSettleTime == b.detailSettleTime;
}

Property* addSlider( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit = "" )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( std::max( high, value ) );
	property->setControlType( GUIControlType::SLIDER );
	property->setUnit( unit );
	return property;
}

// Русла конвейера (WaterChannelsSettings) в подокне «Channels»: имя свойства, поле, границы, единицы, подсказка
struct ChannelParameter
{
	const char* name;
	float WaterChannelsSettings::* field;
	float low;
	float high;
	const char* unit;
	const char* tooltip;
};

const ChannelParameter channelParameters[] = {
	{ "Min discharge", &WaterChannelsSettings::minDischarge, 0.0005f, 0.05f, "m3/s", "A channel starts where the discharge is larger" },
	{ "Width coef", &WaterChannelsSettings::widthCoef, 0.5f, 10.0f, "", "Bed width w = a * Q^0.5, m" },
	{ "Min width", &WaterChannelsSettings::minWidth, 0.5f, 10.0f, "m", "Narrowest bed" },
	{ "Depth coef", &WaterChannelsSettings::depthCoef, 0.1f, 5.0f, "", "Incision d = c * Q^0.4, m" },
	{ "Min incision", &WaterChannelsSettings::minIncision, 0.0f, 3.0f, "m", "Shallowest incision" },
	{ "Bank slope", &WaterChannelsSettings::bankSlope, 0.2f, 5.0f, "", "Bank: horizontal metres per metre of incision" },
	{ "Meander length", &WaterChannelsSettings::meanderLength, 5.0f, 200.0f, "m", "Meander wavelength along the stream" },
	{ "Meander amplitude", &WaterChannelsSettings::meanderAmplitude, 0.0f, 10.0f, "m", "Lateral shift at Q >= 0.1 m3/s" },
	{ "Meander max slope", &WaterChannelsSettings::meanderMaxSlope, 0.005f, 0.3f, "", "Steeper terrain has no meanders, m per m" },
	{ "Min slope", &WaterChannelsSettings::minSlope, 0.0f, 0.05f, "", "Bed drop downstream at least, m per m" },
	{ "Lake depth", &WaterChannelsSettings::lakeDepth, 0.01f, 1.0f, "m", "Deeper filled depressions are lakes: not carved, streams end there" },
	{ "Min lake area", &WaterChannelsSettings::minLakeArea, 0.0f, 1000.0f, "m2", "Smaller depressions are a part of the stream bed, not lakes" },
	{ "Thalweg", &WaterChannelsSettings::thalweg, 0.0f, 1.0f, "", "Extra incision in the bed middle, share of the incision" },
	{ "Manning", &WaterChannelsSettings::manning, 0.02f, 0.15f, "s/m^(1/3)", "Stream bed roughness: water depth of the ribbons" },
	{ "Min water slope", &WaterChannelsSettings::minWaterSlope, 0.001f, 0.05f, "", "Slope in the water depth at least, m per m" },
	{ "Min speed", &WaterChannelsSettings::minSpeed, 0.0f, 2.0f, "m/s", "Stream flow at least" },
	{ "Min water depth", &WaterChannelsSettings::minWaterDepth, 0.05f, 1.0f, "m", "Water over the bed at least" },
	{ "Foam slope", &WaterChannelsSettings::foamSlope, 0.02f, 0.5f, "", "Steeper streams foam (fully at twice), m per m" },
	{ "Freeboard", &WaterChannelsSettings::freeboard, 0.0f, 2.0f, "m", "Water stands this much below the bank top: the bed is cut for floods" },
	{ "Source taper", &WaterChannelsSettings::sourceTaper, 0.0f, 100.0f, "m", "From a spring the narrowest bed, incision, water and freeboard grow over this length" },
	{ "Bed shape", &WaterChannelsSettings::bedShape, 1.0f, 12.0f, "", "Bed cross-section: 2 - V (sharp thalweg), more - flat U" },
	{ "Bank roundness", &WaterChannelsSettings::bankRoundness, 0.0f, 1.0f, "", "1 - rounded bank top and toe, 0 - straight bank with a sharp edge" },
	{ "Bend asymmetry", &WaterChannelsSettings::bendAsymmetry, 0.0f, 1.0f, "", "Outer bank of a bend deeper and steeper, inner - a gentle bar" },
	{ "Pool depth", &WaterChannelsSettings::poolDepth, 0.0f, 1.0f, "", "Pools deeper by this share of the incision (0 - no pools)" },
	{ "Pool spacing", &WaterChannelsSettings::poolSpacing, 2.0f, 15.0f, "", "Pool and riffle every this many channel widths" },
	{ "Bed noise", &WaterChannelsSettings::bedNoise, 0.0f, 0.5f, "m", "Unevenness of the bed and banks" },
	{ "Bed noise scale", &WaterChannelsSettings::bedNoiseScale, 0.3f, 5.0f, "m", "Size of the bed unevenness" },
	{ "Lake depth ratio", &WaterChannelsSettings::lakeDepthRatio, 0.0f, 0.3f, "", "Deepest lake point per square root of the lake area" },
	{ "Lake min depth", &WaterChannelsSettings::lakeMinDepth, 0.0f, 10.0f, "m", "Deepest point of a small lake at least" },
	{ "Lake max depth", &WaterChannelsSettings::lakeMaxDepth, 0.0f, 50.0f, "m", "Deepest point of a large lake at most" },
	{ "Lake shelf width", &WaterChannelsSettings::lakeShelfWidth, 0.0f, 20.0f, "m", "Shallow shelf along the shore" },
	{ "Lake shelf depth", &WaterChannelsSettings::lakeShelfDepth, 0.0f, 3.0f, "m", "Water depth at the shelf edge" },
	{ "Lake drop slope", &WaterChannelsSettings::lakeDropSlope, 0.05f, 2.0f, "", "Drop-off beyond the shelf, m per m" },
	{ "Stone density", &WaterChannelsSettings::stoneDensity, 0.0f, 5.0f, "1/m", "Stones per metre of a stream (bed and banks): denser on riffles and steep reaches" },
	{ "Stone size", &WaterChannelsSettings::stoneSize, 0.05f, 1.0f, "m", "Typical stone diameter" },
	{ "Bank stones", &WaterChannelsSettings::bankStones, 0.0f, 1.0f, "", "Share of the stones at the banks, near the water edge" },
	{ "Boulder density", &WaterChannelsSettings::boulderDensity, 0.0f, 0.5f, "1/m", "Boulders per metre of a stream: more on steep reaches" },
	{ "Boulder size", &WaterChannelsSettings::boulderSize, 0.3f, 3.0f, "m", "Boulder diameter" },
};

// Множители кривой русла в подокне «Streams»: имя свойства, поле, подсказка
struct StreamScale
{
	const char* name;
	float StreamCurve::* field;
	const char* tooltip;
};

const StreamScale streamScales[] = {
	{ "Width scale", &StreamCurve::widthScale, "Bed width of this stream, times the common one" },
	{ "Depth scale", &StreamCurve::depthScale, "Incision of this stream, times the common one" },
	{ "Freeboard scale", &StreamCurve::freeboardScale, "Bank above the water, times the common one" },
	{ "Bank slope scale", &StreamCurve::bankSlopeScale, "Gentler (> 1) or steeper (< 1) banks" },
	{ "Thalweg scale", &StreamCurve::thalwegScale, "Deeper middle of the bed, times the common one" },
	{ "Roughness scale", &StreamCurve::roughnessScale, "Bed roughness (Manning): deeper, slower water at > 1" },
	{ "Discharge scale", &StreamCurve::dischargeScale, "Water of this stream: wider, deeper bed at > 1" },
	{ "Stone scale", &StreamCurve::stoneScale, "Stones and boulders of this stream, times the common density" },
};

// Параметры генератора кривых русел: их правка генерирует кривые заново при следующей загрузке (отпечаток), а не перестраивает
// русла сразу
bool generatorParameter( float WaterChannelsSettings::* field )
{
	return field == &WaterChannelsSettings::minDischarge || field == &WaterChannelsSettings::meanderLength ||
		   field == &WaterChannelsSettings::meanderAmplitude || field == &WaterChannelsSettings::meanderMaxSlope ||
		   field == &WaterChannelsSettings::lakeDepth || field == &WaterChannelsSettings::minLakeArea;
}

bool sameSources( const WaterSimulationSettings& a, const WaterSimulationSettings& b )
{
	return a.sourceRate == b.sourceRate && a.flowStart == b.flowStart && a.flowFull == b.flowFull && a.sourceRadius == b.sourceRadius;
}

}

WaterSimulation::WaterSimulation() : SceneObject( "Water simulation" )
{
	m_properties.setName( "Water simulation" );
	m_surfaceProperties.setName( "Surface" );
}

bool WaterSimulation::initialize( const Settings& settings, const TerrainHeightSource& terrain, const TerrainHydrology::Result& hydrology )
{
	const auto start = std::chrono::high_resolution_clock::now();
	m_initial = settings;
	m_terrain = &terrain;
	const TerrainHeight height = terrain.terrainHeight();
	m_size = height.mapSize;
	m_cellSize = height.worldSize / std::max( m_size, 1u );
	if( m_size == 0 || !height.heightMap )
	{
		LOG( "Water simulation needs a terrain height map" );
		return false;
	}

	addSlider( m_properties, "Time scale", settings.timeScale, 0.0f, 100.0f );
	addSlider( m_properties, "Source rate", settings.sourceRate, 0.0f, 2.0f, "l/s" );
	addSlider( m_properties, "Flow start", settings.flowStart, 100.0f, 100000.0f, "m2" );
	addSlider( m_properties, "Flow full", settings.flowFull, 100.0f, 1000000.0f, "m2" );
	addSlider( m_properties, "Source radius", settings.sourceRadius, 0.0f, 16.0f, "m" );
	addSlider( m_properties, "Rain", settings.rain, 0.0f, 50.0f, "mm/h" );
	addSlider( m_properties, "Evaporation", settings.evaporation, 0.0f, 50.0f, "mm/h" );
	addSlider( m_properties, "Manning roughness", settings.manning, 0.0f, 0.2f );

	Property* property = m_surfaceProperties.insert( "Absorption", settings.absorption );
	property->setLow( 0.0f );
	property->setHigh( 5.0f );
	property->setControlType( GUIControlType::DRAG );
	property->setUnit( "1/m" )->setTooltip( "Absorption per channel R, G, B" );
	m_surfaceProperties.insert( "Scatter color", settings.scatterColor )->setControlType( GUIControlType::COLOR );
	addSlider( m_surfaceProperties, "Scatter strength", settings.scatterStrength, 0.0f, 0.1f );
	addSlider( m_surfaceProperties, "Roughness", settings.roughness, 0.0f, 0.5f );
	addSlider( m_surfaceProperties, "Ripple scale", settings.rippleScale, 0.5f, 20.0f, "m" );
	addSlider( m_surfaceProperties, "Ripple strength", settings.rippleStrength, 0.0f, 4.0f );
	addSlider( m_surfaceProperties, "Calm ripple", settings.calmRipple, 0.0f, 2.0f );
	addSlider( m_surfaceProperties, "Refraction", settings.refraction, 0.0f, 0.1f );
	addSlider( m_surfaceProperties, "Ripple stretch", settings.rippleStretch, 1.0f, 6.0f )
		->setTooltip( "How many times the ripple pattern is longer along the flow than across it on running water" );
	addSlider( m_surfaceProperties, "Foam speed", settings.foamSpeed, 0.1f, 5.0f, "m/s" );
	addSlider( m_surfaceProperties, "Foam shear", settings.foamShear, 0.1f, 10.0f, "1/s" );
	addSlider( m_surfaceProperties, "Detail distance", settings.detailDistance, 0.0f, 1000.0f, "m" )
		->setTooltip( "Nearer the water surface of streams is drawn by the detail tiles (0.25 m): farther the 1 m grid differs by less "
					  "than a pixel, and a detail tile has 16 times as many triangles; 0 - the 1 m grid only" );
	m_properties.addSubContainer( &m_surfaceProperties );

	// Течение статичной воды: установившийся расчёт при загрузке, правка — расчёт заново
	m_flowProperties.setName( "Flow" );
	const char* recomputed = "; the flow of streams and lakes is recomputed on change (static water)";
	addSlider( m_flowProperties, "Settle time", settings.flow.settleTime, 0.0f, 3600.0f, "s" )
		->setTooltip( std::string( "Flow of streams and lakes is computed this long towards its steady state" ) + recomputed );
	addSlider( m_flowProperties, "Time step", settings.flow.timeStep, 0.1f, 2.0f, "s" )
		->setTooltip( std::string( "Step of the flow computation" ) + recomputed );
	addSlider( m_flowProperties, "Viscosity", settings.flow.viscosity, 0.0f, 1.0f, "m2/s" )
		->setTooltip( std::string( "Eddy viscosity: a wider, shorter jet of an inflowing stream" ) + recomputed );
	addSlider( m_flowProperties, "Bed roughness", settings.flow.manning, 0.0f, 0.1f, "s/m^(1/3)" )
		->setTooltip( std::string( "Manning roughness of the bed of streams and lakes: a jet, a bend or a confluence settles "
								   "back to the Manning flow sooner at more" ) + recomputed );
	addSlider( m_flowProperties, "Surface layer", settings.flow.layer, 0.1f, 5.0f, "m" )
		->setTooltip( std::string( "Depth of the flowing layer: a stream spreads over a lake near the surface" ) + recomputed );
	Property* iterations = m_flowProperties.insert( "Pressure iterations", settings.flow.iterations );
	iterations->setLow( 1.0f );
	iterations->setHigh( 100.0f );
	iterations->setTooltip( std::string( "Pressure solver iterations per step" ) + recomputed );
	addSlider( m_flowProperties, "Detail settle time", settings.flow.detailSettleTime, 0.0f, 600.0f, "s" )
		->setTooltip( std::string( "Flow on the detail grid of the streams (0.25 m) is computed this long from the 1 m flow: "
								   "around the stones, faster at the axis than at the banks; 0 - the 1 m flow" ) + recomputed );
	m_properties.addSubContainer( &m_flowProperties );

	// Русла и ручьи строит конвейер рельефа при загрузке — правки применяются при следующей загрузке уровня
	m_channelsProperties.setName( "Channels" );
	for( const ChannelParameter& parameter : channelParameters )
		addSlider( m_channelsProperties, parameter.name, settings.channels.*parameter.field, parameter.low, parameter.high, parameter.unit )
			->setTooltip( std::string( parameter.tooltip ) +
						  ( generatorParameter( parameter.field ) ? "; generates the stream curves anew at the next level load" :
																	"; the channels are rebuilt on change" ) );
	Property* smooth = m_channelsProperties.insert( "Smooth passes", settings.channels.smooth );
	smooth->setLow( 0.0f );
	smooth->setHigh( 20.0f );
	smooth->setTooltip( "Smoothing passes of the channel axis along the flow; generates the stream curves anew at the next level load" );
	Property* paint = m_channelsProperties.insert( "Paint layer", settings.channels.paintLayer );
	paint->setLow( 0.0f );
	paint->setHigh( 7.0f );
	paint->setTooltip( "Terrain material layer of the bed and banks (TerrainLayers.layer); the channels are rebuilt on change" );
	m_properties.addSubContainer( &m_channelsProperties );

	// Кривые русел: подокно на кривую — включение и множители; правка помечает кривую правленной («Save level»):
	// новая генерация её не заменит
	m_streamsProperties.setName( "Streams" );
	m_streamProperties.clear();
	m_curves = settings.streams;
	m_builtCurves = settings.streams;
	m_builtChannels = settings.channels;
	m_curvesDirty = false;
	// Кривые живут в m_curves: settings() их не копирует (его зовут каждый кадр)
	m_initial.streams.clear();
	for( const StreamCurve& curve : m_curves )
	{
		auto container = std::make_unique<PropertyContainer>();
		container->setName( curve.name + ( curve.generated ? "" : " (manual)" ) + ( curve.edited ? " (edited)" : "" ) + " #" +
							std::to_string( curve.id ) );
		container->insert( "Enabled", curve.enabled )->setTooltip( "Off - no channel and no water; the channels are rebuilt on change" );
		for( const StreamScale& scale : streamScales )
			addSlider( *container, scale.name, curve.*scale.field, 0.1f, 4.0f )
				->setTooltip( std::string( scale.tooltip ) + "; the channels are rebuilt on change" );
		m_streamsProperties.addSubContainer( container.get() );
		m_streamProperties.push_back( std::move( container ) );
	}
	m_properties.addSubContainer( &m_streamsProperties );

	DMD3D& d3d = DMD3D::instance();
	if( !m_sourcesShader.Initialize( "Shaders\\water_simulation.cs", "mainSources" ) ||
		!m_fluxShader.Initialize( "Shaders\\water_simulation.cs", "mainFlux" ) ||
		!m_waterShader.Initialize( "Shaders\\water_simulation.cs", "mainWater" ) ||
		!m_fillInitShader.Initialize( "Shaders\\water_simulation.cs", "mainFillInit" ) ||
		!m_fillShader.Initialize( "Shaders\\water_simulation.cs", "mainFill" ) ||
		!m_lakeInitShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeInit" ) ||
		!m_lakeGrowShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeGrow" ) ||
		!m_lakeApplyShader.Initialize( "Shaders\\water_simulation.cs", "mainLakeApply" ) ||
		!m_staticShader.Initialize( "Shaders\\water_simulation.cs", "mainStatic" ) ||
		!d3d.createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) )
		return false;

	auto create = [&]( DXGI_FORMAT format, const char* name, Texture& texture, StorageView& uav, ShaderView* srv )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
		if( !d3d.createTexture( desc, nullptr, texture ) || !d3d.createStorageView( texture, {}, uav ) ||
			( srv && !d3d.createShaderView( texture, {}, *srv ) ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		// Без начальных данных память не обнулена: первая операция — очистка
		d3d.clearStorageView( uav );
		return true;
	};
	if( !create( DXGI_FORMAT_R32_FLOAT, "Water depth", m_water, m_waterUAV, nullptr ) ||
		!create( DXGI_FORMAT_R32G32B32A32_FLOAT, "Water flux", m_flux, m_fluxUAV, nullptr ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water sources", m_sources, m_sourcesUAV, &m_sourcesView ) ||
		!create( DXGI_FORMAT_R16G16B16A16_FLOAT, "Water state", m_output, m_outputUAV, &m_outputView ) ||
		!create( DXGI_FORMAT_R32_FLOAT, "Water memory", m_memory, m_memoryUAV, nullptr ) ||
		!d3d.createShaderView( m_water, {}, m_waterView ) || !createSurface() )
		return false;

	// Источники-помощники — гауссовы пятна притока в mainSources: x, z, расход м³/с, σ (не меньше ячейки)
	if( !settings.sources.empty() )
	{
		std::vector<DirectX::XMFLOAT4> helpers;
		for( const WaterSource& source : settings.sources )
			helpers.emplace_back( source.position.x, source.position.y, source.rate * 0.001f, std::max( source.radius * 0.5f, m_cellSize ) );
		BufferDesc desc;
		desc.size = static_cast<uint32_t>( helpers.size() * sizeof( DirectX::XMFLOAT4 ) );
		desc.stride = sizeof( DirectX::XMFLOAT4 );
		desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, helpers.data(), m_helpers ) || !d3d.createShaderView( m_helpers, {}, m_helpersView ) )
		{
			LOG( "Failed to create water source helpers" );
			return false;
		}
		d3d.setName( m_helpers, "Water source helpers" );
		m_helperCount = static_cast<uint32_t>( helpers.size() );
		for( const WaterSource& source : settings.sources )
			LOG( "Water source " + source.name + ": " + std::to_string( source.rate ) + " l/s at " +
				 std::to_string( source.position.x ) + ", " + std::to_string( source.position.y ) );
	}

	// Режим static: озёра, ручьи и их вода — из конвейера рельефа (TerrainHydrology), без шагов и налива на GPU
	m_static = settings.staticWater;
	if( !createHydrologyTextures( hydrology ) )
		return false;
	if( m_static )
		m_staticWaterMap = &m_staticWaterView;
	buildSources( settings );
	// Доли — по времени загрузки (Release, Test: растр русел и источники ~0,16 с — до этих шагов, течение ~0,5 с):
	// течение озёр (static) или налив озёр и прогрев (simulated) — почти всё
	SlowTask task( 1.0f );
	task.enterProgressFrame( 0.1f, "Water surface" );
	// Режим simulated: озёра до уровня перелива, затем до установившегося течения — порциями с ожиданием GPU
	if( !m_static )
		task.enterProgressFrame( 0.3f, "Lakes" );
	if( !m_static && !fillLakes() )
		return false;
	if( m_static )
	{
		// Статичная вода: озёра и ручьи — одно поле из растра; шагов нет, течение озёр — установившийся расчёт
		if( !buildStaticWater( settings ) || !createFlowResources() )
			return false;
		task.enterProgressFrame( 0.9f, "Flow" );
		solveStaticFlow( settings.flow );
		m_initialized = true;
		logWaterSummary();
		return true;
	}
	uint32_t warmupSteps = settings.timeStep > 0.0f ? static_cast<uint32_t>( settings.warmupTime / settings.timeStep ) : 0;
	if( d3d.gpuValidation() && warmupSteps > validationWarmupSteps )
	{
		// Под GPU-based validation шаг в десятки раз дольше: озёра уже налиты, ручьи досчитаются в игре
		LOG( "Water simulation: warm-up is cut to " + std::to_string( validationWarmupSteps ) + " steps under GPU-based validation" );
		warmupSteps = validationWarmupSteps;
	}
	task.enterProgressFrame( 0.6f, "Warm-up" );
	SlowTask warmup( static_cast<float>( std::max( warmupSteps, 1u ) ) );
	for( uint32_t done = 0; done < warmupSteps; done += warmupStepsPerSubmit )
	{
		// Порция — граница шага прогресса: параметры и привязки она ставит сама
		warmup.enterProgressFrame( static_cast<float>( std::min( warmupStepsPerSubmit, warmupSteps - done ) ) );
		setParameters( settings );
		step( std::min( warmupStepsPerSubmit, warmupSteps - done ) );
		d3d.waitForGpu();
	}
	buildSurface();
	m_initialized = true;
	const auto end = std::chrono::high_resolution_clock::now();
	LOG( "Water simulation " + std::to_string( m_size ) + "x" + std::to_string( m_size ) + ", warm-up " +
		 std::to_string( warmupSteps ) + " steps for ms: " +
		 std::to_string( std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count() / 1000.0 ) );
	logWaterSummary();
	return true;
}

bool WaterSimulation::createSurface()
{
	DMD3D& d3d = DMD3D::instance();
	m_tilesPerSide = ( m_size + tileCells - 1 ) / tileCells;
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	if( !m_surfaceShader.Initialize( "Shaders\\water_surface.cs", "mainSurface" ) ||
		!m_tilesResetShader.Initialize( "Shaders\\water_surface.cs", "mainTilesReset" ) ||
		!m_tilesShader.Initialize( "Shaders\\water_surface.cs", "mainTiles" ) ||
		!d3d.createShaderConstantBuffer( sizeof( TilesParameters ), m_tilesBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( SurfaceParameters ), m_surfaceBuffer ) )
		return false;
	if( !WaterRipples::create( rippleSize, m_ripple, m_rippleView ) )
	{
		LOG( "Failed to create the water ripple texture" );
		return false;
	}
	d3d.setName( m_ripple, "Water ripple" );

	TextureDesc levelDesc;
	levelDesc.width = m_size;
	levelDesc.height = m_size;
	levelDesc.format = DXGI_FORMAT_R32_FLOAT;
	levelDesc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	BufferDesc boundsDesc;
	boundsDesc.size = tileCount * sizeof( DirectX::XMFLOAT2 );
	boundsDesc.stride = sizeof( DirectX::XMFLOAT2 );
	boundsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc listDesc;
	listDesc.size = tileCount * sizeof( uint32_t );
	listDesc.stride = sizeof( uint32_t );
	listDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	BufferDesc argsDesc;
	argsDesc.size = 32;
	argsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::indirectArgs | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !d3d.createTexture( levelDesc, nullptr, m_level ) || !d3d.createStorageView( m_level, {}, m_levelUAV ) ||
		!d3d.createShaderView( m_level, {}, m_levelView ) ||
		!d3d.createBuffer( boundsDesc, nullptr, m_tileBounds ) || !d3d.createStorageView( m_tileBounds, {}, m_tileBoundsUAV ) ||
		!d3d.createShaderView( m_tileBounds, {}, m_tileBoundsView ) ||
		!d3d.createBuffer( listDesc, nullptr, m_tileList ) || !d3d.createStorageView( m_tileList, {}, m_tileListUAV ) ||
		!d3d.createShaderView( m_tileList, {}, m_tileListView ) ||
		!d3d.createBuffer( argsDesc, nullptr, m_drawArgs ) || !d3d.createStorageView( m_drawArgs, rawView, m_drawArgsUAV ) )
	{
		LOG( "Failed to create water surface resources" );
		return false;
	}
	d3d.setName( m_level, "Water surface level" );
	d3d.setName( m_tileBounds, "Water tile bounds" );
	d3d.setName( m_tileList, "Water visible tiles" );
	d3d.setName( m_drawArgs, "Water indirect command" );

	m_tileMesh.initialize( tileCells + 1, tileCells + 1 );
	m_surfaceProgram.setLayoutDesc( { { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0 } } );
	if( !m_surfaceProgram.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\water.vs" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\water.ps" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\water.vs", "WATER_DETAIL" ) ||
		!m_surfaceProgram.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\water.ps", "WATER_DETAIL" ) )
	{
		LOG( "Water surface: shader compilation failed" );
		return false;
	}
	m_surfacePhase = m_surfaceProgram.createPhase( 0, 0 );
	m_detailPhase = m_surfaceProgram.createPhase( 1, 1 );

	// Детальная поверхность у русел: сетка плитки — вершина на тексель плитки с полем, свои список экземпляров и команда
	const uint32_t side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	m_detailMesh.initialize( static_cast<uint16_t>( side ), static_cast<uint16_t>( side ) );
	if( !m_detailSurfaceShader.Initialize( "Shaders\\water_detail.cs", "mainDetailSurface" ) ||
		!d3d.createShaderConstantBuffer( sizeof( DetailParameters ), m_detailBuffer ) ||
		!d3d.createBuffer( listDesc, nullptr, m_detailTileList ) || !d3d.createStorageView( m_detailTileList, {}, m_detailTileListUAV ) ||
		!d3d.createShaderView( m_detailTileList, {}, m_detailTileListView ) ||
		!d3d.createBuffer( argsDesc, nullptr, m_detailDrawArgs ) || !d3d.createStorageView( m_detailDrawArgs, rawView, m_detailDrawArgsUAV ) )
	{
		LOG( "Failed to create water detail surface resources" );
		return false;
	}
	d3d.setName( m_detailTileList, "Water visible detail tiles" );
	d3d.setName( m_detailDrawArgs, "Water detail indirect command" );
	return m_surfacePhase >= 0 && m_detailPhase >= 0;
}

void WaterSimulation::buildSurface()
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water surface";
	pass.reads = { { &heightMap, "height map" }, { &m_waterView, "water depth" } };
	pass.writes = { { &m_levelUAV, "water level" }, { &m_tileBoundsUAV, "water tile bounds" } };
	d3d.beginPass( pass );
	setParameters( settings() );
	TilesParameters tiles = {};
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, m_waterView );
	d3d.setUAV( 0, m_levelUAV );
	d3d.setUAV( 1, m_tileBoundsUAV );
	m_surfaceShader.dispatchGroups( m_tilesPerSide, m_tilesPerSide, 1 );
	m_surfaceDirty = false;
	if( m_detailTileCount > 0 )
		buildDetailSurface();
}

void WaterSimulation::buildDetailSurface()
{
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_terrain->terrainHeight();
	if( !height.detailTiles )
		return;
	PassDesc pass;
	pass.name = "Water detail surface";
	pass.reads = { { &m_detailStaticView, "detail static water" }, { height.detailTiles, "terrain detail tiles" },
				   { &m_outputView, "water state" }, { &m_detailTileCoordsView, "detail tile coordinates" } };
	pass.writes = { { &m_detailWaterUAV, "detail water" }, { &m_detailBoundsUAV, "detail tile bounds" } };
	d3d.beginPass( pass );
	DetailParameters params = {};
	params.heightMultiplier = height.heightMultiplier;
	params.heightOffset = height.heightOffset;
	params.worldSize = height.worldSize;
	params.tileSize = m_detailTileSize;
	params.visibleDepth = visibleDepth;
	params.side = m_detailSide;
	Device::updateResourceData( m_detailBuffer, params );
	d3d.setConstantBuffer( 5, m_detailBuffer );
	// Границы уровней плиток — наибольшие значения (атомарный max с нуля): нули — плитка без воды
	d3d.clearStorageView( m_detailBoundsUAV );
	d3d.setSRV( 0, m_detailStaticView );
	d3d.setSRV( 1, *height.detailTiles );
	d3d.setSRV( 2, m_outputView );
	d3d.setSRV( 3, m_detailTileCoordsView );
	d3d.setUAV( 0, m_detailWaterUAV );
	d3d.setUAV( 1, m_detailBoundsUAV );
	const uint32_t groups = ( m_detailSide + 7 ) / 8;
	m_detailSurfaceShader.dispatchGroups( groups, groups, m_detailTileCount );
}

DirectX::XMFLOAT4 WaterSimulation::detailParameters() const
{
	if( !m_initialized || m_detailTileCount == 0 )
		return DirectX::XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	return DirectX::XMFLOAT4( m_terrain->terrainHeight().worldSize, m_detailTileSize, 0.0f, 0.0f );
}

void WaterSimulation::cullTiles( const RenderView& view )
{
	DMD3D& d3d = DMD3D::instance();
	PassDesc pass;
	pass.name = "Water tiles";
	pass.reads = { { &m_tileBoundsView, "water tile bounds" } };
	pass.writes = { { &m_drawArgsUAV, "water indirect command" }, { &m_tileListUAV, "water visible tiles" },
					{ &m_detailDrawArgsUAV, "water detail indirect command" }, { &m_detailTileListUAV, "water visible detail tiles" } };
	if( m_detailTileCount > 0 )
		pass.reads.push_back( { &m_detailBoundsView, "detail tile bounds" } );
	d3d.beginPass( pass );
	TilesParameters tiles = {};
	for( int i = 0; i < 6; ++i )
		DirectX::XMStoreFloat4( &tiles.planes[i], view.frustum.planes()[i] );
	tiles.tilesPerSide = m_tilesPerSide;
	tiles.visibleDepth = visibleDepth;
	tiles.worldSize = m_terrain->terrainHeight().worldSize;
	tiles.indexCount = m_tileMesh.indexCount();
	tiles.cameraPosition = view.position;
	tiles.detailDistance = m_detailTileCount > 0 ? std::max( settings().detailDistance, 0.0f ) : 0.0f;
	tiles.detailIndexCount = m_detailMesh.indexCount();
	Device::updateResourceData( m_tilesBuffer, tiles );
	d3d.setConstantBuffer( 5, m_tilesBuffer );
	setParameters( settings() );
	d3d.setUAV( 2, m_drawArgsUAV );
	d3d.setUAV( 4, m_detailDrawArgsUAV );
	m_tilesResetShader.dispatchGroups( 1, 1, 1 );
	d3d.setSRV( 4, m_tileBoundsView );
	if( m_detailTileCount > 0 )
		d3d.setSRV( 6, m_detailBoundsView );
	d3d.setUAV( 2, m_drawArgsUAV );
	d3d.setUAV( 3, m_tileListUAV );
	d3d.setUAV( 4, m_detailDrawArgsUAV );
	d3d.setUAV( 5, m_detailTileListUAV );
	const uint32_t tileCount = m_tilesPerSide * m_tilesPerSide;
	m_tilesShader.dispatchGroups( ( tileCount + 63 ) / 64, 1, 1 );
}

void WaterSimulation::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	// Только главный вид: тени вода не отбрасывает. Полупрозрачные — от дальних к ближним, вода — первой
	if( m_initialized && view.index == 0 )
		collector.addCustom( passBit( MeshPass::transparent ), view.farPlane, true );
}

void WaterSimulation::warmPipelines( const PassStates& states )
{
	if( !m_initialized )
		return;
	m_surfaceProgram.warmPipelines( { { RasterState::solid, DepthState::readOnly, BlendState::alpha },
									  { RasterState::wireframe, DepthState::readOnly, BlendState::alpha } },
									states.scene, { m_surfacePhase, m_detailPhase } );
}

void WaterSimulation::renderCustom( const RenderContext& context )
{
	if( context.pass != MeshPass::transparent )
		return;
	DMD3D& d3d = DMD3D::instance();
	const TerrainHeight height = m_terrain->terrainHeight();
	SurfaceParameters params = {};
	params.size = m_size;
	params.cellSize = m_cellSize;
	params.worldSize = height.worldSize;
	params.tilesPerSide = m_tilesPerSide;
	const Settings current = settings();
	params.absorption = current.absorption;
	params.rippleStretch = std::max( current.rippleStretch, 1.0f );
	params.scatterColor = DirectX::XMFLOAT3( current.scatterColor.x * current.scatterStrength, current.scatterColor.y * current.scatterStrength,
											 current.scatterColor.z * current.scatterStrength );
	params.rippleScale = std::max( current.rippleScale, 0.01f );
	params.rippleStrength = current.rippleStrength;
	params.calmRipple = current.calmRipple;
	params.refraction = current.refraction;
	params.roughness = current.roughness;
	params.foamSpeed = std::max( current.foamSpeed, 0.01f );
	params.foamShear = std::max( current.foamShear, 0.01f );
	params.detailDistance = m_detailTileCount > 0 ? std::max( current.detailDistance, 0.0f ) : 0.0f;
	Device::updateResourceData( m_surfaceBuffer, params );

	ScopedRenderState state( context.frameRaster );
	d3d.setVertexBuffer( m_tileMesh.vertexBuffer(), sizeof( DirectX::XMFLOAT3 ) );
	d3d.setIndexBuffer( m_tileMesh.indexBuffer(), DXGI_FORMAT_R32_UINT );
	m_surfaceProgram.setPass( m_surfacePhase );
	d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_surfaceBuffer );
	d3d.setSRV( 0, m_levelView );
	d3d.setSRV( 1, m_rippleView );
	d3d.setSRV( 2, m_stoneDragView );
	d3d.setSRV( SLOT_INSTANCE_DATA, m_tileListView );
	d3d.drawIndexedInstancedIndirectCount( m_drawArgs, 0, 1, m_drawArgs, 24 );
	// Детальные плитки у русел: своя сетка (0,25 м); уровень и течение — детальная вода (SLOT_WATER_DETAIL); грубая
	// поверхность в этих плитках отсечена в water.ps
	if( m_detailTileCount > 0 )
	{
		d3d.setVertexBuffer( m_detailMesh.vertexBuffer(), sizeof( DirectX::XMFLOAT3 ) );
		d3d.setIndexBuffer( m_detailMesh.indexBuffer(), DXGI_FORMAT_R32_UINT );
		m_surfaceProgram.setPass( m_detailPhase );
		d3d.setConstantBuffer( SLOT_CB_MATERIAL, m_surfaceBuffer );
		d3d.setSRV( 1, m_rippleView );
		d3d.setSRV( 2, m_stoneDragView );
		d3d.setSRV( SLOT_INSTANCE_DATA, m_detailTileListView );
		d3d.drawIndexedInstancedIndirectCount( m_detailDrawArgs, 0, 1, m_detailDrawArgs, 24 );
	}
}

bool WaterSimulation::createHydrologyTextures( const TerrainHydrology::Result& hydrology )
{
	if( hydrology.size != m_size || hydrology.channelFlow.size() != static_cast<size_t>( m_size ) * m_size )
	{
		LOG( "Water simulation: water channels differ from the height map in size" );
		return false;
	}
	// Тайлы счёта течения (solveFlow): с ячейками озёр и ручьёв и восемь соседних — сухой берег вокруг
	const uint32_t tiles = ( m_size + flowTile - 1 ) / flowTile;
	std::vector<uint8_t> lakeTile( static_cast<size_t>( tiles ) * tiles, 0 );
	for( uint32_t row = 0; row < m_size; ++row )
		for( uint32_t col = 0; col < m_size; ++col )
		{
			const DirectX::XMFLOAT4& water = hydrology.staticWater[static_cast<size_t>( row ) * m_size + col];
			if( water.w > -1e8f || water.x > -1e8f )
				lakeTile[static_cast<size_t>( row / flowTile ) * tiles + col / flowTile] = 1;
		}
	m_flowTileList.clear();
	for( uint32_t ty = 0; ty < tiles; ++ty )
		for( uint32_t tx = 0; tx < tiles; ++tx )
		{
			bool adjacent = false;
			for( int dy = -1; dy <= 1 && !adjacent; ++dy )
				for( int dx = -1; dx <= 1 && !adjacent; ++dx )
				{
					const int x = static_cast<int>( tx ) + dx;
					const int y = static_cast<int>( ty ) + dy;
					adjacent = x >= 0 && y >= 0 && x < static_cast<int>( tiles ) && y < static_cast<int>( tiles ) &&
						   lakeTile[static_cast<size_t>( y ) * tiles + x];
				}
			if( adjacent )
				m_flowTileList.emplace_back( tx, ty );
		}

	DMD3D& d3d = DMD3D::instance();
	if( !m_flowTileList.empty() )
	{
		BufferDesc desc;
		desc.size = static_cast<uint32_t>( m_flowTileList.size() * sizeof( DirectX::XMUINT2 ) );
		desc.stride = sizeof( DirectX::XMUINT2 );
		desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( desc, m_flowTileList.data(), m_flowTiles ) || !d3d.createShaderView( m_flowTiles, {}, m_flowTilesView ) )
		{
			LOG( "Failed to create water flow tiles" );
			return false;
		}
		d3d.setName( m_flowTiles, "Water flow tiles" );
	}
	auto create = [&]( DXGI_FORMAT format, const void* data, uint32_t texelBytes, const char* name, Texture& texture, ShaderView& view )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = format;
		TextureData initial;
		initial.data = data;
		initial.rowPitch = m_size * texelBytes;
		initial.slicePitch = initial.rowPitch * m_size;
		if( !d3d.createTexture( desc, &initial, texture ) || !d3d.createShaderView( texture, {}, view ) )
		{
			LOG( std::string( "Failed to create water texture " ) + name );
			return false;
		}
		d3d.setName( texture, name );
		return true;
	};
	// Камни русел тормозят течение (water_flow.cs, mainFlowAdvect): у клетки — площадь камней поперёк потока на квадратный
	// метр дна, только погружённая часть — до уровня воды, — и её среднее по клеткам ручья вокруг (stoneSpread)
	std::vector<float> stoneArea( static_cast<size_t>( m_size ) * m_size, 0.0f );
	const float worldSize = m_cellSize * m_size;
	for( const TerrainHydrology::Stone& stone : hydrology.stones )
	{
		const int col = static_cast<int>( stone.position.x / m_cellSize );
		const int row = static_cast<int>( ( worldSize - stone.position.z ) / m_cellSize );
		if( col < 0 || row < 0 || col >= static_cast<int>( m_size ) || row >= static_cast<int>( m_size ) )
			continue;
		const size_t index = static_cast<size_t>( row ) * m_size + col;
		const float level = std::max( hydrology.staticWater[index].x, hydrology.staticWater[index].w );
		const float submerged = std::clamp( level - stone.position.y, 0.0f, stoneHeight * stone.size );
		stoneArea[index] += stone.size * submerged / ( m_cellSize * m_cellSize );
	}
	std::vector<DirectX::XMFLOAT2> stoneDrag( stoneArea.size(), DirectX::XMFLOAT2( 0.0f, 0.0f ) );
	for( int row = 0; row < static_cast<int>( m_size ); ++row )
		for( int col = 0; col < static_cast<int>( m_size ); ++col )
		{
			const size_t index = static_cast<size_t>( row ) * m_size + col;
			if( hydrology.staticWater[index].x <= -1e8f )
				continue;
			float sum = 0.0f;
			int cells = 0;
			for( int dr = -stoneSpread; dr <= stoneSpread; ++dr )
				for( int dc = -stoneSpread; dc <= stoneSpread; ++dc )
				{
					const int r = row + dr;
					const int c = col + dc;
					if( r < 0 || c < 0 || r >= static_cast<int>( m_size ) || c >= static_cast<int>( m_size ) )
						continue;
					const size_t around = static_cast<size_t>( r ) * m_size + c;
					if( hydrology.staticWater[around].x <= -1e8f )
						continue;
					sum += stoneArea[around];
					++cells;
				}
			stoneDrag[index] = DirectX::XMFLOAT2( stoneArea[index], cells > 0 ? sum / cells : 0.0f );
		}
	if( !createDetailTextures( hydrology ) )
		return false;

	// Отпечаток входа течения — ключ кэша (solveStaticFlow): итоговый рельеф, растр статичной воды, камни, плитки детальной
	// земли и воды; остальное, что читает расчёт (сопротивление камней, тайлы, блоки), из них выводится
	m_flowInputKey = 0;
	const TerrainHeight height = m_terrain->terrainHeight();
	if( const float* heights = m_terrain->cpuHeights() )
	{
		Fingerprint key;
		key.add( m_size );
		key.add( m_cellSize );
		key.add( height.mapSize );
		key.addWords( heights, static_cast<size_t>( height.mapSize ) * height.mapSize * sizeof( float ) );
		key.addWords( hydrology.staticWater.data(), hydrology.staticWater.size() * sizeof( DirectX::XMFLOAT4 ) );
		for( const TerrainHydrology::Stone& stone : hydrology.stones )
		{
			key.add( stone.position );
			key.add( stone.size );
			key.add( stone.seed );
		}
		for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		{
			key.add( tile.x );
			key.add( tile.z );
			key.addWords( tile.heights.data(), tile.heights.size() * sizeof( float ) );
			key.addWords( tile.water.data(), tile.water.size() * sizeof( DirectX::XMFLOAT4 ) );
		}
		m_flowInputKey = std::max<uint64_t>( key.value, 1 );
	}
	return create( DXGI_FORMAT_R32_FLOAT, hydrology.channelFlow.data(), sizeof( float ), "Water channel flow", m_flowTexture, m_flowView ) &&
		   create( DXGI_FORMAT_R32G32B32A32_FLOAT, hydrology.staticWater.data(), sizeof( DirectX::XMFLOAT4 ), "Water static streams",
				   m_staticWaterTexture, m_staticWaterView ) &&
		   create( DXGI_FORMAT_R32G32_FLOAT, stoneDrag.data(), sizeof( DirectX::XMFLOAT2 ), "Water stone drag", m_stoneDrag, m_stoneDragView );
}

bool WaterSimulation::createDetailTextures( const TerrainHydrology::Result& hydrology )
{
	// Детальная вода — только у статичной: срез — плитка детальной земли (тот же порядок, что у CDLODTerrain)
	m_detailTileCount = 0;
	const uint32_t side = TerrainHydrology::detailTileCells * TerrainHydrology::detailSamples + 2;
	const size_t texels = static_cast<size_t>( side ) * side;
	if( !m_static || hydrology.detailTiles.empty() )
		return true;
	for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		if( tile.water.size() != texels )
		{
			LOG( "Water simulation: detail water differs from the detail tiles in size, no detail water" );
			return true;
		}
	DMD3D& d3d = DMD3D::instance();
	const uint32_t count = static_cast<uint32_t>( hydrology.detailTiles.size() );
	std::vector<TextureData> slices( count );
	for( uint32_t t = 0; t < count; ++t )
	{
		slices[t].data = hydrology.detailTiles[t].water.data();
		slices[t].rowPitch = side * static_cast<uint32_t>( sizeof( DirectX::XMFLOAT4 ) );
		slices[t].slicePitch = slices[t].rowPitch * side;
	}
	TextureDesc desc;
	desc.width = side;
	desc.height = side;
	desc.arraySize = count;
	desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	BufferDesc boundsDesc;
	boundsDesc.size = count * 2 * static_cast<uint32_t>( sizeof( uint32_t ) );
	boundsDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	TextureDesc target = desc;
	target.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	std::vector<DirectX::XMUINT2>& coords = m_detailCoords;
	coords.clear();
	for( const TerrainHydrology::DetailTile& tile : hydrology.detailTiles )
		coords.emplace_back( tile.x, tile.z );
	BufferDesc coordsDesc;
	coordsDesc.size = count * static_cast<uint32_t>( sizeof( DirectX::XMUINT2 ) );
	coordsDesc.stride = sizeof( DirectX::XMUINT2 );
	coordsDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
	if( !d3d.createBuffer( coordsDesc, coords.data(), m_detailTileCoords ) || !d3d.createShaderView( m_detailTileCoords, {}, m_detailTileCoordsView ) ||
		!d3d.createTexture( desc, slices.data(), m_detailStatic ) || !d3d.createShaderView( m_detailStatic, arrayView, m_detailStaticView ) ||
		!d3d.createTexture( target, nullptr, m_detailWater ) || !d3d.createStorageView( m_detailWater, arrayView, m_detailWaterUAV ) ||
		!d3d.createShaderView( m_detailWater, arrayView, m_detailWaterView ) ||
		!d3d.createBuffer( boundsDesc, nullptr, m_detailBounds ) || !d3d.createStorageView( m_detailBounds, rawView, m_detailBoundsUAV ) ||
		!d3d.createShaderView( m_detailBounds, rawView, m_detailBoundsView ) )
	{
		LOG( "Failed to create water detail tiles" );
		return false;
	}
	d3d.setName( m_detailStatic, "Water detail static" );
	d3d.setName( m_detailWater, "Water detail tiles" );
	d3d.setName( m_detailBounds, "Water detail tile bounds" );

	// Течение детальной сетки (solveDetailFlow): блоки 8 × 8 ячеек, где в блоке или в текселе вокруг есть вода (сухой берег
	// у воды — тоже в счёте), плитка мира → срез, камни русла
	const uint32_t cells = side - 2;
	const uint32_t blocksPerSide = cells / flowTile;
	m_detailBlockList.clear();
	for( uint32_t t = 0; t < count; ++t )
	{
		const TerrainHydrology::DetailTile& tile = hydrology.detailTiles[t];
		for( uint32_t by = 0; by < blocksPerSide; ++by )
			for( uint32_t bx = 0; bx < blocksPerSide; ++bx )
			{
				bool wet = false;
				// Блок — тексели bx · 8 + 1 … bx · 8 + 8 (поле — тексель 0), с текселем вокруг
				for( uint32_t j = by * flowTile; j <= by * flowTile + flowTile + 1 && !wet; ++j )
					for( uint32_t i = bx * flowTile; i <= bx * flowTile + flowTile + 1 && !wet; ++i )
					{
						const size_t index = static_cast<size_t>( j ) * side + i;
						wet = tile.water[index].x > -1e8f && tile.water[index].x > tile.heights[index];
					}
				if( wet )
					m_detailBlockList.emplace_back( t, by * blocksPerSide + bx );
			}
	}
	m_detailTilesPerSide = static_cast<uint32_t>( std::lround( static_cast<float>( m_size ) / TerrainHydrology::detailTileCells ) );
	m_detailSliceOf.assign( static_cast<size_t>( m_detailTilesPerSide ) * m_detailTilesPerSide, -1 );
	for( uint32_t t = 0; t < count; ++t )
		if( coords[t].x < m_detailTilesPerSide && coords[t].y < m_detailTilesPerSide )
			m_detailSliceOf[static_cast<size_t>( coords[t].y ) * m_detailTilesPerSide + coords[t].x] = static_cast<int32_t>( t );
	m_detailStones = hydrology.stones;
	if( !m_detailBlockList.empty() )
	{
		BufferDesc blocksDesc;
		blocksDesc.size = static_cast<uint32_t>( m_detailBlockList.size() * sizeof( DirectX::XMUINT2 ) );
		blocksDesc.stride = sizeof( DirectX::XMUINT2 );
		blocksDesc.usage = BufferUsage::shaderResource | BufferUsage::structured;
		if( !d3d.createBuffer( blocksDesc, m_detailBlockList.data(), m_detailBlocks ) ||
			!d3d.createShaderView( m_detailBlocks, {}, m_detailBlocksView ) )
		{
			LOG( "Failed to create water detail flow blocks" );
			return false;
		}
		d3d.setName( m_detailBlocks, "Water detail flow blocks" );
	}
	// Без начальных данных память не обнулена: первая операция — очистка
	d3d.clearStorageView( m_detailWaterUAV );
	m_detailTileCount = count;
	m_detailSide = side;
	m_detailTileSize = TerrainHydrology::detailTileCells * m_cellSize;
	LOG( "Water detail tiles " + std::to_string( count ) + " (" + std::to_string( side ) + "x" + std::to_string( side ) + ", " +
		 std::to_string( 2 * texels * count * sizeof( DirectX::XMFLOAT4 ) / 1024 / 1024 ) + " MB)" );
	return true;
}

bool WaterSimulation::buildStaticWater( const Settings& settings )
{
	const ShaderView& staticWater = m_staticWaterView;
	DMD3D& d3d = DMD3D::instance();
	setParameters( settings );
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water static";
	pass.reads = { { &heightMap, "height map" }, { &staticWater, "static water" } };
	pass.writes = { { &m_waterUAV, "water depth" }, { &m_outputUAV, "water state" } };
	d3d.beginPass( pass );
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 3, staticWater );
	d3d.setUAV( 0, m_waterUAV );
	d3d.setUAV( 2, m_outputUAV );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	m_staticShader.dispatchGroups( groups, groups, 1 );
	return true;
}

bool WaterSimulation::createFlowResources()
{
	DMD3D& d3d = DMD3D::instance();
	// Воспроизводимо (-O3 и строгий IEEE в Debug и Release, ShaderCompiler::compile): течение одно в обеих конфигурациях, а
	// неоптимизированный байткод переноса и вязкости детальной сетки под GPU-based validation при первом вызове
	// обрабатывался минутами (~170 с каждый)
	const char* file = "Shaders\\water_flow.cs";
	const bool same = true;
	if( !m_flowInitShader.Initialize( file, "mainFlowInit", same ) || !m_flowFacesShader.Initialize( file, "mainFlowFaces", same ) ||
		!m_flowAdvectShader.Initialize( file, "mainFlowAdvect", same ) ||
		!m_flowViscosityShader.Initialize( file, "mainFlowViscosity", same ) ||
		!m_flowDivergenceShader.Initialize( file, "mainFlowDivergence", same ) ||
		!m_flowRedShader.Initialize( file, "mainFlowRed", same ) || !m_flowBlackShader.Initialize( file, "mainFlowBlack", same ) ||
		!m_flowProjectShader.Initialize( file, "mainFlowProject", same ) ||
		!m_flowOutputShader.Initialize( file, "mainFlowOutput", same ) || !m_flowRestoreShader.Initialize( file, "mainFlowRestore", same ) ||
		!d3d.createShaderConstantBuffer( sizeof( FlowParameters ), m_flowBuffer ) )
		return false;
	// Детальная сетка — тот же шейдер с FLOW_DETAIL
	const char* detail = "FLOW_DETAIL";
	if( !m_detailFlowInitShader.Initialize( file, "mainFlowInit", same, detail ) ||
		!m_detailFlowFacesShader.Initialize( file, "mainFlowFaces", same, detail ) ||
		!m_detailFlowAdvectShader.Initialize( file, "mainFlowAdvect", same, detail ) ||
		!m_detailFlowViscosityShader.Initialize( file, "mainFlowViscosity", same, detail ) ||
		!m_detailFlowNudgeShader.Initialize( file, "mainFlowNudge", same, detail ) ||
		!m_detailFlowDivergenceShader.Initialize( file, "mainFlowDivergence", same, detail ) ||
		!m_detailFlowRedShader.Initialize( file, "mainFlowRed", same, detail ) ||
		!m_detailFlowBlackShader.Initialize( file, "mainFlowBlack", same, detail ) ||
		!m_detailFlowProjectShader.Initialize( file, "mainFlowProject", same, detail ) ||
		!m_detailFlowOutputShader.Initialize( file, "mainFlowOutput", same, detail ) ||
		!m_detailFlowRestoreShader.Initialize( file, "mainFlowRestore", same, detail ) ||
		!d3d.createShaderConstantBuffer( sizeof( DetailFlowParameters ), m_detailFlowBuffer ) )
		return false;
	for( auto [texture, uav, format, name] :
		 { std::tuple{ &m_flowInfo, &m_flowInfoUAV, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water flow cells" },
		   std::tuple{ &m_flowVelocity, &m_flowVelocityUAV, DXGI_FORMAT_R32G32_FLOAT, "Water flow velocity" },
		   std::tuple{ &m_flowAdvected, &m_flowAdvectedUAV, DXGI_FORMAT_R32G32_FLOAT, "Water flow advected" },
		   std::tuple{ &m_flowPressure, &m_flowPressureUAV, DXGI_FORMAT_R32_FLOAT, "Water flow pressure" },
		   std::tuple{ &m_flowDivergence, &m_flowDivergenceUAV, DXGI_FORMAT_R32_FLOAT, "Water flow divergence" } } )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = format;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create water flow texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
	}
	return true;
}

std::string WaterSimulation::solveFlow( const Settings::Flow& flow )
{
	m_flowBuilt = flow;
	if( !m_static || m_flowTileList.empty() )
		return {};
	const auto start = std::chrono::high_resolution_clock::now();
	DMD3D& d3d = DMD3D::instance();
	const float timeStep = std::max( flow.timeStep, 0.01f );
	uint32_t steps = flow.settleTime > 0.0f ? static_cast<uint32_t>( std::ceil( flow.settleTime / timeStep ) ) : 0;
	if( d3d.gpuValidation() && steps > validationFlowSteps )
	{
		LOG( "Water flow: " + std::to_string( steps ) + " steps are cut to " + std::to_string( validationFlowSteps ) +
			 " under GPU-based validation" );
		steps = validationFlowSteps;
	}
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( flow.iterations, 1, 200 ) );

	FlowParameters params = {};
	params.tileCount = static_cast<uint32_t>( m_flowTileList.size() );
	params.timeStep = timeStep;
	params.viscosity = std::min( flow.viscosity * timeStep / ( m_cellSize * m_cellSize ), maxViscosityShare );
	params.friction = timeStep * gravity * flow.manning * flow.manning;
	params.minDepth = flowMinDepth;
	params.layer = std::max( flow.layer, flowMinDepth );
	params.screen = flowScreen;
	params.relaxation = flowRelaxation;
	params.stoneDrag = timeStep * 0.5f * stoneDragCoefficient;

	// Тайлы прошлого расчёта (другие русла) — сухие: соседи за краем тайлов читаются нулями
	for( StorageView* uav : { &m_flowInfoUAV, &m_flowVelocityUAV, &m_flowAdvectedUAV, &m_flowPressureUAV, &m_flowDivergenceUAV } )
		d3d.clearStorageView( *uav );
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	auto begin = [&]()
	{
		PassDesc pass;
		pass.name = "Water flow";
		pass.reads = { { &heightMap, "height map" }, { &m_staticWaterView, "static water" }, { &m_flowTilesView, "flow tiles" },
					   { &m_stoneDragView, "stone drag" } };
		pass.writes = { { &m_flowInfoUAV, "flow cells" },	   { &m_flowVelocityUAV, "flow velocity" },
						{ &m_flowAdvectedUAV, "flow advected" }, { &m_flowPressureUAV, "flow pressure" },
						{ &m_flowDivergenceUAV, "flow divergence" }, { &m_outputUAV, "water state" } };
		d3d.beginPass( pass );
		setParameters( m_initial );
		Device::updateResourceData( m_flowBuffer, params );
		d3d.setConstantBuffer( 5, m_flowBuffer );
		d3d.setSRV( 0, heightMap );
		d3d.setSRV( 3, m_staticWaterView );
		d3d.setSRV( 5, m_flowTilesView );
		d3d.setSRV( 6, m_stoneDragView );
	};
	// Привязка UAV после dispatch ставит барьер UAV → UAV: следующий проход видит записанное предыдущим
	const uint32_t groups = static_cast<uint32_t>( m_flowTileList.size() );
	auto dispatch = [&]( DMComputeShader& shader )
	{
		d3d.setUAV( 0, m_flowInfoUAV );
		d3d.setUAV( 1, m_flowVelocityUAV );
		d3d.setUAV( 2, m_flowAdvectedUAV );
		d3d.setUAV( 3, m_flowPressureUAV );
		d3d.setUAV( 4, m_flowDivergenceUAV );
		d3d.setUAV( 5, m_outputUAV );
		shader.dispatchGroups( groups, 1, 1 );
	};
	// Порция шагов — шаг прогресса загрузки: после ожидания GPU проход и привязки ставит begin. Под GPU-based validation
	// шаг — ~0,5 с, порции мельче: экран загрузки обновляется, окно не «зависает»
	const uint32_t stepsPerSubmit = d3d.gpuValidation() ? 2 : flowStepsPerSubmit;
	SlowTask task( static_cast<float>( std::max( steps, 1u ) ) );
	begin();
	dispatch( m_flowInitShader );
	dispatch( m_flowFacesShader );
	for( uint32_t step = 0; step < steps; ++step )
	{
		if( step > 0 && step % stepsPerSubmit == 0 )
		{
			d3d.waitForGpu();
			task.enterProgressFrame( static_cast<float>( stepsPerSubmit ) );
			begin();
		}
		dispatch( m_flowAdvectShader );
		dispatch( m_flowViscosityShader );
		dispatch( m_flowDivergenceShader );
		for( uint32_t i = 0; i < iterations; ++i )
		{
			dispatch( m_flowRedShader );
			dispatch( m_flowBlackShader );
		}
		dispatch( m_flowProjectShader );
	}
	dispatch( m_flowOutputShader );
	d3d.waitForGpu();

	// Сводка: ячеек озёр и ручьёв, средняя и наибольшая скорость — установилось ли течение (сравнить с расчётом подольше);
	// у ручьёв — насколько поле отошло от скорости по Маннингу u₀ (у слияний, изгибов, впадений)
	std::vector<DMD3D::SubresourceCopy> infoCopies, velocityCopies;
	std::vector<uint8_t> infoBytes, velocityBytes;
	double lakeSum = 0.0, streamSum = 0.0, baseSum = 0.0, deviationSum = 0.0;
	float lakePeak = 0.0f, streamPeak = 0.0f;
	uint32_t lakeCells = 0, streamCells = 0;
	if( d3d.captureTexture( m_flowInfo, infoCopies, infoBytes ) && d3d.captureTexture( m_flowVelocity, velocityCopies, velocityBytes ) &&
		!infoCopies.empty() && !velocityCopies.empty() )
	{
		for( uint32_t row = 0; row < m_size; ++row )
		{
			const float* info = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[0].offset + row * infoCopies[0].rowPitch );
			const float* velocity =
				reinterpret_cast<const float*>( velocityBytes.data() + velocityCopies[0].offset + row * velocityCopies[0].rowPitch );
			const float* above = row > 0 ? reinterpret_cast<const float*>( velocityBytes.data() + velocityCopies[0].offset +
																		   ( row - 1 ) * velocityCopies[0].rowPitch ) :
										   nullptr;
			for( uint32_t col = 0; col < m_size; ++col )
			{
				const float kind = info[col * 4 + 2];
				if( kind != 1.0f && kind != 2.0f )
					continue;
				// Скорость ячейки — средняя её открытых граней (скорость хранится на восточной и южной грани; грань открыта,
				// если сосед мокрый — как mainFlowOutput)
				auto wet = [&]( int64_t r, int64_t c )
				{
					if( r < 0 || c < 0 || r >= m_size || c >= m_size )
						return false;
					const float* line = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[0].offset + r * infoCopies[0].rowPitch );
					return line[c * 4 + 2] != 0.0f;
				};
				auto average = [&]( bool first, float a, bool second, float b )
				{
					const int open = ( first ? 1 : 0 ) + ( second ? 1 : 0 );
					return open ? ( ( first ? a : 0.0f ) + ( second ? b : 0.0f ) ) / open : 0.0f;
				};
				const float x = average( wet( row, col + 1 ), velocity[col * 2], wet( row, static_cast<int64_t>( col ) - 1 ), col > 0 ? velocity[col * 2 - 2] : 0.0f );
				const float y = average( wet( row + 1, col ), velocity[col * 2 + 1], wet( static_cast<int64_t>( row ) - 1, col ), above ? above[col * 2 + 1] : 0.0f );
				const float speed = std::hypot( x, y );
				if( kind == 1.0f )
				{
					lakeSum += speed;
					lakePeak = std::max( lakePeak, speed );
					++lakeCells;
				}
				else if( info[col * 4 + 3] >= flowMinDepth )
				{
					streamSum += speed;
					streamPeak = std::max( streamPeak, speed );
					baseSum += std::hypot( info[col * 4], info[col * 4 + 1] );
					deviationSum += std::hypot( x - info[col * 4], y - info[col * 4 + 1] );
					++streamCells;
				}
			}
		}
	}
	char text[320];
	snprintf( text, sizeof( text ),
			  "Water flow: %u steps x %u iterations over %zu tiles 8x8; lake cells %u, mean speed %.3f m/s, max %.3f m/s; stream cells %u, "
			  "mean speed %.3f m/s (Manning %.3f), max %.3f m/s, off Manning %.0f%%, ms: %.1f",
			  steps, iterations, m_flowTileList.size(), lakeCells, lakeCells ? lakeSum / lakeCells : 0.0, lakePeak, streamCells,
			  streamCells ? streamSum / streamCells : 0.0, streamCells ? baseSum / streamCells : 0.0, streamPeak,
			  baseSum > 0.0 ? 100.0 * deviationSum / baseSum : 0.0,
			  std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() );
	LOG( text );
	return text;
}

std::string WaterSimulation::solveDetailFlow( const Settings::Flow& flow )
{
	if( !m_static || m_detailTileCount == 0 || m_detailBlockList.empty() || flow.detailSettleTime <= 0.0f )
		return {};
	const TerrainHeight height = m_terrain->terrainHeight();
	if( !height.detailTiles || !height.detailIndex )
		return {};
	const auto start = std::chrono::high_resolution_clock::now();
	DMD3D& d3d = DMD3D::instance();
	const uint32_t side = m_detailSide;
	const uint32_t cells = side - 2;
	const float step = m_detailTileSize / cells;
	const float timeStep = std::max( flow.timeStep, 0.01f ) * detailTimeStepShare;
	uint32_t steps = static_cast<uint32_t>( std::ceil( flow.detailSettleTime / timeStep ) );
	uint32_t blocks = static_cast<uint32_t>( m_detailBlockList.size() );
	if( d3d.gpuValidation() )
	{
		LOG( "Water detail flow: " + std::to_string( steps ) + " steps over " + std::to_string( blocks ) + " blocks are cut to " +
			 std::to_string( std::min( steps, validationDetailFlowSteps ) ) + " over " +
			 std::to_string( std::min( blocks, validationDetailFlowBlocks ) ) + " under GPU-based validation" );
		steps = std::min( steps, validationDetailFlowSteps );
		blocks = std::min( blocks, validationDetailFlowBlocks );
	}
	const uint32_t iterations = static_cast<uint32_t>( std::clamp( flow.iterations, 1, 200 ) );

	// Камни русла — верх над ячейками плиток (и полем соседних): дно поднято до него (water_flow.cs: не больше чем на
	// половину глубины).
	// Камень — полуэллипсоид: поперечник size, высота над дном stoneHeight · size (как у сопротивления сетки 1 м)
	const size_t texels = static_cast<size_t>( side ) * side;
	std::vector<float> stones( texels * m_detailTileCount, -1e9f );
	const int tilesPerSide = static_cast<int>( m_detailTilesPerSide );
	const int tileCells = static_cast<int>( cells );
	for( const TerrainHydrology::Stone& stone : m_detailStones )
	{
		const float radius = 0.5f * stone.size;
		const float rise = stoneHeight * stone.size;
		if( radius <= 0.0f )
			continue;
		const int x0 = static_cast<int>( std::floor( ( stone.position.x - radius ) / step ) );
		const int x1 = static_cast<int>( std::floor( ( stone.position.x + radius ) / step ) );
		const int z0 = static_cast<int>( std::floor( ( stone.position.z - radius ) / step ) );
		const int z1 = static_cast<int>( std::floor( ( stone.position.z + radius ) / step ) );
		for( int z = std::max( z0, 0 ); z <= z1; ++z )
			for( int x = std::max( x0, 0 ); x <= x1; ++x )
			{
				const float dx = ( x + 0.5f ) * step - stone.position.x;
				const float dz = ( z + 0.5f ) * step - stone.position.z;
				const float share = ( dx * dx + dz * dz ) / ( radius * radius );
				if( share >= 1.0f )
					continue;
				const float top = stone.position.y + rise * std::sqrt( 1.0f - share );
				// Своя плитка и поле соседних
				const int tx = x / tileCells;
				const int tz = z / tileCells;
				for( int k = 0; k < 9; ++k )
				{
					const int nx = tx + k % 3 - 1;
					const int nz = tz + k / 3 - 1;
					if( nx < 0 || nz < 0 || nx >= tilesPerSide || nz >= tilesPerSide )
						continue;
					const int slice = m_detailSliceOf[static_cast<size_t>( nz ) * tilesPerSide + nx];
					const int lx = x - nx * tileCells;
					const int lz = z - nz * tileCells;
					if( slice < 0 || lx < -1 || lz < -1 || lx > tileCells || lz > tileCells )
						continue;
					float& texel = stones[static_cast<size_t>( slice ) * texels + static_cast<size_t>( lz + 1 ) * side + lx + 1];
					texel = std::max( texel, top );
				}
			}
	}

	// Массивы счёта — на время расчёта: срез на плитку, тексели — как у детальной воды (с полем)
	TextureDesc desc;
	desc.width = side;
	desc.height = side;
	desc.arraySize = m_detailTileCount;
	desc.usage = TextureUsage::unorderedAccess;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	Texture info, info2, velocity, advected, pressure, divergence, stoneTops;
	StorageView infoUAV, info2UAV, velocityUAV, advectedUAV, pressureUAV, divergenceUAV;
	ShaderView stonesView;
	for( auto [texture, uav, format, name] :
		 { std::tuple{ &info, &infoUAV, DXGI_FORMAT_R32G32B32A32_FLOAT, "Water detail flow cells" },
		   std::tuple{ &info2, &info2UAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow radius" },
		   std::tuple{ &velocity, &velocityUAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow velocity" },
		   std::tuple{ &advected, &advectedUAV, DXGI_FORMAT_R32G32_FLOAT, "Water detail flow advected" },
		   std::tuple{ &pressure, &pressureUAV, DXGI_FORMAT_R32_FLOAT, "Water detail flow pressure" },
		   std::tuple{ &divergence, &divergenceUAV, DXGI_FORMAT_R32_FLOAT, "Water detail flow divergence" } } )
	{
		desc.format = format;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, arrayView, *uav ) )
		{
			LOG( std::string( "Failed to create water detail flow texture " ) + name );
			return {};
		}
		d3d.setName( *texture, name );
	}
	std::vector<TextureData> slices( m_detailTileCount );
	for( uint32_t t = 0; t < m_detailTileCount; ++t )
	{
		slices[t].data = stones.data() + t * texels;
		slices[t].rowPitch = side * static_cast<uint32_t>( sizeof( float ) );
		slices[t].slicePitch = slices[t].rowPitch * side;
	}
	TextureDesc stonesDesc = desc;
	stonesDesc.format = DXGI_FORMAT_R32_FLOAT;
	stonesDesc.usage = TextureUsage::shaderResource;
	if( !d3d.createTexture( stonesDesc, slices.data(), stoneTops ) || !d3d.createShaderView( stoneTops, arrayView, stonesView ) )
	{
		LOG( "Failed to create water detail stones" );
		return {};
	}
	d3d.setName( stoneTops, "Water detail stones" );

	FlowParameters params = {};
	params.tileCount = blocks;
	params.timeStep = timeStep;
	params.viscosity = std::min( flow.viscosity * timeStep / ( step * step ), maxViscosityShare );
	params.friction = timeStep * gravity * flow.manning * flow.manning;
	params.minDepth = flowMinDepth;
	params.layer = std::max( flow.layer, flowMinDepth );
	params.screen = flowScreen;
	params.relaxation = flowRelaxation;
	DetailFlowParameters detailParams = {};
	detailParams.cells = cells;
	detailParams.tilesPerSide = m_detailTilesPerSide;
	detailParams.step = step;
	detailParams.wetDepth = flowMinDepth;

	// Блоки прошлого расчёта и ячейки вне блоков — сухие
	for( StorageView* uav : { &infoUAV, &info2UAV, &velocityUAV, &advectedUAV, &pressureUAV, &divergenceUAV } )
		d3d.clearStorageView( *uav );
	auto begin = [&]()
	{
		PassDesc pass;
		pass.name = "Water detail flow";
		pass.reads = { { &m_detailStaticView, "detail static water" }, { height.detailTiles, "terrain detail tiles" },
					   { height.detailIndex, "terrain detail index" }, { &m_detailTileCoordsView, "detail tile coordinates" },
					   { &m_detailBlocksView, "detail flow blocks" },	{ &stonesView, "detail stones" },
					   { &m_outputView, "water state" } };
		pass.writes = { { &infoUAV, "detail flow cells" },		   { &info2UAV, "detail flow radius" },
						{ &velocityUAV, "detail flow velocity" },	   { &advectedUAV, "detail flow advected" },
						{ &pressureUAV, "detail flow pressure" },	   { &divergenceUAV, "detail flow divergence" },
						{ &m_detailWaterUAV, "detail water" } };
		d3d.beginPass( pass );
		setParameters( m_initial );
		Device::updateResourceData( m_flowBuffer, params );
		d3d.setConstantBuffer( 5, m_flowBuffer );
		Device::updateResourceData( m_detailFlowBuffer, detailParams );
		d3d.setConstantBuffer( 6, m_detailFlowBuffer );
		d3d.setSRV( 3, m_detailStaticView );
		d3d.setSRV( 5, m_detailBlocksView );
		d3d.setSRV( 6, *height.detailTiles );
		d3d.setSRV( 7, *height.detailIndex );
		d3d.setSRV( 8, m_detailTileCoordsView );
		d3d.setSRV( 9, stonesView );
		d3d.setSRV( 10, m_outputView );
	};
	const uint32_t groups = blocks;
	auto dispatch = [&]( DMComputeShader& shader )
	{
		d3d.setUAV( 0, infoUAV );
		d3d.setUAV( 1, velocityUAV );
		d3d.setUAV( 2, advectedUAV );
		d3d.setUAV( 3, pressureUAV );
		d3d.setUAV( 4, divergenceUAV );
		d3d.setUAV( 5, m_detailWaterUAV );
		d3d.setUAV( 6, info2UAV );
		shader.dispatchGroups( groups, 1, 1 );
	};
	const uint32_t stepsPerSubmit = d3d.gpuValidation() ? 2 : flowStepsPerSubmit;
	SlowTask task( static_cast<float>( std::max( steps, 1u ) ) );
	begin();
	dispatch( m_detailFlowInitShader );
	dispatch( m_detailFlowFacesShader );
	for( uint32_t done = 0; done < steps; ++done )
	{
		if( done > 0 && done % stepsPerSubmit == 0 )
		{
			d3d.waitForGpu();
			task.enterProgressFrame( static_cast<float>( stepsPerSubmit ) );
			begin();
		}
		dispatch( m_detailFlowAdvectShader );
		dispatch( m_detailFlowViscosityShader );
		dispatch( m_detailFlowNudgeShader );
		dispatch( m_detailFlowDivergenceShader );
		for( uint32_t i = 0; i < iterations; ++i )
		{
			dispatch( m_detailFlowRedShader );
			dispatch( m_detailFlowBlackShader );
		}
		dispatch( m_detailFlowProjectShader );
	}
	dispatch( m_detailFlowOutputShader );
	d3d.waitForGpu();
	const double solveMs = std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count();

	// Сводка по ячейкам плиток (без поля): ручьи — средняя по глубине против сетки 1 м в тех же точках (билинейно по мокрым
	// клеткам, как берёт её детальный расчёт) и против u₀, озёра
	std::vector<DMD3D::SubresourceCopy> infoCopies, waterCopies, coarseCopies;
	std::vector<uint8_t> infoBytes, waterBytes, coarseBytes;
	double streamSum = 0.0, coarseSum = 0.0, baseSum = 0.0, streamDepth = 0.0, lakeSum = 0.0;
	float peak = 0.0f;
	uint32_t streamCells = 0, lakeCells = 0;
	if( d3d.captureTexture( info, infoCopies, infoBytes ) && d3d.captureTexture( m_detailWater, waterCopies, waterBytes ) &&
		d3d.captureTexture( m_output, coarseCopies, coarseBytes ) && infoCopies.size() >= m_detailTileCount &&
		waterCopies.size() >= m_detailTileCount && !coarseCopies.empty() )
	{
		// m_output — R16G16B16A16_FLOAT: глубина, течение X, Z мира
		auto coarse = [&]( int col, int row, int channel )
		{
			const uint16_t* line = reinterpret_cast<const uint16_t*>( coarseBytes.data() + coarseCopies[0].offset + row * coarseCopies[0].rowPitch );
			return DirectX::PackedVector::XMConvertHalfToFloat( line[col * 4 + channel] );
		};
		const float worldSize = m_size * m_cellSize;
		auto coarseSpeed = [&]( float x, float z )
		{
			const float gx = x / m_cellSize - 0.5f;
			const float gy = ( worldSize - z ) / m_cellSize - 0.5f;
			const int c0 = static_cast<int>( std::floor( gx ) );
			const int r0 = static_cast<int>( std::floor( gy ) );
			float vx = 0.0f, vz = 0.0f, weights = 0.0f;
			for( int k = 0; k < 4; ++k )
			{
				const int c = c0 + ( k & 1 );
				const int r = r0 + ( k >> 1 );
				const float weight = ( k & 1 ? gx - c0 : 1.0f - ( gx - c0 ) ) * ( k >> 1 ? gy - r0 : 1.0f - ( gy - r0 ) );
				if( weight <= 0.0f || c < 0 || r < 0 || c >= static_cast<int>( m_size ) || r >= static_cast<int>( m_size ) ||
					coarse( c, r, 0 ) < flowMinDepth )
					continue;
				vx += weight * coarse( c, r, 1 );
				vz += weight * coarse( c, r, 2 );
				weights += weight;
			}
			return weights > 1e-6f ? std::hypot( vx, vz ) / weights : 0.0f;
		};
		for( uint32_t t = 0; t < m_detailTileCount; ++t )
			for( uint32_t j = 1; j <= cells; ++j )
			{
				const float* cell = reinterpret_cast<const float*>( infoBytes.data() + infoCopies[t].offset + j * infoCopies[t].rowPitch );
				const float* water = reinterpret_cast<const float*>( waterBytes.data() + waterCopies[t].offset + j * waterCopies[t].rowPitch );
				for( uint32_t i = 1; i <= cells; ++i )
				{
					const float kind = cell[i * 4 + 2];
					const float speed = std::hypot( water[i * 4 + 1], water[i * 4 + 2] );
					if( kind == 2.0f )
					{
						// По глубине: средняя по расходу (у берега мелко и медленно — средняя по ячейкам ниже)
						const float depth = cell[i * 4 + 3];
						const float x = ( m_detailCoords[t].x * cells + i - 0.5f ) * step;
						const float z = ( m_detailCoords[t].y * cells + j - 0.5f ) * step;
						streamSum += depth * speed;
						coarseSum += depth * coarseSpeed( x, z );
						baseSum += depth * std::hypot( cell[i * 4], cell[i * 4 + 1] );
						streamDepth += depth;
						++streamCells;
					}
					else if( kind == 1.0f )
					{
						lakeSum += speed;
						++lakeCells;
					}
					if( kind == 1.0f || kind == 2.0f )
						peak = std::max( peak, speed );
				}
			}
	}
	char text[400];
	snprintf( text, sizeof( text ),
			  "Water detail flow: %u steps x %u iterations over %zu blocks 8x8 (%.2f m); stream cells %u, mean speed by depth %.3f m/s "
			  "(1 m grid %.3f, Manning %.3f); lake cells %u, mean speed %.3f m/s; max %.3f m/s; ms: %.1f (solve %.1f)",
			  steps, iterations, static_cast<size_t>( blocks ), step, streamCells, streamDepth > 0.0 ? streamSum / streamDepth : 0.0,
			  streamDepth > 0.0 ? coarseSum / streamDepth : 0.0, streamDepth > 0.0 ? baseSum / streamDepth : 0.0, lakeCells,
			  lakeCells ? lakeSum / lakeCells : 0.0, peak,
			  std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count(), solveMs );
	LOG( text );
	return text;
}

std::string WaterSimulation::flowCachePath( const Settings::Flow& flow ) const
{
	if( m_flowInputKey == 0 )
		return {};
	Fingerprint key;
	key.add( flowCacheVersion );
	key.add( m_flowInputKey );
	key.add( flow.settleTime );
	key.add( flow.timeStep );
	key.add( flow.viscosity );
	key.add( flow.manning );
	key.add( flow.iterations );
	key.add( flow.layer );
	key.add( flow.detailSettleTime );
	const TerrainHeight height = m_terrain->terrainHeight();
	key.add( height.heightMultiplier );
	key.add( height.heightOffset );
	// Код расчёта — шейдер течения и его include: правка любого — расчёт заново
	for( const char* file : { "Shaders\\water_flow.cs", "Shaders\\water_simulation.sh", "Shaders\\bindless.sh", "Shaders\\slots.h" } )
	{
		std::ifstream in( file, std::ios::binary );
		const std::string text( ( std::istreambuf_iterator<char>( in ) ), std::istreambuf_iterator<char>() );
		key.add( text.data(), text.size() );
	}
	char name[64];
	snprintf( name, sizeof( name ), "\\flow_%016llx.bin", static_cast<unsigned long long>( key.value ) );
	return std::string( flowCacheDirectory ) + name;
}

namespace
{

// Файл кэша течения: заголовок, течение сетки 1 м (X, Z мира — half, как в текстуре воды), детальной воды (X, Z — float,
// у всех текселей плиток с полем; нет — если течения детальной сетки не было), сводки расчётов
struct FlowCacheHeader
{
	char magic[4] = { 'D', 'M', 'W', 'F' };
	uint32_t version = flowCacheVersion;
	uint32_t size = 0;
	uint32_t detailSide = 0;
	uint32_t detailSlices = 0;
	uint32_t coarseSummary = 0;		// длина строки сводки, байт
	uint32_t detailSummary = 0;
	uint32_t padding = 0;
};

}

bool WaterSimulation::loadFlowCache( const std::string& path )
{
	std::ifstream in( path, std::ios::binary );
	FlowCacheHeader header;
	if( !in.read( reinterpret_cast<char*>( &header ), sizeof( header ) ) || std::memcmp( header.magic, "DMWF", 4 ) != 0 ||
		header.version != flowCacheVersion || header.size != m_size ||
		( header.detailSlices != 0 && ( header.detailSlices != m_detailTileCount || header.detailSide != m_detailSide ) ) )
		return false;
	std::vector<uint16_t> coarse( static_cast<size_t>( m_size ) * m_size * 2 );
	std::vector<float> detail( static_cast<size_t>( header.detailSide ) * header.detailSide * header.detailSlices * 2 );
	std::string coarseSummary( header.coarseSummary, ' ' ), detailSummary( header.detailSummary, ' ' );
	if( !in.read( reinterpret_cast<char*>( coarse.data() ), coarse.size() * sizeof( uint16_t ) ) ||
		!in.read( reinterpret_cast<char*>( detail.data() ), detail.size() * sizeof( float ) ) ||
		!in.read( coarseSummary.data(), coarseSummary.size() ) || !in.read( detailSummary.data(), detailSummary.size() ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	Texture coarseTexture, detailTexture;
	ShaderView coarseView, detailView;
	TextureDesc desc;
	desc.width = m_size;
	desc.height = m_size;
	desc.format = DXGI_FORMAT_R16G16_FLOAT;
	TextureData coarseData;
	coarseData.data = coarse.data();
	coarseData.rowPitch = m_size * 2 * static_cast<uint32_t>( sizeof( uint16_t ) );
	coarseData.slicePitch = coarseData.rowPitch * m_size;
	if( !d3d.createTexture( desc, &coarseData, coarseTexture ) || !d3d.createShaderView( coarseTexture, {}, coarseView ) )
		return false;
	TextureViewDesc arrayView;
	arrayView.kind = TextureViewDesc::Kind::texture2DArray;
	if( header.detailSlices != 0 )
	{
		std::vector<TextureData> slices( header.detailSlices );
		const size_t sliceFloats = static_cast<size_t>( header.detailSide ) * header.detailSide * 2;
		for( uint32_t t = 0; t < header.detailSlices; ++t )
		{
			slices[t].data = detail.data() + t * sliceFloats;
			slices[t].rowPitch = header.detailSide * 2 * static_cast<uint32_t>( sizeof( float ) );
			slices[t].slicePitch = slices[t].rowPitch * header.detailSide;
		}
		TextureDesc detailDesc;
		detailDesc.width = header.detailSide;
		detailDesc.height = header.detailSide;
		detailDesc.arraySize = header.detailSlices;
		detailDesc.format = DXGI_FORMAT_R32G32_FLOAT;
		if( !d3d.createTexture( detailDesc, slices.data(), detailTexture ) || !d3d.createShaderView( detailTexture, arrayView, detailView ) )
			return false;
	}

	// Сетка 1 м — поверх mainStatic, затем поверхность (детальная берёт течение 1 м) и детальная вода поверх неё
	PassDesc pass;
	pass.name = "Water flow from cache";
	pass.reads = { { &coarseView, "cached flow" } };
	pass.writes = { { &m_outputUAV, "water state" } };
	d3d.beginPass( pass );
	setParameters( m_initial );
	d3d.setSRV( 11, coarseView );
	d3d.setUAV( 5, m_outputUAV );
	const uint32_t groups = ( m_size + 7 ) / 8;
	m_flowRestoreShader.dispatchGroups( groups, groups, 1 );
	buildSurface();
	m_surfaceDirty = false;
	if( header.detailSlices != 0 )
	{
		PassDesc detailPass;
		detailPass.name = "Water detail flow from cache";
		detailPass.reads = { { &detailView, "cached detail flow" } };
		detailPass.writes = { { &m_detailWaterUAV, "detail water" } };
		d3d.beginPass( detailPass );
		d3d.setSRV( 11, detailView );
		d3d.setUAV( 5, m_detailWaterUAV );
		const uint32_t detailGroups = ( header.detailSide + 7 ) / 8;
		m_detailFlowRestoreShader.dispatchGroups( detailGroups, detailGroups, header.detailSlices );
	}
	// Временные текстуры освобождаются при выходе: GPU должен закончить с ними
	d3d.waitForGpu();
	if( !coarseSummary.empty() )
		LOG( "  cached: " + coarseSummary );
	if( !detailSummary.empty() )
		LOG( "  cached: " + detailSummary );
	return true;
}

void WaterSimulation::saveFlowCache( const std::string& path, const std::string& coarseSummary, const std::string& detailSummary )
{
	DMD3D& d3d = DMD3D::instance();
	std::vector<DMD3D::SubresourceCopy> coarseCopies, detailCopies;
	std::vector<uint8_t> coarseBytes, detailBytes;
	if( !d3d.captureTexture( m_output, coarseCopies, coarseBytes ) || coarseCopies.empty() )
	{
		LOG( "Water flow: cache not written, readback failed" );
		return;
	}
	FlowCacheHeader header;
	header.size = m_size;
	header.coarseSummary = static_cast<uint32_t>( coarseSummary.size() );
	header.detailSummary = static_cast<uint32_t>( detailSummary.size() );
	// m_output — R16G16B16A16_FLOAT: течение — каналы 1, 2
	std::vector<uint16_t> coarse( static_cast<size_t>( m_size ) * m_size * 2 );
	for( uint32_t row = 0; row < m_size; ++row )
	{
		const uint16_t* line = reinterpret_cast<const uint16_t*>( coarseBytes.data() + coarseCopies[0].offset + row * coarseCopies[0].rowPitch );
		for( uint32_t col = 0; col < m_size; ++col )
		{
			coarse[( static_cast<size_t>( row ) * m_size + col ) * 2] = line[col * 4 + 1];
			coarse[( static_cast<size_t>( row ) * m_size + col ) * 2 + 1] = line[col * 4 + 2];
		}
	}
	std::vector<float> detail;
	if( !detailSummary.empty() && d3d.captureTexture( m_detailWater, detailCopies, detailBytes ) && detailCopies.size() >= m_detailTileCount )
	{
		header.detailSide = m_detailSide;
		header.detailSlices = m_detailTileCount;
		detail.resize( static_cast<size_t>( m_detailSide ) * m_detailSide * m_detailTileCount * 2 );
		for( uint32_t t = 0; t < m_detailTileCount; ++t )
			for( uint32_t j = 0; j < m_detailSide; ++j )
			{
				const float* line = reinterpret_cast<const float*>( detailBytes.data() + detailCopies[t].offset + j * detailCopies[t].rowPitch );
				float* out = detail.data() + ( ( static_cast<size_t>( t ) * m_detailSide + j ) * m_detailSide ) * 2;
				for( uint32_t i = 0; i < m_detailSide; ++i )
				{
					out[i * 2] = line[i * 4 + 1];
					out[i * 2 + 1] = line[i * 4 + 2];
				}
			}
	}
	std::error_code error;
	std::filesystem::create_directories( flowCacheDirectory, error );
	{
		std::ofstream out( path, std::ios::binary | std::ios::trunc );
		out.write( reinterpret_cast<const char*>( &header ), sizeof( header ) );
		out.write( reinterpret_cast<const char*>( coarse.data() ), coarse.size() * sizeof( uint16_t ) );
		out.write( reinterpret_cast<const char*>( detail.data() ), detail.size() * sizeof( float ) );
		out.write( coarseSummary.data(), coarseSummary.size() );
		out.write( detailSummary.data(), detailSummary.size() );
		if( !out )
		{
			LOG( "Water flow: can`t write cache " + path );
			return;
		}
	}
	// Хранятся последние flowCacheFiles: русла правят вживую, и каждый вариант — свой файл
	std::vector<std::filesystem::directory_entry> files;
	for( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( flowCacheDirectory, error ) )
		if( entry.is_regular_file() && entry.path().filename().string().rfind( "flow_", 0 ) == 0 )
			files.push_back( entry );
	std::sort( files.begin(), files.end(), []( const auto& a, const auto& b ) { return a.last_write_time() > b.last_write_time(); } );
	for( size_t i = flowCacheFiles; i < files.size(); ++i )
		std::filesystem::remove( files[i].path(), error );
	LOG( "Water flow: cache written " + path );
}

void WaterSimulation::solveStaticFlow( const Settings::Flow& flow )
{
	const auto start = std::chrono::high_resolution_clock::now();
	const std::string path = flowCachePath( flow );
	SlowTask task( 1.0f );
	if( !path.empty() )
	{
		task.enterProgressFrame( 0.0f, "Cache" );
		if( loadFlowCache( path ) )
		{
			m_flowBuilt = flow;
			LOG( "Water flow: from cache " + path + ", ms: " +
				 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() ) );
			return;
		}
	}
	task.enterProgressFrame( 0.5f, "1 m grid" );
	const std::string coarse = solveFlow( flow );
	buildSurface();
	m_surfaceDirty = false;
	task.enterProgressFrame( 0.5f, "Detail grid" );
	const std::string detail = solveDetailFlow( flow );
	// Под GPU-based validation расчёт урезан — такой в кэш не пишется
	if( !path.empty() && !coarse.empty() && !DMD3D::instance().gpuValidation() )
		saveFlowCache( path, coarse, detail );
}

void WaterSimulation::applyFlowChanges()
{
	if( !m_initialized || !m_static )
		return;
	const Settings current = settings();
	if( sameFlow( current.flow, m_flowBuilt ) )
		return;
	// mainStatic заново: у озёр нули течения, ручьи — как в растре; затем течение озёр и ручьёв, поверхность (детальная
	// вода берёт течение сетки 1 м) и течение детальной сетки
	if( buildStaticWater( current ) )
		solveStaticFlow( current.flow );
}

bool WaterSimulation::fillLakes()
{
	DMD3D& d3d = DMD3D::instance();
	Texture fill, lake;
	StorageView fillUAV, lakeUAV;
	for( auto [texture, uav, name] : { std::tuple{ &fill, &fillUAV, "Water lake level" }, std::tuple{ &lake, &lakeUAV, "Water lake mask" } } )
	{
		TextureDesc desc;
		desc.width = m_size;
		desc.height = m_size;
		desc.format = DXGI_FORMAT_R32_FLOAT;
		desc.usage = TextureUsage::unorderedAccess;
		if( !d3d.createTexture( desc, nullptr, *texture ) || !d3d.createStorageView( *texture, {}, *uav ) )
		{
			LOG( std::string( "Failed to create water simulation texture " ) + name );
			return false;
		}
		d3d.setName( *texture, name );
	}

	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	auto run = [&]( DMComputeShader& shader, uint32_t iterations )
	{
		for( uint32_t i = 0; i < iterations; ++i )
		{
			if( i % iterationsPerSubmit == 0 )
			{
				if( i > 0 )
					d3d.waitForGpu();
				PassDesc pass;
				pass.name = "Water lakes";
				pass.reads = { { &heightMap, "height map" }, { &m_sourcesView, "water sources" } };
				pass.writes = { { &fillUAV, "lake level" }, { &lakeUAV, "lake mask" }, { &m_waterUAV, "water depth" } };
				d3d.beginPass( pass );
				setParameters( m_initial );
				d3d.setSRV( 0, heightMap );
				d3d.setSRV( 2, m_sourcesView );
			}
			d3d.setUAV( 0, m_waterUAV );
			d3d.setUAV( 4, fillUAV );
			d3d.setUAV( 5, lakeUAV );
			shader.dispatchGroups( groups, groups, 1 );
		}
	};
	run( m_fillInitShader, 1 );
	run( m_fillShader, fillIterations );
	run( m_lakeInitShader, 1 );
	run( m_lakeGrowShader, lakeGrowIterations );
	run( m_lakeApplyShader, 1 );
	d3d.waitForGpu();
	return true;
}

void WaterSimulation::logWaterSummary()
{
	// Копия глубины на CPU: установилось ли течение к концу прогрева — сравнить с прогревом подольше
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_water, copies, bytes ) || copies.empty() )
		return;
	double volume = 0.0;
	float maxDepth = 0.0f;
	uint32_t wet = 0;
	for( uint32_t y = 0; y < copies[0].rows; ++y )
	{
		const float* row = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + y * copies[0].rowPitch );
		for( uint32_t x = 0; x < m_size; ++x )
		{
			volume += row[x];
			maxDepth = std::max( maxDepth, row[x] );
			wet += row[x] > wetDepth ? 1 : 0;
		}
	}
	volume *= m_cellSize * m_cellSize;
	char text[160];
	snprintf( text, sizeof( text ), "Water: volume %.0f m3, max depth %.2f m, cells deeper than 1 cm %u", volume, maxDepth, wet );
	LOG( text );
}

WaterSimulation::Settings WaterSimulation::settings() const
{
	Settings settings = m_initial;
	settings.timeScale = m_properties["Time scale"].data<float>();
	settings.sourceRate = m_properties["Source rate"].data<float>();
	settings.flowStart = m_properties["Flow start"].data<float>();
	settings.flowFull = m_properties["Flow full"].data<float>();
	settings.sourceRadius = m_properties["Source radius"].data<float>();
	settings.rain = m_properties["Rain"].data<float>();
	settings.evaporation = m_properties["Evaporation"].data<float>();
	settings.manning = m_properties["Manning roughness"].data<float>();
	settings.absorption = m_surfaceProperties["Absorption"].data<DirectX::XMFLOAT3>();
	settings.scatterColor = m_surfaceProperties["Scatter color"].data<DirectX::XMFLOAT3>();
	settings.scatterStrength = m_surfaceProperties["Scatter strength"].data<float>();
	settings.roughness = m_surfaceProperties["Roughness"].data<float>();
	settings.rippleScale = m_surfaceProperties["Ripple scale"].data<float>();
	settings.rippleStrength = m_surfaceProperties["Ripple strength"].data<float>();
	settings.calmRipple = m_surfaceProperties["Calm ripple"].data<float>();
	settings.refraction = m_surfaceProperties["Refraction"].data<float>();
	settings.rippleStretch = m_surfaceProperties["Ripple stretch"].data<float>();
	settings.foamSpeed = m_surfaceProperties["Foam speed"].data<float>();
	settings.foamShear = m_surfaceProperties["Foam shear"].data<float>();
	settings.detailDistance = m_surfaceProperties["Detail distance"].data<float>();
	settings.flow.settleTime = m_flowProperties["Settle time"].data<float>();
	settings.flow.timeStep = m_flowProperties["Time step"].data<float>();
	settings.flow.viscosity = m_flowProperties["Viscosity"].data<float>();
	settings.flow.manning = m_flowProperties["Bed roughness"].data<float>();
	settings.flow.iterations = m_flowProperties["Pressure iterations"].data<int32_t>();
	settings.flow.layer = m_flowProperties["Surface layer"].data<float>();
	settings.flow.detailSettleTime = m_flowProperties["Detail settle time"].data<float>();
	for( const ChannelParameter& parameter : channelParameters )
		settings.channels.*parameter.field = m_channelsProperties[parameter.name].data<float>();
	settings.channels.smooth = m_channelsProperties["Smooth passes"].data<int32_t>();
	settings.channels.paintLayer = m_channelsProperties["Paint layer"].data<int32_t>();
	return settings;
}

std::vector<StreamCurve> WaterSimulation::streamCurves() const
{
	std::vector<StreamCurve> curves = m_curves;
	for( size_t i = 0; i < curves.size() && i < m_streamProperties.size(); ++i )
	{
		StreamCurve& curve = curves[i];
		const PropertyContainer& properties = *m_streamProperties[i];
		const bool enabled = properties["Enabled"].data<bool>();
		bool changed = enabled != curve.enabled;
		curve.enabled = enabled;
		for( const StreamScale& scale : streamScales )
		{
			const float value = properties[scale.name].data<float>();
			changed = changed || value != curve.*scale.field;
			curve.*scale.field = value;
		}
		// Правка в окне — кривая правленная: новая генерация её не заменит
		curve.edited = curve.edited || changed;
	}
	return curves;
}

void WaterSimulation::setCurvePoint( size_t curve, size_t point, DirectX::XMFLOAT2 position )
{
	if( curve < m_curves.size() && point < m_curves[curve].points.size() )
		m_curves[curve].points[point].position = position;
}

bool WaterSimulation::insertCurvePoint( size_t curve, int after, DirectX::XMFLOAT2 position )
{
	if( curve >= m_curves.size() || after < -1 || after >= static_cast<int>( m_curves[curve].points.size() ) )
		return false;
	std::vector<StreamCurvePoint>& points = m_curves[curve].points;
	// Расход новой точки — как у соседней выше по течению (у первой — у прежней первой)
	StreamCurvePoint point = points[std::max( after, 0 )];
	point.position = position;
	points.insert( points.begin() + ( after + 1 ), point );
	finishCurveEdit( curve );
	return true;
}

bool WaterSimulation::removeCurvePoint( size_t curve, size_t point )
{
	if( curve >= m_curves.size() || point >= m_curves[curve].points.size() || m_curves[curve].points.size() <= 2 )
		return false;
	m_curves[curve].points.erase( m_curves[curve].points.begin() + point );
	finishCurveEdit( curve );
	return true;
}

void WaterSimulation::finishCurveEdit( size_t curve )
{
	if( curve >= m_curves.size() )
		return;
	m_curves[curve].edited = true;
	m_curves[curve].pointsChanged = true;
	m_curvesDirty = true;
}

PropertyContainer* WaterSimulation::curveProperties( size_t curve )
{
	return curve < m_streamProperties.size() ? m_streamProperties[curve].get() : nullptr;
}

bool WaterSimulation::rebuildRequested() const
{
	if( !m_initialized )
		return false;
	if( m_curvesDirty )
		return true;
	for( size_t i = 0; i < m_builtCurves.size() && i < m_streamProperties.size(); ++i )
	{
		const PropertyContainer& properties = *m_streamProperties[i];
		if( properties["Enabled"].data<bool>() != m_builtCurves[i].enabled )
			return true;
		for( const StreamScale& scale : streamScales )
			if( properties[scale.name].data<float>() != m_builtCurves[i].*scale.field )
				return true;
	}
	const Settings current = settings();
	for( const ChannelParameter& parameter : channelParameters )
		if( !generatorParameter( parameter.field ) && current.channels.*parameter.field != m_builtChannels.*parameter.field )
			return true;
	return current.channels.paintLayer != m_builtChannels.paintLayer;
}

bool WaterSimulation::rebuild( const TerrainHydrology::Result& hydrology )
{
	const auto start = std::chrono::high_resolution_clock::now();
	m_builtCurves = streamCurves();
	const Settings current = settings();
	m_builtChannels = current.channels;
	m_curvesDirty = false;
	if( !createHydrologyTextures( hydrology ) )
		return false;
	if( m_static )
		m_staticWaterMap = &m_staticWaterView;
	buildSources( current );
	if( m_static )
	{
		if( !buildStaticWater( current ) )
			return false;
		solveStaticFlow( current.flow );
	}
	else
		m_surfaceDirty = true;
	LOG( "Water rebuilt for the edited channels, ms: " +
		 std::to_string( std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count() ) );
	return true;
}

void WaterSimulation::setParameters( const Settings& settings )
{
	const TerrainHeight height = m_terrain->terrainHeight();
	Parameters params = {};
	params.size = m_size;
	params.cellSize = m_cellSize;
	params.timeStep = settings.timeStep;
	params.gravity = gravity;
	params.rain = settings.rain * mmPerHour;
	params.evaporation = settings.evaporation * mmPerHour;
	// л/с на ячейку → м/с слоя воды на её площади
	params.sourceRate = settings.sourceRate * 0.001f / ( m_cellSize * m_cellSize );
	params.heightMultiplier = height.heightMultiplier;
	params.heightOffset = height.heightOffset;
	params.logFlowStart = std::log10( std::max( settings.flowStart, 1.0f ) );
	params.logFlowFull = std::max( std::log10( std::max( settings.flowFull, 1.0f ) ), params.logFlowStart + 0.01f );
	params.sourceRadius = std::clamp( static_cast<int32_t>( std::lround( settings.sourceRadius / m_cellSize ) ), 0, maxSourceRadius );
	params.manning = settings.manning;
	params.helperCount = m_helperCount;
	Device::updateResourceData( m_constantBuffer, params );
	DMD3D::instance().setConstantBuffer( 4, m_constantBuffer );
}

void WaterSimulation::buildSources( const Settings& settings )
{
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& flowMap = m_flowView;
	PassDesc pass;
	pass.name = "Water sources";
	pass.reads = { { &flowMap, "flow map" } };
	if( m_helperCount > 0 )
		pass.reads.push_back( { &m_helpersView, "water source helpers" } );
	pass.writes = { { &m_sourcesUAV, "water sources" } };
	d3d.beginPass( pass );
	setParameters( settings );
	d3d.setSRV( 1, flowMap );
	if( m_helperCount > 0 )
		d3d.setSRV( 4, m_helpersView );
	d3d.setUAV( 3, m_sourcesUAV );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	m_sourcesShader.dispatchGroups( groups, groups, 1 );
	m_sourcesBuilt = settings;
}

void WaterSimulation::step( uint32_t steps )
{
	if( steps == 0 )
		return;
	DMD3D& d3d = DMD3D::instance();
	const ShaderView& heightMap = *m_terrain->terrainHeight().heightMap;
	PassDesc pass;
	pass.name = "Water simulation";
	pass.reads = { { &heightMap, "height map" }, { &m_sourcesView, "water sources" } };
	pass.writes = { { &m_waterUAV, "water depth" }, { &m_fluxUAV, "water flux" }, { &m_outputUAV, "water state" },
					{ &m_memoryUAV, "water memory" } };
	d3d.beginPass( pass );
	// Константы привязаны setParameters до прохода; beginPass очищает только таблицу привязок
	d3d.setSRV( 0, heightMap );
	d3d.setSRV( 2, m_sourcesView );
	const uint32_t groups = ( m_size + groupSize - 1 ) / groupSize;
	for( uint32_t i = 0; i < steps; ++i )
	{
		// Привязка UAV после dispatch ставит барьер UAV → UAV: следующая стадия видит записанное предыдущей
		d3d.setUAV( 0, m_waterUAV );
		d3d.setUAV( 1, m_fluxUAV );
		m_fluxShader.dispatchGroups( groups, groups, 1 );
		d3d.setUAV( 0, m_waterUAV );
		d3d.setUAV( 1, m_fluxUAV );
		d3d.setUAV( 2, m_outputUAV );
		d3d.setUAV( 6, m_memoryUAV );
		m_waterShader.dispatchGroups( groups, groups, 1 );
	}
}

void WaterSimulation::compute( const FrameContext& frame )
{
	if( !m_initialized )
		return;
	const Settings current = settings();
	if( !m_static && !sameSources( current, m_sourcesBuilt ) )
		buildSources( current );

	// Время симуляции — по времени кадра (с timestep — фиксированному), не больше maxStepsPerFrame шагов за кадр
	if( !m_static && current.timeStep > 0.0f )
	{
		m_accumulated = std::min( m_accumulated + frame.elapsedTime * 0.001f * current.timeScale,
								  current.timeStep * maxStepsPerFrame );
		const uint32_t steps = static_cast<uint32_t>( m_accumulated / current.timeStep );
		m_accumulated -= steps * current.timeStep;
		if( steps > 0 )
		{
			setParameters( current );
			step( steps );
			m_surfaceDirty = true;
		}
	}
	if( m_surfaceDirty )
		buildSurface();
	cullTiles( frame.view );
	// Вода — ресурс сцены до следующего кадра (террейн, поверхность воды, мокрый берег, расстановка, частицы); приток —
	// подсветке «Show water»
	DMD3D::instance().setSRV( SLOT_WATER, m_outputView );
	DMD3D::instance().setSRV( SLOT_WATER_SOURCES, m_sourcesView );
	// Уровень поверхности — глубине в точке (урез внутри клетки): мокрая земля, галька дна, трава в воде
	DMD3D::instance().setSRV( SLOT_WATER_LEVEL, m_levelView );
	// Детальная вода у русел — тем же потребителям (water.sh: по индексу плиток детальной земли)
	if( m_detailTileCount > 0 )
		DMD3D::instance().setSRV( SLOT_WATER_DETAIL, m_detailWaterView );
}

bool WaterSimulation::exportState( const std::string& prefix, std::string& reason )
{
	if( !m_initialized )
	{
		reason = "the level has no water simulation";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_output, copies, bytes ) || copies.empty() )
	{
		reason = "water state readback failed";
		return false;
	}
	const size_t cells = static_cast<size_t>( m_size ) * m_size;
	std::vector<float> channels[3] = { std::vector<float>( cells ), std::vector<float>( cells ), std::vector<float>( cells ) };
	for( uint32_t row = 0; row < m_size; ++row )
	{
		const auto* texel = reinterpret_cast<const DirectX::PackedVector::HALF*>( bytes.data() + copies[0].offset + row * copies[0].rowPitch );
		for( uint32_t col = 0; col < m_size; ++col )
			for( int c = 0; c < 3; ++c )
				channels[c][static_cast<size_t>( row ) * m_size + col] = DirectX::PackedVector::XMConvertHalfToFloat( texel[col * 4 + c] );
	}
	const char* names[3] = { "_depth.dds", "_vx.dds", "_vz.dds" };
	for( int c = 0; c < 3; ++c )
	{
		const std::string file = prefix + names[c];
		if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), m_size, m_size, channels[c].data() ) )
		{
			reason = "cannot write " + file;
			return false;
		}
	}
	LOG( "Water state: " + prefix + "_depth.dds, _vx.dds, _vz.dds" );

	// Детальная вода: плитки — в растр мира (внутренние тексели, без поля), строка 0 — дальний край по z
	if( m_detailTileCount == 0 )
		return true;
	copies.clear();
	bytes.clear();
	if( !DMD3D::instance().captureTexture( m_detailWater, copies, bytes ) || copies.size() < m_detailTileCount )
	{
		reason = "water detail readback failed";
		return false;
	}
	const uint32_t inner = m_detailSide - 2;
	const uint32_t side = static_cast<uint32_t>( std::lround( m_terrain->terrainHeight().worldSize / m_detailTileSize ) ) * inner;
	std::vector<float> detail[4];
	detail[0].assign( static_cast<size_t>( side ) * side, -1e9f );
	for( int c = 1; c < 4; ++c )
		detail[c].assign( static_cast<size_t>( side ) * side, 0.0f );
	for( uint32_t t = 0; t < m_detailTileCount; ++t )
	{
		for( uint32_t j = 1; j <= inner; ++j )
		{
			const float* texel = reinterpret_cast<const float*>( bytes.data() + copies[t].offset + j * copies[t].rowPitch );
			const uint32_t z = m_detailCoords[t].y * inner + j - 1;
			if( z >= side )
				continue;
			for( uint32_t i = 1; i <= inner; ++i )
			{
				const uint32_t x = m_detailCoords[t].x * inner + i - 1;
				if( x >= side )
					continue;
				const size_t index = static_cast<size_t>( side - 1 - z ) * side + x;
				for( int c = 0; c < 4; ++c )
					detail[c][index] = texel[i * 4 + c];
			}
		}
	}
	const char* detailNames[4] = { "_detail_level.dds", "_detail_vx.dds", "_detail_vz.dds", "_detail_depth.dds" };
	for( int c = 0; c < 4; ++c )
	{
		const std::string file = prefix + detailNames[c];
		if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), side, side, detail[c].data() ) )
		{
			reason = "cannot write " + file;
			return false;
		}
	}
	LOG( "Water detail state: " + prefix + "_detail_level.dds, _detail_vx.dds, _detail_vz.dds, _detail_depth.dds" );
	return true;
}

bool WaterSimulation::exportDischarge( const std::string& file, std::string& reason )
{
	if( !m_initialized )
	{
		reason = "the level has no water simulation";
		return false;
	}
	std::vector<DMD3D::SubresourceCopy> copies;
	std::vector<uint8_t> bytes;
	if( !DMD3D::instance().captureTexture( m_flux, copies, bytes ) || copies.empty() )
	{
		reason = "water flux readback failed";
		return false;
	}
	std::vector<float> discharge( static_cast<size_t>( m_size ) * m_size );
	double total = 0.0;
	float peak = 0.0f;
	for( uint32_t row = 0; row < m_size; ++row )
	{
		const float* flux = reinterpret_cast<const float*>( bytes.data() + copies[0].offset + row * copies[0].rowPitch );
		for( uint32_t col = 0; col < m_size; ++col )
		{
			const float* out = flux + col * 4;
			const float q = out[0] + out[1] + out[2] + out[3];
			discharge[static_cast<size_t>( row ) * m_size + col] = q;
			peak = std::max( peak, q );
			total += q;
		}
	}
	if( !GpuImages::saveFloatDDS( std::wstring( file.begin(), file.end() ), m_size, m_size, discharge.data() ) )
	{
		reason = "cannot write " + file;
		return false;
	}
	char text[160];
	snprintf( text, sizeof( text ), "Water discharge: %ux%u, peak %.3f m3/s -> ", m_size, m_size, peak );
	LOG( text + file );
	return true;
}

PropertyContainer* WaterSimulation::properties()
{
	return &m_properties;
}

}
