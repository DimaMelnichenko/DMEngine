#include "SkyAtmosphere.h"
#include "Shaders\slots.h"
#include "Shaders\atmosphere_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "D3D\DMD3D.h"
#include "Light\DMLightDriver.h"
#include "Logger\Logger.h"

namespace GS
{

namespace
{

constexpr DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

float luminance( const XMFLOAT3& color )
{
	return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

// Расстояния до входа и выхода луча из сферы с центром в начале координат; y < 0 — промах или сфера позади
XMFLOAT2 raySphere( const XMVECTOR& origin, const XMVECTOR& direction, float radius )
{
	const float b = XMVectorGetX( XMVector3Dot( origin, direction ) );
	const float c = XMVectorGetX( XMVector3Dot( origin, origin ) ) - radius * radius;
	const float d = b * b - c;
	if( d < 0.0f )
		return XMFLOAT2( -1.0f, -1.0f );
	const float s = std::sqrt( d );
	return XMFLOAT2( -b - s, -b + s );
}

}

SkyAtmosphere::SkyAtmosphere() :
	SceneObject( "Sky atmosphere" )
{
}

bool SkyAtmosphere::initialize( const DMLightDriver& lights, const Settings& settings )
{
	m_lights = &lights;

	if( !m_multipleScatteringShader.load( "Shaders\\sky_multiscattering.ps" ) ||
		!m_cubeShader.load( "Shaders\\sky_cube.ps" ) ||
		!m_prefilterShader.load( "Shaders\\sky_prefilter.ps" ) ||
		!m_brdfShader.load( "Shaders\\brdf_lut.ps" ) ||
		!m_backgroundShader.load( "Shaders\\sky_background.ps" ) ||
		!m_irradianceShader.Initialize( "Shaders\\sky_irradiance.cs", "main" ) ||
		!m_aerialPerspectiveShader.Initialize( "Shaders\\aerial_perspective.cs", "main" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!createCube( skySize, 0, true, m_skyCube, m_skyTargets, m_skySRV ) ||
		!createCube( specularSize, specularMipCount, false, m_specularCube, m_specularTargets, m_specularSRV ) ||
		!createTexture2D( multipleScatteringSize, hdrFormat, m_multipleScattering, m_multipleScatteringTarget, m_multipleScatteringSRV ) ||
		!createTexture2D( brdfLutSize, DXGI_FORMAT_R16G16_FLOAT, m_brdfLut, m_brdfTarget, m_brdfSRV ) ||
		!createIrradianceBuffer() ||
		!createAerialPerspectiveVolume() ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_aerialPerspectiveConstants ) )
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

	m_properties.setName( "Sky atmosphere" );

	auto prop = m_properties.insert( "Sky intensity", settings.skyIntensity );
	prop->setLow( 0.0f );
	prop->setHigh( 10.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Haze", settings.haze );
	prop->setLow( 0.0f );
	prop->setHigh( 10.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Ground albedo", settings.groundAlbedo );
	prop->setLow( 0.0f );
	prop->setHigh( 1.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Aerial perspective view distance scale", settings.aerialPerspectiveViewDistanceScale );
	prop->setLow( 0.0f );
	prop->setHigh( 16.0f );
	prop->setControlType( GUIControlType::SLIDER );

	return true;
}

SkyAtmosphere::Settings SkyAtmosphere::settings()
{
	Settings settings;
	settings.skyIntensity = m_properties["Sky intensity"].data<float>();
	settings.haze = m_properties["Haze"].data<float>();
	settings.groundAlbedo = m_properties["Ground albedo"].data<float>();
	settings.aerialPerspectiveViewDistanceScale = m_properties["Aerial perspective view distance scale"].data<float>();
	return settings;
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

bool SkyAtmosphere::createAerialPerspectiveVolume()
{
	ID3D11Device* device = DMD3D::instance().GetDevice();

	D3D11_TEXTURE3D_DESC desc = {};
	desc.Width = AERIAL_PERSPECTIVE_SIZE;
	desc.Height = AERIAL_PERSPECTIVE_SIZE;
	desc.Depth = AERIAL_PERSPECTIVE_DEPTH;
	desc.MipLevels = 1;
	desc.Format = hdrFormat;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

	ID3D11Texture3D* texture = nullptr;
	if( FAILED( device->CreateTexture3D( &desc, nullptr, &texture ) ) )
		return false;
	m_aerialPerspective = make_com_ptr<ID3D11Texture3D>( texture );

	ID3D11UnorderedAccessView* uav = nullptr;
	if( FAILED( device->CreateUnorderedAccessView( texture, nullptr, &uav ) ) )
		return false;
	m_aerialPerspectiveUAV = make_com_ptr<ID3D11UnorderedAccessView>( uav );

	ID3D11ShaderResourceView* srv = nullptr;
	if( FAILED( device->CreateShaderResourceView( texture, nullptr, &srv ) ) )
		return false;
	m_aerialPerspectiveSRV = make_com_ptr<ID3D11ShaderResourceView>( srv );

	return true;
}

void SkyAtmosphere::setBackgroundVisible( bool visible )
{
	m_backgroundVisible = visible;
}

XMFLOAT3 SkyAtmosphere::sunTransmittance( const XMFLOAT3& toSun ) const
{
	const XMVECTOR origin = XMVectorSet( 0.0f, ATMOSPHERE_PLANET_RADIUS + ATMOSPHERE_OBSERVER_ALTITUDE, 0.0f, 0.0f );
	const XMVECTOR direction = XMVector3Normalize( XMLoadFloat3( &toSun ) );
	if( raySphere( origin, direction, ATMOSPHERE_PLANET_RADIUS ).x > 0.0f )
		return XMFLOAT3( 0.0f, 0.0f, 0.0f );

	// Оптическая толщина по лучу до края атмосферы: плотности — как atmosphereMedium в Shaders/atmosphere.sh.
	// 64 шага — сходится до тысячных и у горизонта (у шейдера неба, где это внутренний цикл, их 8)
	const float haze = m_properties["Haze"].data<float>();
	const XMFLOAT3 rayleigh( ATMOSPHERE_RAYLEIGH_SCATTERING );
	const XMFLOAT3 ozone( ATMOSPHERE_OZONE_ABSORPTION );
	constexpr int samples = 64;
	const float stepLength = raySphere( origin, direction, ATMOSPHERE_TOP_RADIUS ).y / samples;
	XMFLOAT3 depth( 0.0f, 0.0f, 0.0f );
	for( int i = 0; i < samples; ++i )
	{
		const XMVECTOR position = XMVectorMultiplyAdd( direction, XMVectorReplicate( stepLength * ( i + 0.5f ) ), origin );
		const float height = std::max( XMVectorGetX( XMVector3Length( position ) ) - ATMOSPHERE_PLANET_RADIUS, 0.0f );
		const float rayleighDensity = std::exp( -height / ATMOSPHERE_RAYLEIGH_SCALE_HEIGHT );
		const float mieDensity = std::exp( -height / ATMOSPHERE_MIE_SCALE_HEIGHT ) * haze;
		const float ozoneDensity = std::max( 0.0f, 1.0f - std::abs( height - ATMOSPHERE_OZONE_CENTER ) / ATMOSPHERE_OZONE_HALF_WIDTH );
		const float mie = ( ATMOSPHERE_MIE_SCATTERING + ATMOSPHERE_MIE_ABSORPTION ) * mieDensity;
		depth.x += ( rayleigh.x * rayleighDensity + mie + ozone.x * ozoneDensity ) * stepLength;
		depth.y += ( rayleigh.y * rayleighDensity + mie + ozone.y * ozoneDensity ) * stepLength;
		depth.z += ( rayleigh.z * rayleighDensity + mie + ozone.z * ozoneDensity ) * stepLength;
	}
	return XMFLOAT3( std::exp( -depth.x ), std::exp( -depth.y ), std::exp( -depth.z ) );
}

SkyAtmosphere::Parameters SkyAtmosphere::currentParameters() const
{
	Parameters params = {};
	XMFLOAT3 lightColor;
	m_lights->directionalLight( params.sunDirection, lightColor );
	// Небо линейно по солнцу: запекается для солнца 1 лк над атмосферой (его цветность — цвет источника, делённый
	// на яркость), а яркость — умножением на освещённость от солнца при выборке (cb_skyIlluminance). Сдвиг
	// интенсивности солнца поэтому небо не пересчитывает
	const float lightLuminance = luminance( lightColor );
	params.sunColor = lightLuminance > 0.0f ?
		XMFLOAT3( lightColor.x / lightLuminance, lightColor.y / lightLuminance, lightColor.z / lightLuminance ) :
		XMFLOAT3( 1.0f, 1.0f, 1.0f );
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

	updateAerialPerspective();
	bindEnvironment();
}

void SkyAtmosphere::updateAerialPerspective()
{
	DMD3D& d3d = DMD3D::instance();
	ID3D11DeviceContext* context = d3d.GetDeviceContext();

	// Объём станет UAV: снимаем его с входа пиксельных шейдеров прошлого кадра
	ID3D11ShaderResourceView* nullView = nullptr;
	context->PSSetShaderResources( SLOT_AERIAL_PERSPECTIVE, 1, &nullView );

	// Раскладка — AerialPerspectiveBuffer в Shaders/aerial_perspective.cs
	XMFLOAT4 constants( m_properties["Aerial perspective view distance scale"].data<float>(), 0.0f, 0.0f, 0.0f );
	Device::updateResourceData<XMFLOAT4>( m_aerialPerspectiveConstants.get(), constants );
	d3d.setConstantBuffer( SRVType::cs, 4, m_aerialPerspectiveConstants );
	Parameters params = m_computedFor;
	Device::updateResourceData<Parameters>( m_constantBuffer.get(), params );
	d3d.setConstantBuffer( SRVType::cs, SLOT_CB_PASS, m_constantBuffer );
	d3d.setSRV( SRVType::cs, 1, m_multipleScatteringSRV );

	// Поток — столбец объёма: идёт от камеры по слоям и пишет накопленное к концу каждого
	m_aerialPerspectiveShader.setUAVBuffer( 0, m_aerialPerspectiveUAV.get() );
	constexpr uint32_t groupSize = 8;	// = numthreads в Shaders/aerial_perspective.cs
	m_aerialPerspectiveShader.dispatchGroups( AERIAL_PERSPECTIVE_SIZE / groupSize, AERIAL_PERSPECTIVE_SIZE / groupSize, 1 );
	context->CSSetShaderResources( 1, 1, &nullView );
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
	d3d.setSRV( SRVType::ps, SLOT_AERIAL_PERSPECTIVE, m_aerialPerspectiveSRV );
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
	// На дальней плоскости: только там, где сцена ничего не нарисовала
	m_backgroundShader.draw( BlendState::opaque, DepthState::readOnlyLessEqual );
}

PropertyContainer* SkyAtmosphere::properties()
{
	return &m_properties;
}

}
