#include "PostProcess.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"

namespace GS
{

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

bool PostProcess::initialize( const Settings& settings )
{
	if( !m_shader.load( "Shaders\\tonemap.ps" ) ||
		!m_bloomDownsample.load( "Shaders\\bloom_downsample.ps" ) ||
		!m_bloomUpsample.load( "Shaders\\bloom_upsample.ps" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( BloomParameters ), m_bloomConstants ) )
		return false;

	// Уровни bloom: половина кадра, четверть … 1/64; R11G11B10 — без альфы, как у bloom в UE
	uint32_t width = DMD3D::instance().sceneWidth();
	uint32_t height = DMD3D::instance().sceneHeight();
	for( RenderTarget& level : m_bloom )
	{
		width = std::max( width / 2, 1u );
		height = std::max( height / 2, 1u );
		if( !level.create( width, height, DXGI_FORMAT_R11G11B10_FLOAT ) )
			return false;
	}

	m_properties.setName( "Post process" );

	auto prop = m_properties.insert( "Exposure compensation (EV)", settings.exposureCompensation );
	prop->setLow( -6.0f );
	prop->setHigh( 6.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Tonemapper (0 none, 1 ACES, 2 AgX)", static_cast<int32_t>( settings.tonemapper ) );
	prop->setLow( 0.0f );
	prop->setHigh( 2.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Bloom intensity", settings.bloomIntensity );
	prop->setLow( 0.0f );
	prop->setHigh( 8.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Bloom threshold", settings.bloomThreshold );
	prop->setLow( -1.0f );
	prop->setHigh( 10.0f );
	prop->setControlType( GUIControlType::SLIDER );

	return true;
}

PostProcess::Settings PostProcess::settings()
{
	Settings settings;
	settings.exposureCompensation = m_properties["Exposure compensation (EV)"].data<float>();
	settings.tonemapper = static_cast<Tonemapper>( std::clamp( m_properties["Tonemapper (0 none, 1 ACES, 2 AgX)"].data<int32_t>(), 0, 2 ) );
	settings.bloomIntensity = m_properties["Bloom intensity"].data<float>();
	settings.bloomThreshold = m_properties["Bloom threshold"].data<float>();
	return settings;
}

void PostProcess::render( GpuProfiler& profiler )
{
	DMD3D& d3d = DMD3D::instance();
	// Сведение выборок MSAA — один раз за кадр: цвет сцены читают и bloom, и тонмаппинг
	const com_unique_ptr<ID3D11ShaderResourceView>& sceneColor = d3d.sceneColor();
	const float exposure = std::exp2( m_properties["Exposure compensation (EV)"].data<float>() );
	const float bloomIntensity = m_properties["Bloom intensity"].data<float>();

	if( bloomIntensity > 0.0f )
	{
		profiler.beginScope( "Bloom" );
		renderBloom( sceneColor, exposure, m_properties["Bloom threshold"].data<float>() );
		profiler.endScope();
	}

	profiler.beginScope( "Tonemap" );
	d3d.setBackBufferTarget();
	Parameters params = {};
	params.exposure = exposure;
	params.tonemapper = m_properties["Tonemapper (0 none, 1 ACES, 2 AgX)"].data<int32_t>();
	// Уровень 1/2 хранит сумму уровней с весами 1, f, f², …: деление на неё — Bloom Intensity как доля энергии
	float weightSum = 0.0f;
	for( uint32_t level = 0; level < bloomLevelCount; ++level )
		weightSum += std::pow( bloomLevelFalloff, static_cast<float>( level ) );
	params.bloomScale = std::max( bloomIntensity, 0.0f ) / weightSum;
	Device::updateResourceData<Parameters>( m_constantBuffer.get(), params );
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
	d3d.setSRV( SRVType::ps, 0, sceneColor );
	d3d.setSRV( SRVType::ps, 1, m_bloom[0].srv() );

	m_shader.draw();
	profiler.endScope();
}

void PostProcess::renderBloom( const com_unique_ptr<ID3D11ShaderResourceView>& sceneColor, float exposure, float threshold )
{
	DMD3D& d3d = DMD3D::instance();
	BloomParameters params = {};
	params.exposure = exposure;
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
		drawPass( m_bloomDownsample, m_bloom[level], level == 0 ? sceneColor : m_bloom[level - 1].srv(), params,
				  BlendState::opaque );
	}

	// Вверх: каждый уровень, увеличенный тентом и ослабленный весом, добавляется в соседний крупный — в 1/2 собирается
	// сумма всех уровней, у k-го — вес bloomLevelFalloff в степени k
	params.firstPass = 0;
	for( uint32_t level = bloomLevelCount - 1; level > 0; --level )
	{
		params.sourceTexelSize = XMFLOAT2( 1.0f / m_bloom[level].width(), 1.0f / m_bloom[level].height() );
		drawPass( m_bloomUpsample, m_bloom[level - 1], m_bloom[level].srv(), params, BlendState::additive );
	}
}

void PostProcess::drawPass( FullscreenShader& shader, const RenderTarget& target,
							const com_unique_ptr<ID3D11ShaderResourceView>& source, BloomParameters params, BlendState blend )
{
	DMD3D& d3d = DMD3D::instance();
	d3d.setRenderTarget( target.rtv(), target.width(), target.height() );
	Device::updateResourceData<BloomParameters>( m_bloomConstants.get(), params );
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_bloomConstants );
	d3d.setSRV( SRVType::ps, 0, source );

	shader.draw( blend );

	// Источник отвязывается: следующий проход пишет в него
	ID3D11ShaderResourceView* none = nullptr;
	d3d.GetDeviceContext()->PSSetShaderResources( 0, 1, &none );
}

PropertyContainer* PostProcess::properties()
{
	return &m_properties;
}

}
