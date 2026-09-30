#include "HDRIBackdrop.h"
#include "D3D\TextureImages.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <DirectXTex.h>
#include "D3D\DMD3D.h"
#include "Logger\Logger.h"
#include "TextureObjects\TextureLoader.h"

namespace GS
{

namespace
{

constexpr float pi = 3.14159265f;

// Сводка по панораме (мип 0, R32G32B32A32_FLOAT) — в лог: по ней подбираются intensity, max_luminance и солнце уровня.
// «Солнце» — пиксели ярче 100 × 99-го процентиля яркости; освещённость от него — на площадку поперёк лучей, от неба —
// на горизонтальную площадку, обе при intensity 1 (в единицах панорамы)
void logStatistics( const Image& image, float rotationDegrees )
{
	const size_t width = image.width;
	const size_t height = image.height;
	std::vector<float> luminance( width * height );
	size_t brightest = 0;
	for( size_t y = 0; y < height; ++y )
	{
		const float* row = reinterpret_cast<const float*>( image.pixels + y * image.rowPitch );
		for( size_t x = 0; x < width; ++x )
		{
			const float* pixel = row + x * 4;
			const size_t index = y * width + x;
			luminance[index] = 0.2126f * pixel[0] + 0.7152f * pixel[1] + 0.0722f * pixel[2];
			if( luminance[index] > luminance[brightest] )
				brightest = index;
		}
	}

	auto percentile = [&luminance]( double fraction )
	{
		std::vector<float> values = luminance;
		const size_t n = std::min( static_cast<size_t>( fraction * values.size() ), values.size() - 1 );
		std::nth_element( values.begin(), values.begin() + n, values.end() );
		return values[n];
	};
	const float median = percentile( 0.5 );
	const float p99 = percentile( 0.99 );
	const float p9999 = percentile( 0.9999 );
	const float sunThreshold = 100.0f * p99;

	double sunIlluminance = 0.0;
	double skyIlluminance = 0.0;
	for( size_t y = 0; y < height; ++y )
	{
		const double theta = ( y + 0.5 ) / height * pi;
		const double solidAngle = ( 2.0 * pi / width ) * ( pi / height ) * std::sin( theta );
		for( size_t x = 0; x < width; ++x )
		{
			const float value = luminance[y * width + x];
			if( value > sunThreshold )
				sunIlluminance += value * solidAngle;
			else if( theta < 0.5 * pi )
				skyIlluminance += value * solidAngle * std::cos( theta );
		}
	}

	// Самый яркий пиксель — направление на солнце в мире (с поворотом панорамы), как Pitch / Yaw источника
	const float u = ( brightest % width + 0.5f ) / width;
	const float theta = ( brightest / width + 0.5f ) / height * pi;
	const float yaw = ( u - 0.5f ) * 2.0f * pi + XMConvertToRadians( rotationDegrees );
	const float elevation = 0.5f * pi - theta;
	const XMFLOAT3 toSun( std::cos( elevation ) * std::sin( yaw ), std::sin( elevation ), std::cos( elevation ) * std::cos( yaw ) );

	char text[512];
	std::snprintf( text, sizeof( text ),
				   "HDRI backdrop: %zux%zu, luminance median %.4g, p99 %.4g, p99.99 %.4g, max %.4g; brightest at elevation %.2f, "
				   "yaw %.2f (light direction %.4f,%.4f,%.4f); sun (> %.4g) illuminance %.4g, sky on horizontal %.4g at intensity 1; "
				   "intensity for a 100000 lx sun: %.4g",
				   width, height, median, p99, p9999, luminance[brightest], XMConvertToDegrees( elevation ),
				   XMConvertToDegrees( yaw ), -toSun.x, -toSun.y, -toSun.z, sunThreshold, sunIlluminance, skyIlluminance,
				   sunIlluminance > 0.0 ? 100000.0 / sunIlluminance : 0.0 );
	LOG( text );
}

template<class TYPE>
void addControl( PropertyContainer& properties, const char* name, const TYPE& value, GUIControlType control, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setControlType( control );
	property->setLow( low );
	property->setHigh( high );
}

}

HDRIBackdrop::HDRIBackdrop() :
	SceneObject( "HDRI backdrop" )
{
}

bool HDRIBackdrop::initialize( const Settings& settings, SkyLight& skyLight )
{
	m_skyLight = &skyLight;
	m_texture = settings.texture;

	if( !loadPanorama( "Textures\\" + settings.texture, settings.rotation ) )
		return false;

	// Цели для прогрева: грани cubemap источника (формат SkyLight::createSource), фон — буфер сцены
	if( !m_cubeShader.load( "Shaders\\hdri_cube.ps", TargetFormats::colorTarget( DXGI_FORMAT_R32G32B32A32_FLOAT ) ) ||
		!m_backgroundShader.load( "Shaders\\hdri_background.ps", DMD3D::sceneFormats() ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) ||
		!SkyLight::createSource( m_cube ) )
		return false;

	m_properties.setName( "HDRI backdrop" );
	// Границы 0, 0 — без ограничения
	addControl( m_properties, "Intensity", settings.intensity, GUIControlType::DRAG, 0.0f, 0.0f );
	addControl( m_properties, "Rotation", settings.rotation, GUIControlType::SLIDER, -180.0f, 180.0f );
	addControl( m_properties, "Max luminance", settings.maxLuminance, GUIControlType::DRAG, 0.0f, 0.0f );
	return true;
}

bool HDRIBackdrop::loadPanorama( const std::string& path, float rotation )
{
	ScratchImage image;
	TextureLoader loader;
	if( !loader.loadFromFile( path.c_str(), image ) )
	{
		LOG( "HDRI backdrop: can't load " + path );
		return false;
	}

	// Мипы и сводка — по float: сжатое (BC6H) распаковывается, прочее переводится в R32G32B32A32_FLOAT
	constexpr DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	if( image.GetMetadata().format != format )
	{
		ScratchImage converted;
		const HRESULT hr = IsCompressed( image.GetMetadata().format ) ?
			Decompress( image.GetImages(), image.GetImageCount(), image.GetMetadata(), format, converted ) :
			Convert( image.GetImages(), image.GetImageCount(), image.GetMetadata(), format, TEX_FILTER_DEFAULT,
					 TEX_THRESHOLD_DEFAULT, converted );
		if( FAILED( hr ) )
		{
			LOG( "HDRI backdrop: can't convert " + path + " to float" );
			return false;
		}
		image = std::move( converted );
	}

	logStatistics( *image.GetImage( 0, 0, 0 ), rotation );

	if( image.GetMetadata().mipLevels == 1 )
	{
		ScratchImage mips;
		if( FAILED( GenerateMipMaps( *image.GetImage( 0, 0, 0 ), TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, mips ) ) )
		{
			LOG( "HDRI backdrop: can't generate mips for " + path );
			return false;
		}
		image = std::move( mips );
	}

	if( !GpuImages::createTexture( image, m_panoramaTexture, m_panorama ) )
	{
		LOG( "HDRI backdrop: can't create texture for " + path );
		return false;
	}
	DMD3D::instance().setName( m_panoramaTexture, "HDRI panorama" );
	m_panoramaWidth = static_cast<uint32_t>( image.GetMetadata().width );
	return true;
}

HDRIBackdrop::Settings HDRIBackdrop::settings()
{
	Settings settings;
	settings.texture = m_texture;
	settings.intensity = m_properties["Intensity"].data<float>();
	settings.rotation = m_properties["Rotation"].data<float>();
	settings.maxLuminance = m_properties["Max luminance"].data<float>();
	return settings;
}

float HDRIBackdrop::intensity()
{
	return m_properties["Intensity"].data<float>();
}

void HDRIBackdrop::setBackgroundVisible( bool visible )
{
	m_backgroundVisible = visible;
}

HDRIBackdrop::Parameters HDRIBackdrop::currentParameters()
{
	Parameters params = {};
	params.rotation = XMConvertToRadians( m_properties["Rotation"].data<float>() );
	params.maxLuminance = std::max( m_properties["Max luminance"].data<float>(), 0.0f );
	// Мип панорамы, у которого тексель по долготе (2π / ширина) — как тексель грани у центра (π/2 / размер)
	params.lod = std::max( std::log2( static_cast<float>( m_panoramaWidth ) / ( 4.0f * SkyLight::sourceSize ) ), 0.0f );
	return params;
}

void HDRIBackdrop::compute( const FrameContext& )
{
	// Cubemap и освещение окружением — только при смене поворота или среза: интенсивность — множитель при выборке
	const Parameters params = currentParameters();
	if( !m_environmentValid || std::memcmp( &params, &m_computedFor, sizeof( Parameters ) ) != 0 )
	{
		updateEnvironment( params );
		m_computedFor = params;
		m_environmentValid = true;
	}
	m_skyLight->bind();
}

void HDRIBackdrop::updateEnvironment( const Parameters& params )
{
	DMD3D& d3d = DMD3D::instance();

	// Грани cubemap из панорамы, затем освещение окружением (мипы строит SkyLight в compute)
	Parameters faceParams = params;
	for( int32_t face = 0; face < 6; ++face )
	{
		faceParams.face = face;
		setParameters( faceParams );
		d3d.beginPass( PassDesc{ "HDRI cube face", { { &m_cube.target( 0, face ), "HDRI cube" } }, {}, m_cube.size(), m_cube.size(),
								 { { &m_panorama, "panorama" } } } );
		d3d.setSRV( SRVType::ps, 0, m_panorama );
		m_cubeShader.draw();
	}

	m_skyLight->capture( m_cube );
}

void HDRIBackdrop::setParameters( const Parameters& params )
{
	Parameters data = params;
	Device::updateResourceData<Parameters>( m_constantBuffer, data );
	DMD3D::instance().setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
}

void HDRIBackdrop::collectMeshes( const RenderView&, MeshCollector& collector )
{
	if( m_backgroundVisible )
		collector.addCustom( passBit( MeshPass::sky ) );
}

void HDRIBackdrop::renderCustom( const RenderContext& )
{
	setParameters( m_computedFor );
	DMD3D::instance().setSRV( SRVType::ps, 0, m_panorama );
	// На дальней плоскости: только там, где сцена ничего не нарисовала
	m_backgroundShader.draw( BlendState::opaque, DepthState::readOnlyNearOrEqual );
}

PropertyContainer* HDRIBackdrop::properties()
{
	return &m_properties;
}

}
