#include "SkyLight.h"
#include "Shaders\slots.h"
#include <algorithm>
#include "D3D\DMD3D.h"

namespace GS
{

namespace
{

constexpr DXGI_FORMAT environmentFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

}

bool SkyLight::initialize()
{
	if( !m_prefilterShader.load( "Shaders\\sky_prefilter.ps" ) ||
		!m_brdfShader.load( "Shaders\\brdf_lut.ps" ) ||
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) )
		return false;

	return DMD3D::instance().createShaderConstantBuffer( sizeof( PrefilterParameters ), m_constantBuffer ) &&
		   m_specular.create( specularSize, specularMipCount, environmentFormat, false ) &&
		   m_brdfLut.create( brdfLutSize, brdfLutSize, DXGI_FORMAT_R16G16_FLOAT ) &&
		   createIrradianceBuffer();
}

bool SkyLight::createSource( CubeTarget& source )
{
	return source.create( sourceSize, 0, environmentFormat, true ) && source.createFacesView( irradianceSourceMip );
}

bool SkyLight::createIrradianceBuffer()
{
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
	m_irradianceBuffer = make_com_ptr<ID3D11Buffer>( buffer );

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = 9;
	ID3D11UnorderedAccessView* uav = nullptr;
	if( FAILED( device->CreateUnorderedAccessView( m_irradianceBuffer.get(), &uavDesc, &uav ) ) )
		return false;
	m_irradianceUAV = make_com_ptr<ID3D11UnorderedAccessView>( uav );

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = 9;
	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( device->CreateShaderResourceView( m_irradianceBuffer.get(), &srvDesc, &srv ) ) )
		return false;
	m_irradianceSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return true;
}

void SkyLight::capture( const CubeTarget& source )
{
	DMD3D& d3d = DMD3D::instance();
	ID3D11DeviceContext* context = d3d.GetDeviceContext();

	// Свои ресурсы станут целями и UAV: снимаем их с входов, иначе D3D отвяжет их с предупреждением
	ID3D11ShaderResourceView* nullViews[3] = {};
	context->PSSetShaderResources( 0, 1, nullViews );
	context->PSSetShaderResources( SLOT_IBL_IRRADIANCE, 3, nullViews );

	// 1. Рассеянный свет: гармоники по мипу irradianceSourceMip
	d3d.setSRV( SRVType::cs, 0, source.facesSRV() );
	m_irradianceShader.setUAVBuffer( 0, m_irradianceUAV.get() );
	m_irradianceShader.Dispatch( 64u, 0.0f );
	context->CSSetShaderResources( 0, 1, nullViews );

	// 2. Отражения: мип m — шероховатость m / (specularMipCount − 1)
	d3d.setSRV( SRVType::ps, 0, source.srv() );
	PrefilterParameters params = {};
	params.sourceSize = static_cast<float>( source.size() );
	for( uint32_t mip = 0; mip < specularMipCount; ++mip )
	{
		params.roughness = static_cast<float>( mip ) / ( specularMipCount - 1 );
		for( int32_t face = 0; face < 6; ++face )
		{
			params.face = face;
			Device::updateResourceData<PrefilterParameters>( m_constantBuffer.get(), params );
			d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
			const uint32_t size = m_specular.mipSize( mip );
			d3d.setRenderTarget( m_specular.rtv( mip, face ), size, size );
			m_prefilterShader.draw();
		}
	}

	// 3. Таблица BRDF не зависит от окружения — один раз
	if( !m_brdfReady )
	{
		d3d.setRenderTarget( m_brdfLut.rtv(), brdfLutSize, brdfLutSize );
		m_brdfShader.draw();
		m_brdfReady = true;
	}

	context->OMSetRenderTargets( 0, nullptr, nullptr );
	context->PSSetShaderResources( 0, 1, nullViews );
}

void SkyLight::bind()
{
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( SRVType::ps, SLOT_IBL_IRRADIANCE, m_irradianceSRV );
	d3d.setSRV( SRVType::ps, SLOT_IBL_SPECULAR, m_specular.srv() );
	d3d.setSRV( SRVType::ps, SLOT_IBL_BRDF, m_brdfLut.srv() );
}

}
