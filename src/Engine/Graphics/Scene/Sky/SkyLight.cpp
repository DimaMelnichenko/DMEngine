#include "SkyLight.h"
#include "Shaders\slots.h"
#include <algorithm>
#include "D3D\DMD3D.h"

namespace GS
{

namespace
{

constexpr DXGI_FORMAT environmentFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

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
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) )
		return false;

	return DMD3D::instance().createShaderConstantBuffer( sizeof( PrefilterParameters ), m_constantBuffer ) &&
		   createResult( m_results[0] ) && createResult( m_results[1] ) &&
		   m_brdfLut.create( brdfLutSize, brdfLutSize, DXGI_FORMAT_R16G16_FLOAT );
}

bool SkyLight::createSource( CubeTarget& source )
{
	return source.create( sourceSize, 0, environmentFormat, true ) && source.createFacesView( irradianceSourceMip );
}

bool SkyLight::createResult( Result& result )
{
	if( !result.specular.create( specularSize, specularMipCount, environmentFormat, false ) )
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

void SkyLight::capture( const CubeTarget& source )
{
	beginCapture( source );
	while( !updateCapture() )
	{
	}
}

void SkyLight::beginCapture( const CubeTarget& source )
{
	m_source = &source;
	m_step = 0;
}

bool SkyLight::updateCapture()
{
	if( !m_source )
		return true;

	unbindEnvironment();
	Result& result = m_results[1 - m_front];
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

	// Готово целиком: новый результат вместо прежнего
	m_front = 1 - m_front;
	m_source = nullptr;
	return true;
}

void SkyLight::captureIrradiance( Result& result )
{
	// Рассеянный свет: гармоники по мипу irradianceSourceMip
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

void SkyLight::bind()
{
	DMD3D& d3d = DMD3D::instance();
	const Result& result = m_results[m_front];
	d3d.setSRV( SRVType::ps, SLOT_IBL_IRRADIANCE, result.irradianceSRV );
	d3d.setSRV( SRVType::ps, SLOT_IBL_SPECULAR, result.specular.srv() );
	d3d.setSRV( SRVType::ps, SLOT_IBL_BRDF, m_brdfLut.srv() );
}

}
