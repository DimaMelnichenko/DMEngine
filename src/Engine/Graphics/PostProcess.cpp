#include "PostProcess.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "D3D\DMD3D.h"

namespace GS
{

namespace
{

void addSlider( PropertyContainer& properties, const char* name, float value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
}

void addSlider( PropertyContainer& properties, const char* name, int32_t value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
}

// Экспозиция по EV100 — как exposureFromEV100 в Shaders/exposure.sh
float exposureFromEV100( float ev100 )
{
	return 1.0f / ( 1.2f * std::exp2( ev100 ) );
}

const char* const meteringModeProperty = "Metering mode (0 manual, 1 auto)";
const char* const tonemapperProperty = "Tonemapper (0 none, 1 ACES, 2 AgX)";

}

PostProcess::Tonemapper PostProcess::tonemapperFromName( const std::string& name )
{
	return name == "None" ? Tonemapper::none : name == "ACES" ? Tonemapper::aces : Tonemapper::agx;
}

const char* PostProcess::tonemapperName( Tonemapper tonemapper )
{
	switch( tonemapper )
	{
		case Tonemapper::none: return "None";
		case Tonemapper::aces: return "ACES";
		default: return "AgX";
	}
}

PostProcess::MeteringMode PostProcess::meteringModeFromName( const std::string& name )
{
	return name == "Manual" ? MeteringMode::manual : MeteringMode::autoHistogram;
}

const char* PostProcess::meteringModeName( MeteringMode mode )
{
	return mode == MeteringMode::manual ? "Manual" : "AutoHistogram";
}

std::vector<XMFLOAT2> PostProcess::curveFromText( const std::string& text )
{
	std::vector<XMFLOAT2> curve;
	std::istringstream keys( text );
	std::string key;
	while( std::getline( keys, key, ';' ) )
	{
		XMFLOAT2 value;
		if( std::sscanf( key.c_str(), " %f , %f", &value.x, &value.y ) == 2 )
			curve.push_back( value );
	}
	std::sort( curve.begin(), curve.end(), []( const XMFLOAT2& a, const XMFLOAT2& b ) { return a.x < b.x; } );
	if( curve.size() > maxCurveKeys )
		curve.resize( maxCurveKeys );
	return curve;
}

std::string PostProcess::curveText( const std::vector<XMFLOAT2>& curve )
{
	std::string text;
	for( const XMFLOAT2& key : curve )
	{
		char value[64];
		std::snprintf( value, sizeof( value ), "%s%g,%g", text.empty() ? "" : "; ", key.x, key.y );
		text += value;
	}
	return text;
}

bool PostProcess::initialize( const Settings& settings )
{
	if( !m_shader.load( "Shaders\\tonemap.ps" ) ||
		!m_bloomDownsample.load( "Shaders\\bloom_downsample.ps" ) ||
		!m_bloomUpsample.load( "Shaders\\bloom_upsample.ps" ) ||
		!m_histogramShader.Initialize( "Shaders\\exposure_histogram.cs", "main" ) ||
		!m_adaptShader.Initialize( "Shaders\\exposure_adapt.cs", "main" ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!d3d.createShaderConstantBuffer( sizeof( BloomParameters ), m_bloomConstants ) ||
		!d3d.createShaderConstantBuffer( sizeof( HistogramParameters ), m_histogramConstants ) ||
		!d3d.createShaderConstantBuffer( sizeof( AdaptParameters ), m_adaptConstants ) )
		return false;

	if( !createBloomTargets() )
		return false;

	// Первый кадр — с ручной экспозицией: автоэкспозиция начинает с неё, без вспышки при запуске
	if( !createExposureResources( settings.manualEV100, settings.exposureCompensation ) )
		return false;

	m_properties.setName( "Post process" );
	addSlider( m_properties, meteringModeProperty, static_cast<int32_t>( settings.meteringMode ), 0.0f, 1.0f );
	addSlider( m_properties, "Manual EV100", settings.manualEV100, -10.0f, 20.0f );
	addSlider( m_properties, "Exposure compensation (EV)", settings.exposureCompensation, -6.0f, 6.0f );
	// Ключи кривой — столько, сколько в базе: (EV100 сцены, поправка EV)
	for( size_t i = 0; i < settings.exposureCompensationCurve.size(); ++i )
	{
		m_curveKeyNames.push_back( "Compensation curve key " + std::to_string( i + 1 ) + " (EV100, EV)" );
		Property* key = m_properties.insert( m_curveKeyNames.back(), settings.exposureCompensationCurve[i] );
		key->setLow( -10.0f );
		key->setHigh( 20.0f );
		key->setControlType( GUIControlType::DRAG );
	}
	addSlider( m_properties, "Min EV100", settings.minEV100, -10.0f, 20.0f );
	addSlider( m_properties, "Max EV100", settings.maxEV100, -10.0f, 20.0f );
	addSlider( m_properties, "Histogram low percent", settings.histogramLowPercent, 0.0f, 100.0f );
	addSlider( m_properties, "Histogram high percent", settings.histogramHighPercent, 0.0f, 100.0f );
	addSlider( m_properties, "Speed up", settings.speedUp, 0.0f, 20.0f );
	addSlider( m_properties, "Speed down", settings.speedDown, 0.0f, 20.0f );
	addSlider( m_properties, tonemapperProperty, static_cast<int32_t>( settings.tonemapper ), 0.0f, 2.0f );
	addSlider( m_properties, "Bloom intensity", settings.bloomIntensity, 0.0f, 8.0f );
	addSlider( m_properties, "Bloom threshold", settings.bloomThreshold, -1.0f, 10.0f );
	addSlider( m_properties, "Purkinje shift", settings.purkinjeShift, 0.0f, 1.0f );

	return true;
}

bool PostProcess::createBloomTargets()
{
	// Уровни bloom: половина кадра, четверть … 1/64; R11G11B10 — без альфы, как у bloom в UE
	DMD3D& d3d = DMD3D::instance();
	uint32_t width = d3d.sceneWidth();
	uint32_t height = d3d.sceneHeight();
	for( RenderTarget& level : m_bloom )
	{
		width = std::max( width / 2, 1u );
		height = std::max( height / 2, 1u );
		if( !level.create( width, height, DXGI_FORMAT_R11G11B10_FLOAT ) )
			return false;
	}
	return true;
}

bool PostProcess::resize()
{
	return createBloomTargets();
}

bool PostProcess::createExposureResources( float initialEV100, float exposureCompensation )
{
	DMD3D& d3d = DMD3D::instance();

	// Гистограмма — HISTOGRAM_BINS чисел uint, пишется атомарно (RWByteAddressBuffer): байтовый буфер и виды raw
	BufferDesc histogramDesc;
	histogramDesc.size = histogramBinCount * sizeof( uint32_t );
	histogramDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::raw;
	BufferViewDesc rawView;
	rawView.raw = true;
	if( !d3d.createBuffer( histogramDesc, nullptr, m_histogram ) ||
		!d3d.createStorageView( m_histogram, rawView, m_histogramUAV ) || !d3d.createShaderView( m_histogram, rawView, m_histogramSRV ) )
		return false;

	// Состояние экспозиции — одна запись ExposureState; начальное — сразу «сошедшееся» к initialEV100
	const float exposure = exposureFromEV100( initialEV100 - exposureCompensation );
	const ExposureState initial = { exposure, exposure, initialEV100, 0.0f };
	BufferDesc stateDesc;
	stateDesc.size = sizeof( ExposureState );
	stateDesc.stride = sizeof( ExposureState );
	stateDesc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	if( !d3d.createBuffer( stateDesc, &initial, m_exposureState ) ||
		!d3d.createStorageView( m_exposureState, {}, m_exposureUAV ) || !d3d.createShaderView( m_exposureState, {}, m_exposureSRV ) )
		return false;

	// Копии для чтения на CPU (статистика)
	BufferDesc readbackDesc;
	readbackDesc.size = sizeof( ExposureState );
	readbackDesc.usage = BufferUsage::readback;
	for( auto& readback : m_exposureReadback )
	{
		if( !d3d.createBuffer( readbackDesc, nullptr, readback ) )
			return false;
	}
	m_ev100 = initialEV100;
	return true;
}

PostProcess::Settings PostProcess::settings()
{
	Settings settings;
	settings.meteringMode = static_cast<MeteringMode>( std::clamp( m_properties[meteringModeProperty].data<int32_t>(), 0, 1 ) );
	settings.manualEV100 = m_properties["Manual EV100"].data<float>();
	settings.exposureCompensation = m_properties["Exposure compensation (EV)"].data<float>();
	// Ключ, сдвинутый в GUI за соседний, встаёт на своё место
	for( const std::string& name : m_curveKeyNames )
		settings.exposureCompensationCurve.push_back( m_properties[name].data<XMFLOAT2>() );
	std::sort( settings.exposureCompensationCurve.begin(), settings.exposureCompensationCurve.end(),
			   []( const XMFLOAT2& a, const XMFLOAT2& b ) { return a.x < b.x; } );
	settings.minEV100 = m_properties["Min EV100"].data<float>();
	settings.maxEV100 = m_properties["Max EV100"].data<float>();
	settings.histogramLowPercent = m_properties["Histogram low percent"].data<float>();
	settings.histogramHighPercent = m_properties["Histogram high percent"].data<float>();
	settings.speedUp = m_properties["Speed up"].data<float>();
	settings.speedDown = m_properties["Speed down"].data<float>();
	settings.tonemapper = static_cast<Tonemapper>( std::clamp( m_properties[tonemapperProperty].data<int32_t>(), 0, 2 ) );
	settings.bloomIntensity = m_properties["Bloom intensity"].data<float>();
	settings.bloomThreshold = m_properties["Bloom threshold"].data<float>();
	settings.purkinjeShift = m_properties["Purkinje shift"].data<float>();
	return settings;
}

void PostProcess::bindExposure()
{
	DMD3D::instance().setSRV( SRVType::ps, SLOT_EXPOSURE, m_exposureSRV );
}

void PostProcess::render( GpuProfiler& profiler, float deltaTime )
{
	DMD3D& d3d = DMD3D::instance();
	// Сведение выборок MSAA — один раз за кадр: цвет сцены читают замер, bloom и тонмаппинг
	const ShaderView& sceneColor = d3d.sceneColor();
	const Settings current = settings();

	profiler.beginScope( "Exposure" );
	renderExposure( sceneColor, deltaTime );
	profiler.endScope();

	if( current.bloomIntensity > 0.0f )
	{
		profiler.beginScope( "Bloom" );
		renderBloom( sceneColor, current.bloomThreshold );
		profiler.endScope();
	}

	profiler.beginScope( "Tonemap" );
	d3d.beginPass( PassDesc{ "Tonemap", { { &d3d.backBufferTarget(), "back buffer" } }, {}, d3d.sceneWidth(), d3d.sceneHeight(),
							 { { &sceneColor, "scene color" }, { &m_bloom[0].srv(), "bloom 1/2" }, { &m_exposureSRV, "exposure" } } } );
	Parameters params = {};
	params.tonemapper = static_cast<int32_t>( current.tonemapper );
	// Уровень 1/2 хранит сумму уровней с весами 1, f, f², …: деление на неё — Bloom Intensity как доля энергии
	float weightSum = 0.0f;
	for( uint32_t level = 0; level < bloomLevelCount; ++level )
		weightSum += std::pow( bloomLevelFalloff, static_cast<float>( level ) );
	params.bloomScale = std::max( current.bloomIntensity, 0.0f ) / weightSum;
	params.purkinjeShift = std::clamp( current.purkinjeShift, 0.0f, 1.0f );
	Device::updateResourceData<Parameters>( m_constantBuffer, params );
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
	d3d.setSRV( SRVType::ps, 0, sceneColor );
	d3d.setSRV( SRVType::ps, 1, m_bloom[0].srv() );

	m_shader.draw();
	profiler.endScope();
}

void PostProcess::renderExposure( const ShaderView& sceneColor, float deltaTime )
{
	DMD3D& d3d = DMD3D::instance();
	const Settings current = settings();

	// Compute-проход: цвет сцены читается (его цель снимается), гистограмма пишется
	d3d.beginPass( PassDesc{ "Exposure histogram", {}, {}, 0, 0, { { &sceneColor, "scene color" }, { &m_exposureSRV, "exposure" } },
							 { { &m_histogramUAV, "histogram" } } } );

	// Диапазон гистограммы — яркости Min…Max EV100: log₂ L = EV100 + log₂( 12,5 / 100 ) = EV100 − 3
	const float minLog2Luminance = current.minEV100 - 3.0f;
	const float log2LuminanceRange = std::max( current.maxEV100 - current.minEV100, 1.0f );

	if( current.meteringMode == MeteringMode::autoHistogram )
	{
		d3d.clearStorageView( m_histogramUAV );

		HistogramParameters histogram = { d3d.sceneWidth(), d3d.sceneHeight(), minLog2Luminance, log2LuminanceRange };
		Device::updateResourceData<HistogramParameters>( m_histogramConstants, histogram );
		d3d.setConstantBuffer( SRVType::cs, 4, m_histogramConstants );
		d3d.setSRV( SRVType::cs, 0, sceneColor );
		d3d.setSRV( SRVType::cs, SLOT_EXPOSURE, m_exposureSRV );
		m_histogramShader.setUAVBuffer( 0, m_histogramUAV );
		// Поток — пиксель из квадрата 2 × 2, группа — 16 × 16 потоков
		m_histogramShader.dispatchGroups( ( d3d.sceneWidth() + 31 ) / 32, ( d3d.sceneHeight() + 31 ) / 32, 1 );
	}

	AdaptParameters adapt = {};
	adapt.meteringMode = static_cast<int32_t>( current.meteringMode );
	adapt.manualEV100 = current.manualEV100;
	adapt.exposureCompensation = current.exposureCompensation;
	// После смены плана — шаг времени, с которым адаптация (1 − e^(−Δt · скорость)) доходит до цели за кадр
	adapt.deltaTime = m_cutFrames > 0 ? 1000.0f : deltaTime;
	if( m_cutFrames > 0 )
		--m_cutFrames;
	adapt.minEV100 = current.minEV100;
	adapt.maxEV100 = std::max( current.maxEV100, current.minEV100 );
	adapt.lowPercent = std::clamp( current.histogramLowPercent, 0.0f, 100.0f ) / 100.0f;
	adapt.highPercent = std::clamp( current.histogramHighPercent, adapt.lowPercent * 100.0f, 100.0f ) / 100.0f;
	adapt.speedUp = current.speedUp;
	adapt.speedDown = current.speedDown;
	adapt.minLog2Luminance = minLog2Luminance;
	adapt.log2LuminanceRange = log2LuminanceRange;
	adapt.curveKeyCount = static_cast<int32_t>( std::min<size_t>( current.exposureCompensationCurve.size(), maxCurveKeys ) );
	for( int32_t i = 0; i < adapt.curveKeyCount; ++i )
	{
		const XMFLOAT2& key = current.exposureCompensationCurve[i];
		XMFLOAT4& pair = adapt.curveKeys[i / 2];
		( i % 2 == 0 ? pair.x : pair.z ) = key.x;
		( i % 2 == 0 ? pair.y : pair.w ) = key.y;
	}
	// Адаптация пишет состояние экспозиции: beginPass снимает его со входов пиксельных шейдеров (t105)
	d3d.beginPass( PassDesc{ "Exposure adapt", {}, {}, 0, 0, { { &m_histogramSRV, "histogram" } }, { { &m_exposureUAV, "exposure" } } } );
	Device::updateResourceData<AdaptParameters>( m_adaptConstants, adapt );
	d3d.setConstantBuffer( SRVType::cs, 4, m_adaptConstants );
	d3d.setSRV( SRVType::cs, 0, m_histogramSRV );
	m_adaptShader.setUAVBuffer( 0, m_exposureUAV );
	m_adaptShader.dispatchGroups( 1, 1, 1 );

	// Новая экспозиция — bloom и тонмаппингу этого кадра, сцене следующего
	bindExposure();
	readBackExposure();
}

void PostProcess::readBackExposure()
{
	DMD3D& d3d = DMD3D::instance();
	d3d.copyBuffer( m_exposureReadback[m_readbackFrame % readbackCount], m_exposureState );
	++m_readbackFrame;
	if( m_readbackFrame < readbackCount )
		return;

	// От самой свежей копии прошлых кадров к самой старой: первая готовая и есть последнее известное значение.
	// GPU отстаёт от CPU на несколько кадров, без ожидания готовы только старые копии
	for( uint32_t age = 1; age < readbackCount; ++age )
	{
		ExposureState state = {};
		if( d3d.readBuffer( m_exposureReadback[( m_readbackFrame - 1 - age ) % readbackCount], &state, sizeof( state ) ) )
		{
			m_ev100 = state.ev100;
			break;
		}
	}
}

void PostProcess::renderBloom( const ShaderView& sceneColor, float threshold )
{
	DMD3D& d3d = DMD3D::instance();
	BloomParameters params = {};
	params.threshold = threshold;
	params.radius = 1.0f;
	params.weight = bloomLevelFalloff;

	// Вниз: сцена → 1/2 → 1/4 … 1/64; первый проход — с порогом и усреднением Karis
	for( uint32_t level = 0; level < bloomLevelCount; ++level )
	{
		const float sourceWidth = static_cast<float>( level == 0 ? d3d.sceneWidth() : m_bloom[level - 1].width() );
		const float sourceHeight = static_cast<float>( level == 0 ? d3d.sceneHeight() : m_bloom[level - 1].height() );
		params.sourceTexelSize = XMFLOAT2( 1.0f / sourceWidth, 1.0f / sourceHeight );
		params.firstPass = level == 0 ? 1 : 0;
		drawPass( "Bloom downsample", m_bloomDownsample, m_bloom[level], level == 0 ? sceneColor : m_bloom[level - 1].srv(), params,
				  BlendState::opaque );
	}

	// Вверх: каждый уровень, увеличенный тентом и ослабленный весом, добавляется в соседний крупный — в 1/2 собирается
	// сумма всех уровней, у k-го — вес bloomLevelFalloff в степени k
	params.firstPass = 0;
	for( uint32_t level = bloomLevelCount - 1; level > 0; --level )
	{
		params.sourceTexelSize = XMFLOAT2( 1.0f / m_bloom[level].width(), 1.0f / m_bloom[level].height() );
		drawPass( "Bloom upsample", m_bloomUpsample, m_bloom[level - 1], m_bloom[level].srv(), params, BlendState::additive );
	}
}

void PostProcess::drawPass( const char* name, FullscreenShader& shader, const RenderTarget& target,
							const ShaderView& source, BloomParameters params, BlendState blend )
{
	DMD3D& d3d = DMD3D::instance();
	d3d.beginPass( PassDesc{ name, { { &target.target(), "bloom level" } }, {}, target.width(), target.height(), { { &source, "source" } } } );
	Device::updateResourceData<BloomParameters>( m_bloomConstants, params );
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_bloomConstants );
	d3d.setSRV( SRVType::ps, 0, source );

	shader.draw( blend );
}

PropertyContainer* PostProcess::properties()
{
	return &m_properties;
}

}
