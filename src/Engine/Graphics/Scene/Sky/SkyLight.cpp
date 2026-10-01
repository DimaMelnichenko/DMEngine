#include "SkyLight.h"
#include "Shaders\slots.h"
#include <algorithm>
#include "D3D\DMD3D.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Источник — во float32: ночью небо в единицах запекания (на 1 лк солнца) — ~10⁻⁶…10⁻⁹, в половинной точности это
// ноль. Результат (его читает каждый освещённый пиксель) — в половинной, делённый на нормировку пересчёта
constexpr DXGI_FORMAT sourceFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT resultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

}

bool SkyLight::initialize()
{
	if( !m_prefilterShader.load( "Shaders\\sky_prefilter.ps", TargetFormats::colorTarget( resultFormat ) ) ||
		!m_brdfShader.load( "Shaders\\brdf_lut.ps", TargetFormats::colorTarget( DXGI_FORMAT_R16G16_FLOAT ) ) ||
		!m_blendShader.load( "Shaders\\sky_light_blend.ps", TargetFormats::colorTarget( resultFormat ) ) ||
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) ||
		!m_irradianceBlendShader.Initialize( "Shaders\\sky_irradiance_blend.cs", "main" ) ||
		!m_mipShader.Initialize( "Shaders\\cube_downsample.cs", "main" ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	return d3d.createShaderConstantBuffer( sizeof( PrefilterParameters ), m_constantBuffer ) &&
		   d3d.createShaderConstantBuffer( sizeof( BlendParameters ), m_blendConstants ) &&
		   d3d.createShaderConstantBuffer( sizeof( BlendParameters ), m_irradianceBlendConstants ) &&
		   d3d.createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_irradianceConstants ) &&
		   createResult( m_results[0] ) && createResult( m_results[1] ) && createResult( m_results[2] ) &&
		   createResult( m_blended ) && m_brdfLut.create( brdfLutSize, brdfLutSize, DXGI_FORMAT_R16G16_FLOAT, "BRDF LUT" );
}

bool SkyLight::createSource( CubeTarget& source )
{
	return source.create( sourceSize, 0, sourceFormat, true, "Sky light source cube" ) && source.createFacesView( irradianceSourceMip );
}

bool SkyLight::createResult( Result& result )
{
	if( !result.specular.create( specularSize, specularMipCount, resultFormat, false, "Sky light specular" ) )
		return false;

	// Гармоники: 9 коэффициентов XMFLOAT4 — пишет compute, читают пиксельные шейдеры
	BufferDesc desc;
	desc.size = 9 * sizeof( XMFLOAT4 );
	desc.stride = sizeof( XMFLOAT4 );
	desc.usage = BufferUsage::unorderedAccess | BufferUsage::shaderResource | BufferUsage::structured;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createBuffer( desc, nullptr, result.irradianceBuffer ) ||
		!d3d.createStorageView( result.irradianceBuffer, {}, result.irradianceUAV ) ||
		!d3d.createShaderView( result.irradianceBuffer, {}, result.irradianceSRV ) )
		return false;
	d3d.setName( result.irradianceBuffer, "Sky light irradiance SH" );

	return true;
}

void SkyLight::capture( const CubeTarget& source, float normalization )
{
	beginCapture( source, normalization );
	while( !updateCapture() )
	{
	}
	// Сразу целиком — без перехода от прежнего
	m_previous = m_current;
	m_blendFrame = blendFrames;
}

void SkyLight::beginCapture( const CubeTarget& source, float normalization )
{
	buildMips( source );
	m_source = &source;
	m_step = 0;
	m_captureNormalization = normalization > 0.0f ? normalization : 1.0f;
	m_results[m_building].normalization = m_captureNormalization;
}

bool SkyLight::updateCapture()
{
	if( !m_source )
		return true;

	Result& result = m_results[m_building];
	if( m_step == 0 )
		captureIrradiance( result );
	else
		prefilterFace( result, static_cast<int32_t>( m_step - 1 ) );

	// Таблица BRDF не зависит от окружения — один раз
	if( !m_brdfReady )
	{
		DMD3D::instance().beginPass( PassDesc{ "BRDF LUT", { { &m_brdfLut.target(), "BRDF LUT" } }, {}, brdfLutSize, brdfLutSize } );
		m_brdfShader.draw();
		m_brdfReady = true;
	}
	if( ++m_step < captureSteps )
		return false;

	// Готово целиком: переход от текущего к новому; пересчёт дальше — в третий результат
	const uint32_t built = m_building;
	m_previous = m_current;
	m_current = built;
	m_building = m_previous != m_current ? 3 - m_previous - m_current : ( m_current + 1 ) % 3;
	m_blendFrame = 0;
	m_source = nullptr;
	return true;
}

void SkyLight::buildMips( const CubeTarget& source )
{
	DMD3D& d3d = DMD3D::instance();
	for( uint32_t mip = 1; mip < source.mipCount(); ++mip )
	{
		d3d.beginPass( PassDesc{ "Sky cube mip", {}, {}, 0, 0, { { &source.mipView( mip - 1 ), "sky cube mip N" } },
								 { { &source.mipStorage( mip ), "sky cube mip N + 1" } } } );
		d3d.setSRV( 0, source.mipView( mip - 1 ) );
		m_mipShader.setUAVBuffer( 0, source.mipStorage( mip ) );
		const uint32_t groups = ( source.mipSize( mip ) + 7 ) / 8;
		m_mipShader.dispatchGroups( groups, groups, 6 );
	}
}

void SkyLight::captureIrradiance( Result& result )
{
	// Рассеянный свет: гармоники по мипу irradianceSourceMip, делённые на нормировку
	XMFLOAT4 constants( 1.0f / m_captureNormalization, 0.0f, 0.0f, 0.0f );
	Device::updateResourceData<XMFLOAT4>( m_irradianceConstants, constants );
	DMD3D::instance().setConstantBuffer( 4, m_irradianceConstants );
	DMD3D::instance().beginPass( PassDesc{ "Sky light irradiance", {}, {}, 0, 0, { { &m_source->facesSRV(), "sky cube faces" } },
										 { { &result.irradianceUAV, "irradiance SH" } } } );
	DMD3D::instance().setSRV( 0, m_source->facesSRV() );
	m_irradianceShader.setUAVBuffer( 0, result.irradianceUAV );
	m_irradianceShader.Dispatch( 64u, 0.0f );
}

void SkyLight::prefilterFace( Result& result, int32_t face )
{
	// Отражения: мип m — шероховатость m / (specularMipCount − 1)
	DMD3D& d3d = DMD3D::instance();
	PrefilterParameters params = {};
	params.face = face;
	params.sourceSize = static_cast<float>( m_source->size() );
	params.outputScale = 1.0f / m_captureNormalization;
	for( uint32_t mip = 0; mip < specularMipCount; ++mip )
	{
		params.roughness = static_cast<float>( mip ) / ( specularMipCount - 1 );
		Device::updateResourceData<PrefilterParameters>( m_constantBuffer, params );
		d3d.setConstantBuffer( SLOT_CB_PASS, m_constantBuffer );
		const uint32_t size = result.specular.mipSize( mip );
		d3d.beginPass( PassDesc{ "Sky light prefilter", { { &result.specular.target( mip, face ), "specular cube" } }, {}, size, size,
								 { { &m_source->srv(), "sky cube" } } } );
		d3d.setSRV( 0, m_source->srv() );
		m_prefilterShader.draw();
	}
}

void SkyLight::blendResults( float blend )
{
	// В нормировке текущего: прежний пересчитывается из своей
	const float previousWeight = ( 1.0f - blend ) * m_results[m_previous].normalization / m_results[m_current].normalization;
	DMD3D& d3d = DMD3D::instance();
	const Result& previous = m_results[m_previous];
	const Result& current = m_results[m_current];

	// Гармоники: 9 коэффициентов одной группой
	BlendParameters params = {};
	params.previousWeight = previousWeight;
	params.currentWeight = blend;
	Device::updateResourceData<BlendParameters>( m_irradianceBlendConstants, params );
	d3d.setConstantBuffer( 4, m_irradianceBlendConstants );
	d3d.beginPass( PassDesc{ "Sky light blend irradiance", {}, {}, 0, 0,
							 { { &previous.irradianceSRV, "previous irradiance SH" }, { &current.irradianceSRV, "current irradiance SH" } },
							 { { &m_blended.irradianceUAV, "blended irradiance SH" } } } );
	d3d.setSRV( 0, previous.irradianceSRV );
	d3d.setSRV( 1, current.irradianceSRV );
	m_irradianceBlendShader.setUAVBuffer( 0, m_blended.irradianceUAV );
	m_irradianceBlendShader.dispatchGroups( 1, 1, 1 );

	// Префильтр отражений: грань за гранью, все мипы
	const std::vector<PassDesc::Read> specularReads = { { &previous.specular.srv(), "previous specular cube" },
													   { &current.specular.srv(), "current specular cube" } };
	for( int32_t face = 0; face < 6; ++face )
	{
		for( uint32_t mip = 0; mip < specularMipCount; ++mip )
		{
			params.face = face;
			params.mip = static_cast<float>( mip );
			Device::updateResourceData<BlendParameters>( m_blendConstants, params );
			d3d.setConstantBuffer( SLOT_CB_PASS, m_blendConstants );
			const uint32_t size = m_blended.specular.mipSize( mip );
			d3d.beginPass( PassDesc{ "Sky light blend specular", { { &m_blended.specular.target( mip, face ), "blended specular cube" } }, {},
									 size, size, specularReads } );
			d3d.setSRV( 0, previous.specular.srv() );
			d3d.setSRV( 1, current.specular.srv() );
			m_blendShader.draw();
		}
	}
}

void SkyLight::bind()
{
	DMD3D& d3d = DMD3D::instance();
	// Доля нового — с первого же кадра после готовности, чтобы на стыке переходов свет не стоял кадр на месте
	const Result* shown = &m_results[m_current];
	if( m_blendFrame < blendFrames )
	{
		++m_blendFrame;
		if( m_blendFrame < blendFrames )
		{
			blendResults( static_cast<float>( m_blendFrame ) / blendFrames );
			shown = &m_blended;
		}
	}
	const Result& result = *shown;
	d3d.setSRV( SLOT_IBL_IRRADIANCE, result.irradianceSRV );
	d3d.setSRV( SLOT_IBL_SPECULAR, result.specular.srv() );
	d3d.setSRV( SLOT_IBL_BRDF, m_brdfLut.srv() );
}

}
