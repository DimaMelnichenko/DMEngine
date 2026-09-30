#include "SkyAtmosphere.h"
#include "Shaders\slots.h"
#include "Shaders\atmosphere_constants.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "D3D\DMD3D.h"
#include "Light\DMLightDriver.h"
#include "Logger\Logger.h"
#include "System.h"

namespace GS
{

namespace
{

constexpr DXGI_FORMAT hdrFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
// Небо вокруг наблюдателя и воздушная перспектива — во float32: луна в единицах запекания (на 1 лк солнца) — ~10⁻⁶,
// ночное свечение — ~10⁻⁹, в половинной точности это ноль
constexpr DXGI_FORMAT skyFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
// Луна светит в небе, пока она выше −10°: ниже её свет в атмосфере — ноль (тень Земли), таблицу луны не считаем
constexpr float moonSkyMinSin = -0.1736f;
constexpr float moonRadiusKm = 1737.4f;
// Звёзды не видны, пока солнце выше горизонта: фон их не считает (~0,2 мс на кадр неба)
constexpr float starsMaxSunSin = 0.0f;

// Слоты таблиц в проходах неба (Shaders/atmosphere.sh, sky_view.sh)
constexpr uint32_t multipleScatteringSlot = 1;
constexpr uint32_t transmittanceSlot = 2;
constexpr uint32_t skyViewSlot = 3;
constexpr uint32_t skyViewMoonSlot = 4;
constexpr uint32_t moonAlbedoSlot = 5;	// фон

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

	if( !m_transmittanceShader.load( "Shaders\\sky_transmittance.ps" ) ||
		!m_multipleScatteringShader.load( "Shaders\\sky_multiscattering.ps" ) ||
		!m_skyViewShader.load( "Shaders\\sky_view.ps" ) ||
		!m_cubeShader.load( "Shaders\\sky_cube.ps" ) ||
		!m_backgroundShader.load( "Shaders\\sky_background.ps" ) ||
		!m_aerialPerspectiveShader.Initialize( "Shaders\\aerial_perspective.cs", "main" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!SkyLight::createSource( m_skyCube ) ||
		!m_transmittanceLut.create( SKY_TRANSMITTANCE_LUT_WIDTH, SKY_TRANSMITTANCE_LUT_HEIGHT, hdrFormat ) ||
		!m_multipleScattering.create( multipleScatteringSize, multipleScatteringSize, hdrFormat ) ||
		!m_skyViewLut.create( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT, skyFormat ) ||
		!m_skyViewMoonLut.create( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT, skyFormat ) ||
		!createAerialPerspectiveVolume() ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( XMFLOAT4 ), m_aerialPerspectiveConstants ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( NightSkyParameters ), m_nightSkyConstants ) )
		return false;

	// Карта луны: нет в базе — белая текстура, диск ровный
	if( System::textures().exists( moonAlbedoTexture ) )
		m_moonAlbedo = System::textures().get( moonAlbedoTexture ).get();
	else
	{
		LOG( std::string( "Sky atmosphere: no texture " ) + moonAlbedoTexture + ", the moon disk is plain" );
		m_moonAlbedo = System::textures().get( DMTextureStorage::whiteId ).get();
	}

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

	prop = m_properties.insert( "Night sky luminance (cd/m2)", settings.nightSkyLuminance );
	prop->setLow( 0.0f );
	prop->setHigh( 0.01f );
	prop->setControlType( GUIControlType::DRAG );

	// Во сколько раз диск луны больше настоящего (0,52°), как Source Angle у источника в UE: по умолчанию вдвое, как
	// диск солнца (Shaders/sky_background.ps), чтобы был заметен на экране; освещённость от луны не меняется
	prop = m_properties.insert( "Moon disk scale", 2.0f );
	prop->setLow( 1.0f );
	prop->setHigh( 30.0f );
	prop->setControlType( GUIControlType::SLIDER );

	// Ночью экспозиция — для пейзажа, а диск луны в ~10⁵ раз ярче: у камеры он уходит в засветку и bloom, как солнце.
	// Глаз (местная адаптация) видит его белым диском с морями: диск целиком приглушается так, чтобы после
	// экспозиции полный диск был не ярче предела (1 — средне-серый у тонмаппера, 3 — яркий, но с видимыми морями).
	// Днём луна тусклее неба и предел не срабатывает; 0 — без предела, физическая яркость
	prop = m_properties.insert( "Moon disk brightness limit", 3.0f );
	prop->setLow( 0.0f );
	prop->setHigh( 20.0f );
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
	settings.nightSkyLuminance = m_properties["Night sky luminance (cd/m2)"].data<float>();
	return settings;
}

bool SkyAtmosphere::createAerialPerspectiveVolume()
{
	TextureDesc desc;
	desc.width = AERIAL_PERSPECTIVE_SIZE;
	desc.height = AERIAL_PERSPECTIVE_SIZE;
	desc.depth = AERIAL_PERSPECTIVE_DEPTH;
	desc.format = hdrFormat;	// с нормировкой (aerialPerspectiveNormalization) — половинной точности хватает
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;

	DMD3D& d3d = DMD3D::instance();
	return d3d.createTexture( desc, nullptr, m_aerialPerspective ) &&
		   d3d.createStorageView( m_aerialPerspective, {}, m_aerialPerspectiveUAV ) &&
		   d3d.createShaderView( m_aerialPerspective, {}, m_aerialPerspectiveSRV );
}

void SkyAtmosphere::setBackgroundVisible( bool visible )
{
	m_backgroundVisible = visible;
}

XMFLOAT3 SkyAtmosphere::sunTransmittance( const XMFLOAT3& toSun ) const
{
	const XMVECTOR origin = XMVectorSet( 0.0f, ATMOSPHERE_PLANET_RADIUS + ATMOSPHERE_OBSERVER_ALTITUDE, 0.0f, 0.0f );
	XMVECTOR direction = XMVector3Normalize( XMLoadFloat3( &toSun ) );

	// Диск (солнца и луны — оба ~0,27°) уходит за горизонт не сразу: доля диска над горизонтом наблюдателя (он ниже 0°
	// на угол dip), линейно. Иначе свет гас бы за кадр — на закате при быстром времени это скачок всей картинки
	constexpr float diskRadius = 0.00465f;
	const float dip = std::acos( ATMOSPHERE_PLANET_RADIUS / ( ATMOSPHERE_PLANET_RADIUS + ATMOSPHERE_OBSERVER_ALTITUDE ) );
	const float elevation = std::asin( std::clamp( XMVectorGetY( direction ), -1.0f, 1.0f ) );
	const float visible = std::clamp( ( elevation + dip + diskRadius ) / ( 2.0f * diskRadius ), 0.0f, 1.0f );
	if( visible <= 0.0f )
		return XMFLOAT3( 0.0f, 0.0f, 0.0f );
	// Центр под горизонтом — пропускание по лучу у самого горизонта
	const float grazing = -dip + 1e-4f;
	if( elevation < grazing )
	{
		const float horizontal = std::sqrt( std::max( 1.0f - XMVectorGetY( direction ) * XMVectorGetY( direction ), 1e-8f ) );
		const float scale = std::cos( grazing ) / horizontal;
		direction = XMVectorSet( XMVectorGetX( direction ) * scale, std::sin( grazing ), XMVectorGetZ( direction ) * scale, 0.0f );
	}
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
	return XMFLOAT3( std::exp( -depth.x ) * visible, std::exp( -depth.y ) * visible, std::exp( -depth.z ) * visible );
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

	// Луна и ночное свечение — в тех же единицах: яркость, делённая на освещённость от солнца (cb_skyLightScale)
	const float bakeScale = lightLuminance > 0.0f ? 1.0f / lightLuminance : 0.0f;
	params.nightSkyLuminance = m_properties["Night sky luminance (cd/m2)"].data<float>() * bakeScale;
	XMFLOAT3 moonColor;
	params.moonDirection = XMFLOAT3( 0.0f, -1.0f, 0.0f );
	if( m_lights->moonLight( params.moonDirection, moonColor ) && params.moonDirection.y > moonSkyMinSin )
		params.moonColor = XMFLOAT3( moonColor.x * bakeScale, moonColor.y * bakeScale, moonColor.z * bakeScale );
	return params;
}

float SkyAtmosphere::skyNormalization() const
{
	const Parameters params = currentParameters();
	// Небо от солнца: 1 днём, в сумерках — ~в 2,5 раза меньше на градус высоты (до −20°)
	const float sunElevation = XMConvertToDegrees( std::asin( std::clamp( params.sunDirection.y, -1.0f, 1.0f ) ) );
	const float sun = std::pow( 10.0f, 0.4f * std::clamp( sunElevation, -20.0f, 0.0f ) );
	return std::max( { sun, luminance( params.moonColor ), params.nightSkyLuminance, 1e-12f } );
}

SkyAtmosphere::NightSkyParameters SkyAtmosphere::currentNightSky( const Parameters& params ) const
{
	NightSkyParameters night = {};
	// Без места и времени — небо без поворота: полюс мира в зените
	night.equatorialX = XMFLOAT3( 1.0f, 0.0f, 0.0f );
	night.equatorialY = XMFLOAT3( 0.0f, 0.0f, 1.0f );
	night.equatorialZ = XMFLOAT3( 0.0f, 1.0f, 0.0f );
	float moonDistance = 384400.0f;
	float fullIlluminance = 0.0f;
	if( const SunPosition* position = m_lights->sunPosition() )
	{
		position->equatorialFrame( night.equatorialX, night.equatorialY, night.equatorialZ );
		const SunPosition::Moon moon = position->moon();
		moonDistance = moon.distance;
		fullIlluminance = moon.fullIlluminance;
	}

	XMFLOAT3 sunDirection;
	XMFLOAT3 sunColor;
	m_lights->directionalLight( sunDirection, sunColor );
	const float sunIlluminance = luminance( sunColor );
	const float bakeScale = sunIlluminance > 0.0f ? 1.0f / sunIlluminance : 0.0f;
	night.starIlluminanceScale = sunDirection.y > starsMaxSunSin ? 0.0f : bakeScale;
	night.moonDiskBrightnessLimit = m_properties["Moon disk brightness limit"].data<float>();

	// Диск луны: яркость полного диска — настоящая (освещённость от полной луны / телесный угол настоящего диска),
	// фазу даёт освещение шара. Увеличенный диск светит той же яркостью: он только в фоне, сцену освещает источник
	// луны, а тусклый большой диск терялся бы днём в небе. Без времени суток — освещённость источника луны
	XMFLOAT3 toMoon;
	XMFLOAT3 moonColor;
	const float realRadius = std::asin( moonRadiusKm / moonDistance );
	night.moonAngularRadius = realRadius * m_properties["Moon disk scale"].data<float>();
	if( m_lights->moonLight( toMoon, moonColor ) )
	{
		const float moonLuminance = luminance( moonColor );
		if( fullIlluminance <= 0.0f )
			fullIlluminance = moonLuminance;
		const float solidAngle = 3.14159265f * realRadius * realRadius;
		const float scale = moonLuminance > 0.0f ? fullIlluminance / moonLuminance / solidAngle * bakeScale : 0.0f;
		night.moonDiskLuminance = XMFLOAT3( moonColor.x * scale, moonColor.y * scale, moonColor.z * scale );
	}
	return night;
}

void SkyAtmosphere::compute( const FrameContext& )
{
	m_frameParams = currentParameters();
	setParameters( m_frameParams );

	// 1. Пропускание и Ψ зависят только от атмосферы — при смене дымки или альбедо земли
	if( !m_lutsValid || m_frameParams.haze != m_lutsFor.haze || m_frameParams.groundAlbedo.x != m_lutsFor.groundAlbedo.x )
	{
		updateAtmosphereLuts();
		m_lutsFor = m_frameParams;
		m_lutsValid = true;
	}

	// 2. Каждый кадр, с солнцем кадра: небо вокруг камеры и объём воздушной перспективы
	updateSkyView();
	updateAerialPerspective();

	// 3. Освещение окружением: при смене солнца или настроек — cubemap неба и новый пересчёт SkyLight по шагу за кадр
	// (самый первый — сразу целиком); пока он идёт, cubemap не меняется. Освещение окружением отстаёт от неба на
	// SkyLight::captureSteps кадров
	if( m_skyLight->capturing() )
	{
		m_skyLight->updateCapture();
	}
	else if( !m_environmentValid || std::memcmp( &m_frameParams, &m_capturedFor, sizeof( Parameters ) ) != 0 )
	{
		renderSkyCube();
		if( m_environmentValid )
			m_skyLight->beginCapture( m_skyCube, skyNormalization() );
		else
			m_skyLight->capture( m_skyCube, skyNormalization() );
		m_capturedFor = m_frameParams;
		m_environmentValid = true;
	}

	bindEnvironment();
}

void SkyAtmosphere::updateAtmosphereLuts()
{
	DMD3D& d3d = DMD3D::instance();

	d3d.beginPass( PassDesc{ "Sky transmittance LUT", { { &m_transmittanceLut.target(), "transmittance LUT" } }, {},
							 SKY_TRANSMITTANCE_LUT_WIDTH, SKY_TRANSMITTANCE_LUT_HEIGHT } );
	m_transmittanceShader.draw();

	d3d.beginPass( PassDesc{ "Sky multiple scattering LUT", { { &m_multipleScattering.target(), "multiple scattering LUT" } }, {},
							 multipleScatteringSize, multipleScatteringSize, { { &m_transmittanceLut.srv(), "transmittance LUT" } } } );
	d3d.setSRV( SRVType::ps, transmittanceSlot, m_transmittanceLut.srv() );
	m_multipleScatteringShader.draw();
}

void SkyAtmosphere::updateSkyView()
{
	DMD3D& d3d = DMD3D::instance();
	const auto bindLuts = [&]
	{
		d3d.setSRV( SRVType::ps, multipleScatteringSlot, m_multipleScattering.srv() );
		d3d.setSRV( SRVType::ps, transmittanceSlot, m_transmittanceLut.srv() );
	};
	const std::vector<PassDesc::Read> lutReads = { { &m_multipleScattering.srv(), "multiple scattering LUT" },
												  { &m_transmittanceLut.srv(), "transmittance LUT" } };
	d3d.beginPass( PassDesc{ "Sky-View LUT", { { &m_skyViewLut.target(), "Sky-View LUT" } }, {}, SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT, lutReads } );
	bindLuts();
	m_skyViewShader.draw();

	// Таблица луны — своя: небо от луны симметрично относительно её вертикальной плоскости, а не солнца
	const XMFLOAT3& moonColor = m_frameParams.moonColor;
	if( moonColor.x > 0.0f || moonColor.y > 0.0f || moonColor.z > 0.0f )
	{
		Parameters moonParams = m_frameParams;
		moonParams.skyViewLight = 1;
		setParameters( moonParams );
		d3d.beginPass( PassDesc{ "Sky-View LUT (moon)", { { &m_skyViewMoonLut.target(), "Sky-View moon LUT" } }, {}, SKY_VIEW_LUT_WIDTH,
								 SKY_VIEW_LUT_HEIGHT, lutReads } );
		bindLuts();
		m_skyViewShader.draw();
		setParameters( m_frameParams );
	}
}

void SkyAtmosphere::renderSkyCube()
{
	DMD3D& d3d = DMD3D::instance();
	const std::vector<PassDesc::Read> reads = { { &m_transmittanceLut.srv(), "transmittance LUT" }, { &m_skyViewLut.srv(), "Sky-View LUT" },
											   { &m_skyViewMoonLut.srv(), "Sky-View moon LUT" } };

	// Грани мипа 0; цепочку мипов (для префильтра с выборкой мипа по плотности и гармоник) строит SkyLight в compute
	Parameters faceParams = m_frameParams;
	for( int32_t face = 0; face < 6; ++face )
	{
		faceParams.face = face;
		setParameters( faceParams );
		d3d.beginPass( PassDesc{ "Sky cube face", { { &m_skyCube.target( 0, face ), "sky cube" } }, {}, m_skyCube.size(), m_skyCube.size(), reads } );
		d3d.setSRV( SRVType::ps, transmittanceSlot, m_transmittanceLut.srv() );
		d3d.setSRV( SRVType::ps, skyViewSlot, m_skyViewLut.srv() );
		d3d.setSRV( SRVType::ps, skyViewMoonSlot, m_skyViewMoonLut.srv() );
		m_cubeShader.draw();
	}
	setParameters( m_frameParams );
}

void SkyAtmosphere::updateAerialPerspective()
{
	DMD3D& d3d = DMD3D::instance();

	// Compute-проход: объём пишется (beginPass снимает его с входа пиксельных шейдеров прошлого кадра, t106)
	d3d.beginPass( PassDesc{ "Aerial perspective volume", {}, {}, 0, 0,
							 { { &m_multipleScattering.srv(), "multiple scattering LUT" }, { &m_transmittanceLut.srv(), "transmittance LUT" } },
							 { { &m_aerialPerspectiveUAV, "aerial perspective volume" } } } );

	// Раскладка — AerialPerspectiveBuffer в Shaders/aerial_perspective.cs
	XMFLOAT4 constants( m_properties["Aerial perspective view distance scale"].data<float>(), 1.0f / skyNormalization(),
						0.0f, 0.0f );
	Device::updateResourceData<XMFLOAT4>( m_aerialPerspectiveConstants, constants );
	d3d.setConstantBuffer( SRVType::cs, 4, m_aerialPerspectiveConstants );
	d3d.setConstantBuffer( SRVType::cs, SLOT_CB_PASS, m_constantBuffer );	// параметры кадра — setParameters в compute()
	d3d.setSRV( SRVType::cs, multipleScatteringSlot, m_multipleScattering.srv() );
	d3d.setSRV( SRVType::cs, transmittanceSlot, m_transmittanceLut.srv() );

	// Поток — столбец объёма: идёт от камеры по слоям и пишет накопленное к концу каждого
	m_aerialPerspectiveShader.setUAVBuffer( 0, m_aerialPerspectiveUAV );
	constexpr uint32_t groupSize = 8;	// = numthreads в Shaders/aerial_perspective.cs
	m_aerialPerspectiveShader.dispatchGroups( AERIAL_PERSPECTIVE_SIZE / groupSize, AERIAL_PERSPECTIVE_SIZE / groupSize, 1 );
}

void SkyAtmosphere::setParameters( const Parameters& params )
{
	Parameters data = params;
	Device::updateResourceData<Parameters>( m_constantBuffer, data );
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
	setParameters( m_frameParams );
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( SRVType::ps, transmittanceSlot, m_transmittanceLut.srv() );
	d3d.setSRV( SRVType::ps, skyViewSlot, m_skyViewLut.srv() );
	d3d.setSRV( SRVType::ps, skyViewMoonSlot, m_skyViewMoonLut.srv() );
	d3d.setSRV( SRVType::ps, moonAlbedoSlot, m_moonAlbedo->srv() );
	NightSkyParameters night = currentNightSky( m_frameParams );
	Device::updateResourceData<NightSkyParameters>( m_nightSkyConstants, night );
	d3d.setConstantBuffer( SRVType::ps, 4, m_nightSkyConstants );
	// На дальней плоскости: только там, где сцена ничего не нарисовала
	m_backgroundShader.draw( BlendState::opaque, DepthState::readOnlyNearOrEqual );
}

PropertyContainer* SkyAtmosphere::properties()
{
	return &m_properties;
}

}
