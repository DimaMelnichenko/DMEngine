#include "SkyAtmosphere.h"
#include "Properties\SettingsProperties.h"
#include "Shaders\slots.h"
#include "Shaders\atmosphere_constants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <DirectXTex.h>
#include "D3D\DMD3D.h"
#include "D3D\TextureImages.h"
#include "SceneTargets.h"
#include "Texture\ImageFile.h"
#include "Utils\Fingerprint.h"
#include "Light\DMLightDriver.h"
#include "Logger\Logger.h"
#include "System.h"

using namespace DirectX;

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
// Пепельный свет: Земля — ламбертов шар с геометрическим альбедо ~0,3 (Goode et al. 2001, по самому пепельному свету)
constexpr float earthRadiusKm = 6371.0f;
constexpr float earthAlbedo = 0.3f;
// Звёзды не видны, пока солнце выше горизонта: фон их не считает (~0,2 мс на кадр неба)
constexpr float starsMaxSunSin = 0.0f;

// Слоты таблиц в проходах неба (Shaders/atmosphere.sh, sky_view.sh)
constexpr uint32_t multipleScatteringSlot = 1;
constexpr uint32_t transmittanceSlot = 2;
constexpr uint32_t skyViewSlot = 3;
constexpr uint32_t skyViewMoonSlot = 4;
constexpr uint32_t moonAlbedoSlot = 5;	// фон
constexpr uint32_t starsSlot = 6;		// звёзды
constexpr uint32_t milkyWaySlot = 7;	// фон
// Кэш готовой карты Млечного Пути; версия — в отпечатке: сменился способ сборки — карта соберётся заново
constexpr const char* milkyWayCacheDirectory = "cache\\sky";
constexpr uint32_t milkyWayCacheVersion = 1;
// Освещённость от звезды величины m — 10^(−0,4·(m + 14,18)) лк, как у луны в SunPosition::computeMoon
constexpr float starMagnitudeOffset = 14.18f;

float luminance( const XMFLOAT3& color )
{
	return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

// Базис тела луны в мире по направлению на неё, полюсу мира и ориентации (Meeus, гл. 53, градусы): ось вращения, к нулевому
// меридиану и к 90° восточной селенографической долготы. Ось на диске повёрнута от севера неба к востоку на позиционный
// угол и наклонена к наблюдателю на либрацию по широте, центр диска — на либрацию по долготе от нулевого меридиана.
// Без либрации и наклона оси: север неба на диске, к наблюдателю и запад неба (запад — cross( север, на луну ), как в
// Shaders/sky_background.ps)
void moonBodyFrame( const XMFLOAT3& toMoon, const XMFLOAT3& celestialPole, float librationLongitude, float librationLatitude,
					float positionAngle, XMFLOAT3& north, XMFLOAT3& prime, XMFLOAT3& east )
{
	const XMVECTOR view = XMVector3Normalize( XMLoadFloat3( &toMoon ) );
	const XMVECTOR observer = XMVectorNegate( view );
	const XMVECTOR pole = XMLoadFloat3( &celestialPole );
	XMVECTOR skyNorth = XMVectorSubtract( pole, XMVectorScale( view, XMVectorGetX( XMVector3Dot( pole, view ) ) ) );
	skyNorth = XMVectorGetX( XMVector3LengthSq( skyNorth ) ) > 1e-8f ? XMVector3Normalize( skyNorth ) : XMVectorSet( 0.0f, 1.0f, 0.0f, 0.0f );
	const XMVECTOR skyEast = XMVectorNegate( XMVector3Cross( skyNorth, view ) );
	const float angle = XMConvertToRadians( positionAngle );
	const float latitude = XMConvertToRadians( librationLatitude );
	const float longitude = XMConvertToRadians( librationLongitude );
	const XMVECTOR axisOnSky = XMVectorAdd( XMVectorScale( skyNorth, std::cos( angle ) ), XMVectorScale( skyEast, std::sin( angle ) ) );
	const XMVECTOR axis = XMVectorAdd( XMVectorScale( axisOnSky, std::cos( latitude ) ), XMVectorScale( observer, std::sin( latitude ) ) );
	const XMVECTOR center = XMVector3Normalize( XMVectorSubtract( observer, XMVectorScale( axis, std::sin( latitude ) ) ) );
	const XMVECTOR side = XMVector3Cross( center, axis );
	XMStoreFloat3( &north, axis );
	XMStoreFloat3( &prime, XMVectorSubtract( XMVectorScale( center, std::cos( longitude ) ), XMVectorScale( side, std::sin( longitude ) ) ) );
	XMStoreFloat3( &east, XMVectorAdd( XMVectorScale( center, std::sin( longitude ) ), XMVectorScale( side, std::cos( longitude ) ) ) );
}

// Лепесток аппроксимации функций сложения цветов CIE 1931: гаусс с разной шириной слева и справа от пика, нм
double colorMatchingLobe( double wavelength, double mean, double left, double right )
{
	const double t = ( wavelength - mean ) / ( wavelength < mean ? left : right );
	return std::exp( -0.5 * t * t );
}

// Цвет звезды по показателю B − V: температура (Ballesteros 2012) → излучение чёрного тела (Планк) → XYZ (функции сложения
// CIE 1931 по Wyman, Sloan, Shirley 2013) → линейный Rec.709 с яркостью 1
XMFLOAT3 starColor( float colorIndex )
{
	const double bv = std::clamp( colorIndex, -0.4f, 2.0f );
	const double temperature = 4600.0 * ( 1.0 / ( 0.92 * bv + 1.7 ) + 1.0 / ( 0.92 * bv + 0.62 ) );
	double x = 0.0, y = 0.0, z = 0.0;
	for( double wavelength = 380.0; wavelength <= 780.0; wavelength += 5.0 )
	{
		const double meters = wavelength * 1e-9;
		const double planck = 1.0 / ( std::pow( meters, 5.0 ) * ( std::exp( 1.4388e-2 / ( meters * temperature ) ) - 1.0 ) );
		x += planck * ( 1.056 * colorMatchingLobe( wavelength, 599.8, 37.9, 31.0 ) + 0.362 * colorMatchingLobe( wavelength, 442.0, 16.0, 26.7 ) -
						0.065 * colorMatchingLobe( wavelength, 501.1, 20.4, 26.2 ) );
		y += planck * ( 0.821 * colorMatchingLobe( wavelength, 568.8, 46.9, 40.5 ) + 0.286 * colorMatchingLobe( wavelength, 530.9, 16.3, 31.1 ) );
		z += planck * ( 1.217 * colorMatchingLobe( wavelength, 437.0, 11.8, 36.0 ) + 0.681 * colorMatchingLobe( wavelength, 459.0, 26.0, 13.8 ) );
	}
	const XMFLOAT3 rgb( static_cast<float>( std::max( 3.2406 * x - 1.5372 * y - 0.4986 * z, 0.0 ) ),
						static_cast<float>( std::max( -0.9689 * x + 1.8758 * y + 0.0415 * z, 0.0 ) ),
						static_cast<float>( std::max( 0.0557 * x - 0.2040 * y + 1.0570 * z, 0.0 ) ) );
	const float scale = 1.0f / std::max( luminance( rgb ), 1e-30f );
	return XMFLOAT3( rgb.x * scale, rgb.y * scale, rgb.z * scale );
}

// Карта Млечного Пути для фона из файла path: float → сводка в лог → нормировка на среднее по сфере 1 → мипы →
// R9G9B9E5 (общая экспонента: 4 байта на тексель, точности хватает на ровный свет без насыщенных цветов)
bool buildMilkyWay( const std::string& path, ScratchImage& packed )
{
	ScratchImage image;
	if( !ImageFile::load( path, image ) )
		return false;
	constexpr DXGI_FORMAT floatFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
	if( image.GetMetadata().format != floatFormat )
	{
		ScratchImage converted;
		const HRESULT hr = IsCompressed( image.GetMetadata().format ) ?
			Decompress( image.GetImages(), image.GetImageCount(), image.GetMetadata(), floatFormat, converted ) :
			Convert( image.GetImages(), image.GetImageCount(), image.GetMetadata(), floatFormat, TEX_FILTER_DEFAULT,
					 TEX_THRESHOLD_DEFAULT, converted );
		if( FAILED( hr ) )
		{
			LOG( "Sky atmosphere: can't convert the Milky Way " + path + " to float" );
			return false;
		}
		image = std::move( converted );
	}

	// Сводка по телесному углу (строка карты — склонение, вес cos δ): среднее по сфере, полоса Галактики |b| < 10° и её
	// полюса |b| > 60°, центр Галактики и Большое Магелланово Облако — проверка ориентации (RA растёт влево:
	// u = 0,5 − RA / 360°; у зеркальной карты оба ≈ среднему)
	const Image& level = *image.GetImage( 0, 0, 0 );
	const size_t width = level.width;
	const size_t height = level.height;
	auto texel = [&]( size_t x, size_t y ) -> XMFLOAT4& { return reinterpret_cast<XMFLOAT4*>( level.pixels + y * level.rowPitch )[x]; };
	const XMVECTOR galacticPole = XMVectorSet( -0.8676661f, -0.1980764f, 0.4559838f, 0.0f );	// RA 192,86°, Dec 27,13°
	const float sinBand = std::sin( XMConvertToRadians( 10.0f ) );
	const float sinPoles = std::sin( XMConvertToRadians( 60.0f ) );
	double sum = 0.0, weightSum = 0.0, plane = 0.0, planeWeight = 0.0, poles = 0.0, polesWeight = 0.0;
	for( size_t y = 0; y < height; ++y )
	{
		const float declination = ( 0.5f - ( y + 0.5f ) / height ) * XM_PI;
		const float weight = std::cos( declination );
		for( size_t x = 0; x < width; ++x )
		{
			const XMFLOAT4& value = texel( x, y );
			const float weighted = luminance( XMFLOAT3( value.x, value.y, value.z ) ) * weight;
			const float rightAscension = ( 0.5f - ( x + 0.5f ) / width ) * XM_2PI;
			const XMVECTOR direction = XMVectorSet( weight * std::cos( rightAscension ), weight * std::sin( rightAscension ),
													std::sin( declination ), 0.0f );
			const float sinLatitude = std::abs( XMVectorGetX( XMVector3Dot( direction, galacticPole ) ) );
			sum += weighted;
			weightSum += weight;
			if( sinLatitude < sinBand )
			{
				plane += weighted;
				planeWeight += weight;
			}
			else if( sinLatitude > sinPoles )
			{
				poles += weighted;
				polesWeight += weight;
			}
		}
	}
	const float mean = static_cast<float>( sum / std::max( weightSum, 1e-30 ) );
	if( mean <= 0.0f )
	{
		LOG( "Sky atmosphere: the Milky Way " + path + " is black" );
		return false;
	}
	auto relativeAt = [&]( float rightAscension, float declination )
	{
		const float u = 0.5f - rightAscension / 360.0f;
		const size_t x = static_cast<size_t>( ( u - std::floor( u ) ) * width ) % width;
		const size_t y = std::min( static_cast<size_t>( ( 0.5f - declination / 180.0f ) * height ), height - 1 );
		const XMFLOAT4& value = texel( x, y );
		return luminance( XMFLOAT3( value.x, value.y, value.z ) ) / mean;
	};
	LOG( "Sky atmosphere: Milky Way " + path + " " + std::to_string( width ) + "x" + std::to_string( height ) + ", mean " +
		 std::to_string( mean ) + "; galactic plane (|b| < 10) " + std::to_string( plane / std::max( planeWeight, 1e-30 ) / mean ) +
		 " x mean, poles (|b| > 60) " + std::to_string( poles / std::max( polesWeight, 1e-30 ) / mean ) + " x mean, galactic center " +
		 std::to_string( relativeAt( 266.4f, -28.9f ) ) + " x mean, LMC " + std::to_string( relativeAt( 80.9f, -69.8f ) ) + " x mean" );

	// Нормировка на среднее 1: яркость задаёт milky_way_luminance уровня
	const float scale = 1.0f / mean;
	for( size_t y = 0; y < height; ++y )
	{
		for( size_t x = 0; x < width; ++x )
		{
			XMFLOAT4& value = texel( x, y );
			value = XMFLOAT4( value.x * scale, value.y * scale, value.z * scale, 1.0f );
		}
	}
	ScratchImage mips;
	if( FAILED( GenerateMipMaps( *image.GetImage( 0, 0, 0 ), TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, mips ) ) ||
		FAILED( Convert( mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), DXGI_FORMAT_R9G9B9E5_SHAREDEXP,
						 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, packed ) ) )
	{
		LOG( "Sky atmosphere: can't build mips of the Milky Way " + path );
		return false;
	}
	return true;
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

bool SkyAtmosphere::initialize( const DMLightDriver& lights, const Settings& settings, const std::vector<StarRecord>& stars,
								SkyLight& skyLight )
{
	m_lights = &lights;
	m_skyLight = &skyLight;

	// Цели для прогрева пайплайнов — форматы таблиц, куба и буфера сцены
	if( !m_transmittanceShader.load( "Shaders\\sky_transmittance.ps", TargetFormats::colorTarget( hdrFormat ) ) ||
		!m_multipleScatteringShader.load( "Shaders\\sky_multiscattering.ps", TargetFormats::colorTarget( hdrFormat ) ) ||
		!m_skyViewShader.load( "Shaders\\sky_view.ps", TargetFormats::colorTarget( skyFormat ) ) ||
		!m_cubeShader.load( "Shaders\\sky_cube.ps", TargetFormats::colorTarget( skyFormat ) ) ||
		!m_backgroundShader.load( "Shaders\\sky_background.ps", SceneTargets::formats() ) ||
		!m_aerialPerspectiveShader.Initialize( "Shaders\\aerial_perspective.cs", "main" ) ||
		!m_starShader.addShaderPassFromFile( ShaderStageType::vertex, "main", "Shaders\\sky_stars.vs" ) ||
		!m_starShader.addShaderPassFromFile( ShaderStageType::pixel, "main", "Shaders\\sky_stars.ps" ) ||
		m_starShader.createPhase( 0, 0 ) < 0 )
		return false;
	m_starShader.warmPipelines( { { RasterState::noCulling, DepthState::readOnlyNearOrEqual, BlendState::additive } },
								SceneTargets::formats() );

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( SkyParameters ), m_constantBuffer ) ||
		!SkyLight::createSource( m_skyCube ) ||
		!m_transmittanceLut.create( SKY_TRANSMITTANCE_LUT_WIDTH, SKY_TRANSMITTANCE_LUT_HEIGHT, hdrFormat, "Transmittance LUT" ) ||
		!m_multipleScattering.create( multipleScatteringSize, multipleScatteringSize, hdrFormat, "Multiple scattering LUT" ) ||
		!m_skyViewLut.create( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT, skyFormat, "Sky-View LUT" ) ||
		!m_skyViewMoonLut.create( SKY_VIEW_LUT_WIDTH, SKY_VIEW_LUT_HEIGHT, skyFormat, "Sky-View moon LUT" ) ||
		!createAerialPerspectiveVolume() ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( AerialPerspectiveBuffer ), m_aerialPerspectiveConstants ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( NightSkyParameters ), m_nightSkyConstants ) ||
		!createStars( stars ) )
		return false;
	m_milkyWayFile = settings.milkyWay;
	if( !m_milkyWayFile.empty() )
		m_hasMilkyWay = loadMilkyWay( m_milkyWayFile );

	// Карта луны: нет в базе — белая текстура, диск ровный
	if( System::textures().exists( moonAlbedoTexture ) )
		m_moonAlbedo = System::textures().get( moonAlbedoTexture ).get();
	else
	{
		LOG( std::string( "Sky atmosphere: no texture " ) + moonAlbedoTexture + ", the moon disk is plain" );
		m_moonAlbedo = System::textures().get( DMTextureStorage::whiteId ).get();
	}

	m_properties.setName( "Sky atmosphere" );

	// Данные уровня — по описанию строки SkyAtmosphere; остальное ниже — настройки показа луны и звёзд, не данные уровня
	addSettingProperties( m_properties, skyAtmosphereSchema(), settings );
	Property* prop = nullptr;

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

	// Пепельный свет — тёмная часть луны, освещённая Землёй: в ~10⁴ раз тусклее освещённой солнцем, но в сумерках и ночью
	// ярче неба вокруг. Глаз видит его серым диском рядом с белым серпом — свой предел яркости после экспозиции (диск
	// приглушается целиком отдельно от серпа; 0,05 — вдвое светлее ночного неба на экране и вчетверо темнее серпа).
	// 0 — физическая яркость: ночью диск почти как серп
	prop = m_properties.insert( "Earthshine brightness limit", 0.05f );
	prop->setLow( 0.0f );
	prop->setHigh( 3.0f );
	prop->setControlType( GUIControlType::SLIDER );

	// Мерцание звёзд (Shaders/sky_stars.vs): 1 — по воздушной массе, в зените почти нет, у горизонта сильное и разноцветное;
	// 0 — не мерцают (ночные кадры совпадают при сравнении)
	prop = m_properties.insert( "Star twinkle", 1.0f );
	prop->setLow( 0.0f );
	prop->setHigh( 3.0f );
	prop->setControlType( GUIControlType::SLIDER );

	return true;
}

SkyAtmosphere::Settings SkyAtmosphere::settings()
{
	Settings settings;
	readSettingProperties( m_properties, skyAtmosphereSchema(), settings );
	settings.milkyWay = m_milkyWayFile;
	return settings;
}

bool SkyAtmosphere::createStars( const std::vector<StarRecord>& stars )
{
	if( stars.empty() )
	{
		LOG( "Sky atmosphere: no stars (table Stars is empty, Tools/star_catalog.py)" );
		return true;
	}
	std::vector<StarData> data;
	data.reserve( stars.size() );
	const StarRecord* brightest = &stars.front();
	for( const StarRecord& star : stars )
	{
		const float rightAscension = XMConvertToRadians( star.rightAscension );
		const float declination = XMConvertToRadians( star.declination );
		StarData item = {};
		item.direction = XMFLOAT3( std::cos( declination ) * std::cos( rightAscension ), std::cos( declination ) * std::sin( rightAscension ),
								   std::sin( declination ) );
		item.illuminance = std::pow( 10.0f, -0.4f * ( star.magnitude + starMagnitudeOffset ) );
		item.color = starColor( star.colorIndex );
		data.push_back( item );
		if( star.magnitude < brightest->magnitude )
			brightest = &star;
	}

	BufferDesc desc;
	desc.size = static_cast<uint32_t>( data.size() * sizeof( StarData ) );
	desc.stride = sizeof( StarData );
	desc.usage = BufferUsage::shaderResource | BufferUsage::structured;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createBuffer( desc, data.data(), m_stars ) || !d3d.createShaderView( m_stars, BufferViewDesc(), m_starsView ) )
		return false;
	d3d.setName( m_stars, "Stars" );
	m_starCount = static_cast<uint32_t>( data.size() );
	LOG( "Sky atmosphere: " + std::to_string( m_starCount ) + " stars, brightest V " + std::to_string( brightest->magnitude ) +
		 " at RA " + std::to_string( brightest->rightAscension ) + ", Dec " + std::to_string( brightest->declination ) );
	return true;
}

bool SkyAtmosphere::loadMilkyWay( const std::string& file )
{
	const auto start = std::chrono::high_resolution_clock::now();
	const std::string path = "Textures\\" + file;
	std::error_code error;
	const uintmax_t size = std::filesystem::file_size( path, error );
	if( error )
	{
		LOG( "Sky atmosphere: no Milky Way " + path + ", the night sky is without it" );
		return false;
	}

	// Готовая карта (нормировка, мипы, R9G9B9E5) — в кэше по отпечатку файла: сборка из EXR 4096 × 2048 — ~1 с
	Fingerprint key;
	key.add( file.data(), file.size() );
	key.add( size );
	key.add( std::filesystem::last_write_time( path, error ).time_since_epoch().count() );
	key.add( milkyWayCacheVersion );
	char name[64];
	snprintf( name, sizeof( name ), "milky_way_%016llx.dds", static_cast<unsigned long long>( key.value ) );
	const std::string cachePath = std::string( milkyWayCacheDirectory ) + "\\" + name;

	ScratchImage packed;
	const bool cached = std::filesystem::exists( cachePath, error ) && ImageFile::load( cachePath, packed ) &&
						packed.GetMetadata().format == DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
	if( !cached )
	{
		if( !buildMilkyWay( path, packed ) )
		{
			LOG( "Sky atmosphere: can't load the Milky Way " + path + ", the night sky is without it" );
			return false;
		}
		// Прежние варианты карты — удалить: один файл на карту
		std::filesystem::create_directories( milkyWayCacheDirectory, error );
		for( const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator( milkyWayCacheDirectory, error ) )
			if( entry.is_regular_file() && entry.path().filename().string().rfind( "milky_way_", 0 ) == 0 )
				std::filesystem::remove( entry.path(), error );
		const std::wstring wide( cachePath.begin(), cachePath.end() );
		if( FAILED( SaveToDDSFile( packed.GetImages(), packed.GetImageCount(), packed.GetMetadata(), DDS_FLAGS_NONE, wide.c_str() ) ) )
			LOG( "Sky atmosphere: can't write the Milky Way cache " + cachePath );
	}
	if( !GpuImages::createTexture( packed, m_milkyWayTexture, m_milkyWay ) )
	{
		LOG( "Sky atmosphere: can't create the Milky Way texture from " + path );
		return false;
	}
	DMD3D::instance().setName( m_milkyWayTexture, "Milky Way" );
	const auto elapsed = std::chrono::duration<double, std::milli>( std::chrono::high_resolution_clock::now() - start ).count();
	LOG( "Sky atmosphere: Milky Way " + std::string( cached ? "from cache " + cachePath : "built from " + path ) + ", ms " +
		 std::to_string( elapsed ) );
	return true;
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
	if( !d3d.createTexture( desc, nullptr, m_aerialPerspective ) )
		return false;
	d3d.setName( m_aerialPerspective, "Aerial perspective volume" );
	return d3d.createStorageView( m_aerialPerspective, {}, m_aerialPerspectiveUAV ) &&
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

SkyParameters SkyAtmosphere::currentParameters() const
{
	SkyParameters params = {};
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
	params.nightSkyLuminance = m_properties["Night sky luminance"].data<float>() * bakeScale;
	XMFLOAT3 moonColor;
	params.moonDirection = XMFLOAT3( 0.0f, -1.0f, 0.0f );
	if( m_lights->moonLight( params.moonDirection, moonColor ) && params.moonDirection.y > moonSkyMinSin )
		params.moonColor = XMFLOAT3( moonColor.x * bakeScale, moonColor.y * bakeScale, moonColor.z * bakeScale );
	return params;
}

float SkyAtmosphere::skyNormalization() const
{
	const SkyParameters params = currentParameters();
	// Небо от солнца: 1 днём, в сумерках — ~в 2,5 раза меньше на градус высоты (до −20°)
	const float sunElevation = XMConvertToDegrees( std::asin( std::clamp( params.sunDirection.y, -1.0f, 1.0f ) ) );
	const float sun = std::pow( 10.0f, 0.4f * std::clamp( sunElevation, -20.0f, 0.0f ) );
	return std::max( { sun, luminance( params.moonColor ), params.nightSkyLuminance, 1e-12f } );
}

NightSkyParameters SkyAtmosphere::currentNightSky( const SkyParameters& params ) const
{
	NightSkyParameters night = {};
	// Без места и времени — небо без поворота: полюс мира в зените
	night.equatorialX = XMFLOAT3( 1.0f, 0.0f, 0.0f );
	night.equatorialY = XMFLOAT3( 0.0f, 0.0f, 1.0f );
	night.equatorialZ = XMFLOAT3( 0.0f, 1.0f, 0.0f );
	float moonDistance = 384400.0f;
	float fullIlluminance = 0.0f;
	float librationLongitude = 0.0f;
	float librationLatitude = 0.0f;
	float positionAngle = 0.0f;
	if( const SunPosition* position = m_lights->sunPosition() )
	{
		position->equatorialFrame( night.equatorialX, night.equatorialY, night.equatorialZ );
		const SunPosition::Moon moon = position->moon();
		moonDistance = moon.distance;
		fullIlluminance = moon.fullIlluminance;
		librationLongitude = moon.librationLongitude;
		librationLatitude = moon.librationLatitude;
		positionAngle = moon.positionAngle;
	}

	XMFLOAT3 sunDirection;
	XMFLOAT3 sunColor;
	m_lights->directionalLight( sunDirection, sunColor );
	const float sunIlluminance = luminance( sunColor );
	const float bakeScale = sunIlluminance > 0.0f ? 1.0f / sunIlluminance : 0.0f;
	night.starIlluminanceScale = sunDirection.y > starsMaxSunSin ? 0.0f : bakeScale;
	night.starTwinkle = m_properties["Star twinkle"].data<float>();
	night.moonDiskBrightnessLimit = m_properties["Moon disk brightness limit"].data<float>();
	night.viewportSize = XMFLOAT2( static_cast<float>( DMD3D::instance().backBufferWidth() ),
								   static_cast<float>( DMD3D::instance().backBufferHeight() ) );
	// Млечный Путь — ночью, как звёзды
	if( m_hasMilkyWay )
		night.milkyWayLuminance = m_properties["Milky Way luminance"].data<float>() * night.starIlluminanceScale;

	// Диск луны: яркость полного диска — настоящая (освещённость от полной луны / телесный угол настоящего диска),
	// фазу даёт освещение шара. Увеличенный диск светит той же яркостью: он только в фоне, сцену освещает источник
	// луны, а тусклый большой диск терялся бы днём в небе. Без времени суток — освещённость источника луны
	XMFLOAT3 toMoon;
	XMFLOAT3 moonColor;
	night.earthshineBrightnessLimit = m_properties["Earthshine brightness limit"].data<float>();
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

		// Поворот диска: либрация и позиционный угол оси — по SunPosition; без времени суток — север луны к полюсу мира
		moonBodyFrame( toMoon, night.equatorialZ, librationLongitude, librationLatitude, positionAngle, night.moonNorth,
					   night.moonPrime, night.moonEast );
		// Пепельный свет: освещённость луны от Земли относительно солнечной — p⊕·(R⊕ / d)²·Φ(α⊕), Φ — фаза ламбертова шара,
		// угол фазы Земли с луны — элонгация луны от солнца (новолуние — полная Земля, ~8·10⁻⁵); солнце — то же, что
		// освещает диск в шейдере
		const XMFLOAT3& toSun = params.sunDirection;
		const float elongation = std::acos( std::clamp( toMoon.x * toSun.x + toMoon.y * toSun.y + toMoon.z * toSun.z, -1.0f, 1.0f ) );
		const float phase = ( std::sin( elongation ) + ( XM_PI - elongation ) * std::cos( elongation ) ) / XM_PI;
		night.earthshine = earthAlbedo * ( earthRadiusKm / moonDistance ) * ( earthRadiusKm / moonDistance ) * phase;
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
	else if( !m_environmentValid || std::memcmp( &m_frameParams, &m_capturedFor, sizeof( SkyParameters ) ) != 0 )
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
	d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );
	m_multipleScatteringShader.draw();
}

void SkyAtmosphere::updateSkyView()
{
	DMD3D& d3d = DMD3D::instance();
	const auto bindLuts = [&]
	{
		d3d.setSRV( multipleScatteringSlot, m_multipleScattering.srv() );
		d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );
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
		SkyParameters moonParams = m_frameParams;
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
	SkyParameters faceParams = m_frameParams;
	for( int32_t face = 0; face < 6; ++face )
	{
		faceParams.face = face;
		setParameters( faceParams );
		d3d.beginPass( PassDesc{ "Sky cube face", { { &m_skyCube.target( 0, face ), "sky cube" } }, {}, m_skyCube.size(), m_skyCube.size(), reads } );
		d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );
		d3d.setSRV( skyViewSlot, m_skyViewLut.srv() );
		d3d.setSRV( skyViewMoonSlot, m_skyViewMoonLut.srv() );
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

	AerialPerspectiveBuffer constants = {};
	constants.viewDistanceScale = m_properties["Aerial perspective view distance scale"].data<float>();
	constants.outputScale = 1.0f / skyNormalization();
	Device::updateResourceData<AerialPerspectiveBuffer>( m_aerialPerspectiveConstants, constants );
	d3d.setConstantBuffer( 4, m_aerialPerspectiveConstants );
	d3d.setConstantBuffer( SLOT_CB_PASS, m_constantBuffer );	// параметры кадра — setParameters в compute()
	d3d.setSRV( multipleScatteringSlot, m_multipleScattering.srv() );
	d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );

	// Поток — столбец объёма: идёт от камеры по слоям и пишет накопленное к концу каждого
	m_aerialPerspectiveShader.setUAVBuffer( 0, m_aerialPerspectiveUAV );
	constexpr uint32_t groupSize = 8;	// = numthreads в Shaders/aerial_perspective.cs
	m_aerialPerspectiveShader.dispatchGroups( AERIAL_PERSPECTIVE_SIZE / groupSize, AERIAL_PERSPECTIVE_SIZE / groupSize, 1 );
}

void SkyAtmosphere::bindForClouds()
{
	setParameters( m_frameParams );
	DMD3D& d3d = DMD3D::instance();
	d3d.setSRV( multipleScatteringSlot, m_multipleScattering.srv() );
	d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );
}

void SkyAtmosphere::cloudLight( XMFLOAT3& direction, XMFLOAT3& color ) const
{
	// Ниже −3° солнце облаков уже не освещает (тень Земли на высоте слоя), светит луна
	constexpr float sunMinSin = -0.0523f;
	if( m_frameParams.sunDirection.y > sunMinSin || m_frameParams.moonDirection.y <= 0.0f )
	{
		direction = m_frameParams.sunDirection;
		color = m_frameParams.sunColor;
	}
	else
	{
		direction = m_frameParams.moonDirection;
		color = m_frameParams.moonColor;
	}
}

void SkyAtmosphere::setParameters( const SkyParameters& params )
{
	SkyParameters data = params;
	Device::updateResourceData<SkyParameters>( m_constantBuffer, data );
	DMD3D::instance().setConstantBuffer( SLOT_CB_PASS, m_constantBuffer );
}

void SkyAtmosphere::bindEnvironment()
{
	m_skyLight->bind();
	DMD3D::instance().setSRV( SLOT_AERIAL_PERSPECTIVE, m_aerialPerspectiveSRV );
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
	d3d.setSRV( transmittanceSlot, m_transmittanceLut.srv() );
	d3d.setSRV( skyViewSlot, m_skyViewLut.srv() );
	d3d.setSRV( skyViewMoonSlot, m_skyViewMoonLut.srv() );
	d3d.setSRV( moonAlbedoSlot, m_moonAlbedo->srv() );
	if( m_hasMilkyWay )
		d3d.setSRV( milkyWaySlot, m_milkyWay );
	NightSkyParameters night = currentNightSky( m_frameParams );
	Device::updateResourceData<NightSkyParameters>( m_nightSkyConstants, night );
	d3d.setConstantBuffer( 4, m_nightSkyConstants );
	// На дальней плоскости: только там, где сцена ничего не нарисовала
	m_backgroundShader.draw( BlendState::opaque, DepthState::readOnlyNearOrEqual );

	// Звёзды — поверх фона сложением, пока солнце под горизонтом: 6 вершин на звезду
	if( m_starCount > 0 && night.starIlluminanceScale > 0.0f )
	{
		d3d.setSRV( starsSlot, m_starsView );
		ScopedRenderState state( RasterState::noCulling, DepthState::readOnlyNearOrEqual, BlendState::additive );
		m_starShader.setPass( 0 );
		d3d.draw( 6 * m_starCount, 0 );
	}
}

PropertyContainer* SkyAtmosphere::properties()
{
	return &m_properties;
}

}
