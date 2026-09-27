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

bool SkyAtmosphere::initialize( const DMLightDriver& lights, const Settings& settings, SkyLight& skyLight )
{
	m_lights = &lights;
	m_skyLight = &skyLight;

	if( !m_multipleScatteringShader.load( "Shaders\\sky_multiscattering.ps" ) ||
		!m_cubeShader.load( "Shaders\\sky_cube.ps" ) ||
		!m_backgroundShader.load( "Shaders\\sky_background.ps" ) ||
		!m_aerialPerspectiveShader.Initialize( "Shaders\\aerial_perspective.cs", "main" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!SkyLight::createSource( m_skyCube ) ||
		!m_multipleScattering.create( multipleScatteringSize, multipleScatteringSize, hdrFormat ) ||
		!createAerialPerspectiveVolume() ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_aerialPerspectiveConstants ) )
		return false;

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
	// на яркость), а яркость — умножением на освещённость от солнца при выборке (cb_skyLightScale). Сдвиг
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
	d3d.setSRV( SRVType::cs, 1, m_multipleScattering.srv() );

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

	// Небо станет целью рендера: снимаем его и таблицу со входов, иначе D3D отвяжет их с предупреждением
	ID3D11ShaderResourceView* nullViews[2] = {};
	context->PSSetShaderResources( 0, 2, nullViews );

	Parameters faceParams = params;
	setParameters( faceParams );

	// 1. Многократное рассеяние: таблица Ψ по высоте и зенитному углу солнца
	d3d.setRenderTarget( m_multipleScattering.rtv(), multipleScatteringSize, multipleScatteringSize );
	m_multipleScatteringShader.draw();
	context->OMSetRenderTargets( 0, nullptr, nullptr );
	d3d.setSRV( SRVType::ps, 1, m_multipleScattering.srv() );

	// 2. Небо: грани мипа 0, затем цепочка мипов (для префильтра с выборкой мипа по плотности)
	for( int32_t face = 0; face < 6; ++face )
	{
		faceParams.face = face;
		setParameters( faceParams );
		d3d.setRenderTarget( m_skyCube.rtv( 0, face ), m_skyCube.size(), m_skyCube.size() );
		m_cubeShader.draw();
	}
	context->OMSetRenderTargets( 0, nullptr, nullptr );
	context->PSSetShaderResources( 1, 1, nullViews );
	context->GenerateMips( m_skyCube.srv().get() );

	// 3. Освещение окружением из неба: гармоники и префильтр отражений
	m_skyLight->capture( m_skyCube );
}

void SkyAtmosphere::setParameters( const Parameters& params )
{
	Parameters data = params;
	Device::updateResourceData<Parameters>( m_constantBuffer.get(), data );
	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
}

void SkyAtmosphere::bindEnvironment()
{
	m_skyLight->bind();
	DMD3D::instance().setSRV( SRVType::ps, SLOT_AERIAL_PERSPECTIVE, m_aerialPerspectiveSRV );
}

void SkyAtmosphere::collectMeshes( const RenderView&, MeshCollector& collector )
{
	if( m_backgroundVisible )
		collector.addCustom( passBit( MeshPass::sky ) );
}

void SkyAtmosphere::renderCustom( const RenderContext& )
{

	setParameters( m_computedFor );
	DMD3D::instance().setSRV( SRVType::ps, 0, m_skyCube.srv() );
	// На дальней плоскости: только там, где сцена ничего не нарисовала
	m_backgroundShader.draw( BlendState::opaque, DepthState::readOnlyNearOrEqual );
}

PropertyContainer* SkyAtmosphere::properties()
{
	return &m_properties;
}

}
