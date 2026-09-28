#include "SkyLight.h"
#include "Shaders\slots.h"
#include <algorithm>
#include "D3D\DMD3D.h"

namespace GS
{

namespace
{

// Источник — во float32: ночью небо в единицах запекания (на 1 лк солнца) — ~10⁻⁶…10⁻⁹, в половинной точности это
// ноль. Результат (его читает каждый освещённый пиксель) — в половинной, делённый на нормировку пересчёта
constexpr DXGI_FORMAT sourceFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
constexpr DXGI_FORMAT resultFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

// Ресурсы освещения окружением и источник станут целями и UAV: снимаем их со входов пиксельных шейдеров, иначе D3D
// отвяжет их с предупреждением
void unbindEnvironment()
{
	ID3D11ShaderResourceView* nullViews[3] = {};
	ID3D11DeviceContext* context = DMD3D::instance().GetDeviceContext();
	context->PSSetShaderResources( 0, 1, nullViews );
	context->PSSetShaderResources( SLOT_IBL_IRRADIANCE, 3, nullViews );
}

}

bool SkyLight::initialize()
{
	if( !m_prefilterShader.load( "Shaders\\sky_prefilter.ps" ) ||
		!m_brdfShader.load( "Shaders\\brdf_lut.ps" ) ||
		!m_blendShader.load( "Shaders\\sky_light_blend.ps" ) ||
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) ||
		!m_irradianceBlendShader.Initialize( "Shaders\\sky_irradiance_blend.cs", "main" ) )
		return false;

	DMD3D& d3d = DMD3D::instance();
	return d3d.createShaderConstantBuffer( sizeof( PrefilterParameters ), m_constantBuffer ) &&
		   d3d.createShaderConstantBuffer( sizeof( BlendParameters ), m_blendConstants ) &&
		   d3d.createShaderConstantBuffer( sizeof( BlendParameters ), m_irradianceBlendConstants ) &&
		   d3d.createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_irradianceConstants ) &&
		   createResult( m_results[0] ) && createResult( m_results[1] ) && createResult( m_results[2] ) &&
		   createResult( m_blended ) && m_brdfLut.create( brdfLutSize, brdfLutSize, DXGI_FORMAT_R16G16_FLOAT );
}

bool SkyLight::createSource( CubeTarget& source )
{
	return source.create( sourceSize, 0, sourceFormat, true ) && source.createFacesView( irradianceSourceMip );
}

bool SkyLight::createResult( Result& result )
{
	if( !result.specular.create( specularSize, specularMipCount, resultFormat, false ) )
		return false;

	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = 9 * sizeof( XMFLOAT4 );
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof( XMFLOAT4 );

	ID3D11Buffer* buffer = nullptr;
	if( FAILED( device->CreateBuffer( &desc, nullptr, &buffer ) ) )
		return false;
	result.irradianceBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = 9;
	ID3D11UnorderedAccessView* uav = nullptr;
	if( FAILED( device->CreateUnorderedAccessView( result.irradianceBuffer.get(), &uavDesc, &uav ) ) )
		return false;
	result.irradianceUAV = make_com_ptr<ID3D11UnorderedAccessView>( uav );

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = 9;
	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( device->CreateShaderResourceView( result.irradianceBuffer.get(), &srvDesc, &srv ) ) )
		return false;
	result.irradianceSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );

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
	m_source = &source;
	m_step = 0;
	m_captureNormalization = normalization > 0.0f ? normalization : 1.0f;
	m_results[m_building].normalization = m_captureNormalization;
}

bool SkyLight::updateCapture()
{
	if( !m_source )
		return true;

	unbindEnvironment();
	Result& result = m_results[m_building];
	if( m_step == 0 )
		captureIrradiance( result );
	else
		prefilterFace( result, static_cast<int32_t>( m_step - 1 ) );

	// Таблица BRDF не зависит от окружения — один раз
	if( !m_brdfReady )
	{
		DMD3D::instance().setRenderTarget( m_brdfLut.rtv(), brdfLutSize, brdfLutSize );
		m_brdfShader.draw();
		m_brdfReady = true;
	}
	DMD3D::instance().GetDeviceContext()->OMSetRenderTargets( 0, nullptr, nullptr );

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

void SkyLight::captureIrradiance( Result& result )
{
	// Рассеянный свет: гармоники по мипу irradianceSourceMip, делённые на нормировку
	XMFLOAT4 constants( 1.0f / m_captureNormalization, 0.0f, 0.0f, 0.0f );
	Device::updateResourceData<XMFLOAT4>( m_irradianceConstants.get(), constants );
	DMD3D::instance().setConstantBuffer( SRVType::cs, 4, m_irradianceConstants );
	DMD3D::instance().setSRV( SRVType::cs, 0, m_source->facesSRV() );
	m_irradianceShader.setUAVBuffer( 0, result.irradianceUAV.get() );
	m_irradianceShader.Dispatch( 64u, 0.0f );
	ID3D11ShaderResourceView* nullView = nullptr;
	DMD3D::instance().GetDeviceContext()->CSSetShaderResources( 0, 1, &nullView );
}

void SkyLight::prefilterFace( Result& result, int32_t face )
{
	// Отражения: мип m — шероховатость m / (specularMipCount − 1)
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( SRVType::ps, 0, m_source->srv() );
	PrefilterParameters params = {};
	params.face = face;
	params.sourceSize = static_cast<float>( m_source->size() );
	params.outputScale = 1.0f / m_captureNormalization;
	for( uint32_t mip = 0; mip < specularMipCount; ++mip )
	{
		params.roughness = static_cast<float>( mip ) / ( specularMipCount - 1 );
		Device::updateResourceData<PrefilterParameters>( m_constantBuffer.get(), params );
		d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
		const uint32_t size = result.specular.mipSize( mip );
		d3d.setRenderTarget( result.specular.rtv( mip, face ), size, size );
		m_prefilterShader.draw();
	}
	ID3D11ShaderResourceView* nullView = nullptr;
	d3d.GetDeviceContext()->PSSetShaderResources( 0, 1, &nullView );
}

void SkyLight::blendResults( float blend )
{
	// В нормировке текущего: прежний пересчитывается из своей
	const float previousWeight = ( 1.0f - blend ) * m_results[m_previous].normalization / m_results[m_current].normalization;
	DMD3D& d3d = DMD3D::instance();
	ID3D11DeviceContext* context = d3d.GetDeviceContext();
	unbindEnvironment();
	const Result& previous = m_results[m_previous];
	const Result& current = m_results[m_current];

	// Гармоники: 9 коэффициентов одной группой
	BlendParameters params = {};
	params.previousWeight = previousWeight;
	params.currentWeight = blend;
	Device::updateResourceData<BlendParameters>( m_irradianceBlendConstants.get(), params );
	d3d.setConstantBuffer( SRVType::cs, 4, m_irradianceBlendConstants );
	d3d.setSRV( SRVType::cs, 0, previous.irradianceSRV );
	d3d.setSRV( SRVType::cs, 1, current.irradianceSRV );
	m_irradianceBlendShader.setUAVBuffer( 0, m_blended.irradianceUAV.get() );
	m_irradianceBlendShader.dispatchGroups( 1, 1, 1 );
	ID3D11ShaderResourceView* nullViews[2] = {};
	context->CSSetShaderResources( 0, 2, nullViews );

	// Префильтр отражений: грань за гранью, все мипы
	d3d.setSRV( SRVType::ps, 0, previous.specular.srv() );
	d3d.setSRV( SRVType::ps, 1, current.specular.srv() );
	for( int32_t face = 0; face < 6; ++face )
	{
		for( uint32_t mip = 0; mip < specularMipCount; ++mip )
		{
			params.face = face;
			params.mip = static_cast<float>( mip );
			Device::updateResourceData<BlendParameters>( m_blendConstants.get(), params );
			d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_blendConstants );
			const uint32_t size = m_blended.specular.mipSize( mip );
			d3d.setRenderTarget( m_blended.specular.rtv( mip, face ), size, size );
			m_blendShader.draw();
		}
	}
	context->PSSetShaderResources( 0, 2, nullViews );
	context->OMSetRenderTargets( 0, nullptr, nullptr );
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
	d3d.setSRV( SRVType::ps, SLOT_IBL_IRRADIANCE, result.irradianceSRV );
	d3d.setSRV( SRVType::ps, SLOT_IBL_SPECULAR, result.specular.srv() );
	d3d.setSRV( SRVType::ps, SLOT_IBL_BRDF, m_brdfLut.srv() );
}

}
