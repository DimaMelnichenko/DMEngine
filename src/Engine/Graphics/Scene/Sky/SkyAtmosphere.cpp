#include "SkyAtmosphere.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cstring>
#include "D3D\DMD3D.h"
#include "Light\DMLightDriver.h"
#include "ResourceMetaFile.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

}

SkyAtmosphere::SkyAtmosphere() :
	SceneObject( "Sky atmosphere" )
{
}

bool SkyAtmosphere::initialize( const DMLightDriver& lights, const std::string& settingsFile )
{
	m_lights = &lights;

	if( !m_multipleScatteringShader.load( "Shaders\\sky_multiscattering.ps" ) ||
		!m_cubeShader.load( "Shaders\\sky_cube.ps" ) ||
		!m_prefilterShader.load( "Shaders\\sky_prefilter.ps" ) ||
		!m_brdfShader.load( "Shaders\\brdf_lut.ps" ) ||
		!m_backgroundShader.load( "Shaders\\sky_background.ps" ) ||
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!createCube( skySize, 0, true, m_skyCube, m_skyTargets, m_skySRV ) ||
		!createCube( specularSize, specularMipCount, false, m_specularCube, m_specularTargets, m_specularSRV ) ||
		!createTexture2D( multipleScatteringSize, hdrFormat, m_multipleScattering, m_multipleScatteringTarget, m_multipleScatteringSRV ) ||
		!createTexture2D( brdfLutSize, DXGI_FORMAT_R16G16_FLOAT, m_brdfLut, m_brdfTarget, m_brdfSRV ) ||
		!createIrradianceBuffer() )
		return false;

	// Грани неба одного мипа как массив: compute-шейдер читает тексели через Load
	D3D11_SHADER_RESOURCE_VIEW_DESC facesDesc = {};
	facesDesc.Format = hdrFormat;
	facesDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	facesDesc.Texture2DArray.MostDetailedMip = irradianceSourceMip;
	facesDesc.Texture2DArray.MipLevels = 1;
	facesDesc.Texture2DArray.FirstArraySlice = 0;
	facesDesc.Texture2DArray.ArraySize = 6;
	ID3D11ShaderResourceView* facesSRV = nullptr;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( m_skyCube.get(), &facesDesc, &facesSRV ) ) )
		return false;
	m_skyFacesSRV = make_com_ptr<ID3D11ShaderResourceView>( facesSRV );

	// Без секции [Sky] — значения по умолчанию
	ResourceMetaFile settings( settingsFile );
	auto setting = [&]( const char* key, float fallback )
	{
		const std::string value = settings.get<std::string>( "Sky", key );
		return value.empty() ? fallback : static_cast<float>( std::atof( value.c_str() ) );
	};

	m_properties.setName( "Sky atmosphere" );

	auto prop = m_properties.insert( "Sky intensity", setting( "SkyIntensity", 1.0f ) );
	prop->setLow( 0.0f );
	prop->setHigh( 10.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Haze", setting( "Haze", 1.0f ) );
	prop->setLow( 0.0f );
	prop->setHigh( 10.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Ground albedo", setting( "GroundAlbedo", 0.25f ) );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop->setControlType( GUIControlType::SLIDER );

	return true;
}

bool SkyAtmosphere::createCube( uint32_t size, uint32_t mipCount, bool generateMips, com_unique_ptr<ID3D11Texture2D>& texture,
								std::vector<com_unique_ptr<ID3D11RenderTargetView>>& targets, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = size;
	desc.Height = size;
	desc.MipLevels = mipCount;	// 0 — полная цепочка
	desc.ArraySize = 6;
	desc.Format = hdrFormat;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | ( generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0 );

	ID3D11Texture2D* rawTexture = nullptr;
	if( FAILED( device->CreateTexture2D( &desc, nullptr, &rawTexture ) ) )
		return false;
	texture = make_com_ptr<ID3D11Texture2D>( rawTexture );
	texture->GetDesc( &desc );

	// Цель рендера на каждую грань каждого мипа: индекс = мип × 6 + грань
	targets.clear();
	for( uint32_t mip = 0; mip < desc.MipLevels; ++mip )
	{
		for( uint32_t face = 0; face < 6; ++face )
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = hdrFormat;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray.MipSlice = mip;
			rtvDesc.Texture2DArray.FirstArraySlice = face;
			rtvDesc.Texture2DArray.ArraySize = 1;
			ID3D11RenderTargetView* rtv = nullptr;
			if( FAILED( device->CreateRenderTargetView( texture.get(), &rtvDesc, &rtv ) ) )
				return false;
			targets.push_back( make_com_ptr<ID3D11RenderTargetView>( rtv ) );
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = hdrFormat;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = desc.MipLevels;
	ID3D11ShaderResourceView* rawSRV = nullptr;
	if( FAILED( device->CreateShaderResourceView( texture.get(), &srvDesc, &rawSRV ) ) )
		return false;
	srv = make_com_ptr<ID3D11ShaderResourceView>( rawSRV );

	return true;
}

bool SkyAtmosphere::createTexture2D( uint32_t size, DXGI_FORMAT format, com_unique_ptr<ID3D11Texture2D>& texture,
									 com_unique_ptr<ID3D11RenderTargetView>& target, com_unique_ptr<ID3D11ShaderResourceView>& srv )
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = size;
	desc.Height = size;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture2D* rawTexture = nullptr;
	if( FAILED( device->CreateTexture2D( &desc, nullptr, &rawTexture ) ) )
		return false;
	texture = make_com_ptr<ID3D11Texture2D>( rawTexture );

	ID3D11RenderTargetView* rtv = nullptr;
	if( FAILED( device->CreateRenderTargetView( texture.get(), nullptr, &rtv ) ) )
		return false;
	target = make_com_ptr<ID3D11RenderTargetView>( rtv );

	ID3D11ShaderResourceView* rawSRV = nullptr;
	if( FAILED( device->CreateShaderResourceView( texture.get(), nullptr, &rawSRV ) ) )
		return false;
	srv = make_com_ptr<ID3D11ShaderResourceView>( rawSRV );

	return true;
}

bool SkyAtmosphere::createIrradianceBuffer()
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

void SkyAtmosphere::setBackgroundVisible( bool visible )
{
	m_backgroundVisible = visible;
}

SkyAtmosphere::Parameters SkyAtmosphere::currentParameters() const
{
	Parameters params = {};
	XMFLOAT3 lightColor;
	m_lights->directionalLight( params.sunDirection, lightColor );
	// Небо освещает белое солнце той же яркости: цвет источника — уже прошедший атмосферу свет у земли
	const float sunLuminance = 0.2126f * lightColor.x + 0.7152f * lightColor.y + 0.0722f * lightColor.z;
	params.sunColor = XMFLOAT3( sunLuminance, sunLuminance, sunLuminance );
	params.skyIntensity = m_properties["Sky intensity"].data<float>();
	params.haze = m_properties["Haze"].data<float>();
	const float albedo = m_properties["Ground albedo"].data<float>();
	params.groundAlbedo = XMFLOAT3( albedo, albedo, albedo );
	return params;
}

void SkyAtmosphere::compute( const FrameContext& )
{
	// Пересчёт только при смене солнца или настроек: cubemap неба, гармоники и префильтр — несколько миллисекунд
	const Parameters params = currentParameters();
	if( !m_environmentValid || std::memcmp( &params, &m_computedFor, sizeof( Parameters ) ) != 0 )
	{
		updateEnvironment( params );
		m_computedFor = params;
		m_environmentValid = true;
	}

	bindEnvironment();
}

void SkyAtmosphere::updateEnvironment( const Parameters& params )
{
	DMD3D& d3d = DMD3D::instance();
	ID3D11DeviceContext* context = d3d.GetDeviceContext();

	// Ресурсы окружения станут целями рендера: снимаем их с входов, иначе D3D отвяжет их с предупреждением
	ID3D11ShaderResourceView* nullViews[4] = {};
	context->PSSetShaderResources( 0, 4, nullViews );
	context->PSSetShaderResources( SLOT_IBL_IRRADIANCE, 3, nullViews );

	Parameters faceParams = params;
	setParameters( faceParams );

	// 1. Многократное рассеяние: таблица Ψ по высоте и зенитному углу солнца
	d3d.setRenderTarget( m_multipleScatteringTarget.get(), multipleScatteringSize, multipleScatteringSize );
	m_multipleScatteringShader.draw();
	context->OMSetRenderTargets( 0, nullptr, nullptr );
	d3d.setSRV( SRVType::ps, 1, m_multipleScatteringSRV );

	// 2. Небо: грани мипа 0, затем цепочка мипов (для префильтра с выборкой мипа по плотности)
	for( int32_t face = 0; face < 6; ++face )
	{
		faceParams.face = face;
		setParameters( faceParams );
		d3d.setRenderTarget( m_skyTargets[face].get(), skySize, skySize );
		m_cubeShader.draw();
	}
	context->OMSetRenderTargets( 0, nullptr, nullptr );
	context->PSSetShaderResources( 1, 1, nullViews );
	context->GenerateMips( m_skySRV.get() );

	// 3. Рассеянный свет: гармоники по мипу 32 × 32
	d3d.setSRV( SRVType::cs, 0, m_skyFacesSRV );
	m_irradianceShader.setUAVBuffer( 0, m_irradianceUAV.get() );
	m_irradianceShader.Dispatch( 64u, 0.0f );
	ID3D11ShaderResourceView* nullView = nullptr;
	context->CSSetShaderResources( 0, 1, &nullView );

	// 4. Отражения: мип m — шероховатость m / (specularMipCount − 1)
	d3d.setSRV( SRVType::ps, 0, m_skySRV );
	faceParams.sourceSize = static_cast<float>( skySize );
	for( uint32_t mip = 0; mip < specularMipCount; ++mip )
	{
		faceParams.roughness = static_cast<float>( mip ) / ( specularMipCount - 1 );
		for( int32_t face = 0; face < 6; ++face )
		{
			faceParams.face = face;
			setParameters( faceParams );
			const uint32_t size = std::max( specularSize >> mip, 1u );
			d3d.setRenderTarget( m_specularTargets[mip * 6 + face].get(), size, size );
			m_prefilterShader.draw();
		}
	}

	// 5. Таблица BRDF не зависит от неба — один раз
	if( !m_brdfReady )
	{
		d3d.setRenderTarget( m_brdfTarget.get(), brdfLutSize, brdfLutSize );
		m_brdfShader.draw();
		m_brdfReady = true;
	}

	context->OMSetRenderTargets( 0, nullptr, nullptr );
	context->PSSetShaderResources( 0, 1, nullViews );
}

void SkyAtmosphere::setParameters( const Parameters& params )
{
	Parameters data = params;
	Device::updateResourceData<Parameters>( m_constantBuffer.get(), data );
	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
}

void SkyAtmosphere::bindEnvironment()
{
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( SRVType::ps, SLOT_IBL_IRRADIANCE, m_irradianceSRV );
	d3d.setSRV( SRVType::ps, SLOT_IBL_SPECULAR, m_specularSRV );
	d3d.setSRV( SRVType::ps, SLOT_IBL_BRDF, m_brdfSRV );
}

void SkyAtmosphere::collectMeshes( const RenderView&, MeshCollector& collector )
{
	if( m_backgroundVisible )
		collector.addCustom( passBit( MeshPass::sky ) );
}

void SkyAtmosphere::renderCustom( const RenderContext& )
{

	setParameters( m_computedFor );
	DMD3D::instance().setSRV( SRVType::ps, 0, m_skySRV );
	m_backgroundShader.draw();
}

PropertyContainer* SkyAtmosphere::properties()
{
	return &m_properties;
}

}
