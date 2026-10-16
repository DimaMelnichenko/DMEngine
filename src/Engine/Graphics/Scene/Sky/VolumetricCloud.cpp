#include "VolumetricCloud.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include "SkyAtmosphere.h"
#include "SceneTargets.h"
#include "Wind\Wind.h"
#include "D3D\DMD3D.h"
#include "Properties\SettingsProperties.h"
#include "ConstantBuffers.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr uint32_t shapeSize = 128;		// = mainShape в Shaders/cloud_noise.cs
constexpr uint32_t detailSize = 32;
constexpr uint32_t weatherSize = 512;
constexpr uint32_t shadowMapSize = 512;
constexpr float shadowMapWorldSize = 8000.0f;	// карта тени облаков вокруг камеры, м: тексель — 16 м
// Доля прошлого кадра: шум сдвига начала лучей гаснет за ~10 кадров
constexpr float historyWeight = 0.9f;
// Смена плана — камера прыгнула дальше за кадр: прошлый кадр не годится, сдвиг лучей — с начала
constexpr float cutDistance = 20.0f;

// Слоты проходов облаков (Shaders/volumetric_cloud.sh, volumetric_cloud.cs, cloud_composite.ps)
constexpr uint32_t historySlot = 0;
constexpr uint32_t shapeSlot = 6;
constexpr uint32_t detailSlot = 7;
constexpr uint32_t weatherSlot = 8;

bool createVolume( uint32_t width, uint32_t height, uint32_t depth, DXGI_FORMAT format, const char* name, Texture& texture,
				   StorageView& uav, ShaderView& srv )
{
	TextureDesc desc;
	desc.width = width;
	desc.height = height;
	desc.depth = depth;
	desc.format = format;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	DMD3D& d3d = DMD3D::instance();
	if( !d3d.createTexture( desc, nullptr, texture ) )
		return false;
	d3d.setName( texture, name );
	return d3d.createStorageView( texture, {}, uav ) && d3d.createShaderView( texture, {}, srv );
}

}

VolumetricCloud::VolumetricCloud() :
	SceneObject( "Volumetric cloud" )
{
}

bool VolumetricCloud::initialize( const Settings& settings, SkyAtmosphere& atmosphere, Wind& wind )
{
	m_atmosphere = &atmosphere;
	m_wind = &wind;
	if( !m_shapeShader.initialize( "Shaders\\cloud_noise.cs", "mainShape" ) ||
		!m_detailShader.initialize( "Shaders\\cloud_noise.cs", "mainDetail" ) ||
		!m_weatherShader.initialize( "Shaders\\cloud_noise.cs", "mainWeather" ) ||
		!m_traceShader.initialize( "Shaders\\volumetric_cloud.cs", "mainTrace" ) ||
		!m_shadowShader.initialize( "Shaders\\volumetric_cloud.cs", "mainShadow" ) ||
		!m_compositeShader.load( "Shaders\\cloud_composite.ps", SceneTargets::formats() ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( VolumetricCloudBuffer ), m_constants ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( CloudCompositeBuffer ), m_compositeConstants ) ||
		!createNoise() )
		return false;

	m_properties.setName( "Volumetric cloud" );
	m_properties.setPresetPart( true );
	m_properties.insert( "Enabled", true );
	addSettingProperties( m_properties, volumetricCloudSchema(), settings );
	m_initialized = true;
	return true;
}

bool VolumetricCloud::createNoise()
{
	return createVolume( shapeSize, shapeSize, shapeSize, DXGI_FORMAT_R8G8B8A8_UNORM, "Cloud shape noise", m_shape, m_shapeUAV, m_shapeSRV ) &&
		   createVolume( detailSize, detailSize, detailSize, DXGI_FORMAT_R8G8B8A8_UNORM, "Cloud detail noise", m_detail, m_detailUAV, m_detailSRV ) &&
		   createVolume( weatherSize, weatherSize, 1, DXGI_FORMAT_R8G8_UNORM, "Cloud weather map", m_weather, m_weatherUAV, m_weatherSRV ) &&
		   createVolume( shadowMapSize, shadowMapSize, 1, DXGI_FORMAT_R16_FLOAT, "Cloud shadow", m_shadow, m_shadowUAV, m_shadowSRV );
}

bool VolumetricCloud::createTraceTargets( uint32_t width, uint32_t height )
{
	for( uint32_t i = 0; i < 2; ++i )
	{
		if( !createVolume( width, height, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, i == 0 ? "Volumetric cloud A" : "Volumetric cloud B",
						   m_clouds[i], m_cloudsUAV[i], m_cloudsSRV[i] ) )
			return false;
	}
	m_traceSize[0] = width;
	m_traceSize[1] = height;
	m_historyValid = false;
	return true;
}

bool VolumetricCloud::enabled()
{
	return m_initialized && m_properties["Enabled"].data<bool>();
}

VolumetricCloud::Settings VolumetricCloud::settings()
{
	Settings settings;
	readSettingProperties( m_properties, volumetricCloudSchema(), settings );
	return settings;
}

void VolumetricCloud::apply( const Settings& settings )
{
	writeSettingProperties( m_properties, volumetricCloudSchema(), settings );
}

void VolumetricCloud::frameConstants( const RenderView& view, FrameParameters& constants )
{
	constants.cloudShadow = shadowParameters( view );
}

XMFLOAT4 VolumetricCloud::shadowParameters( const RenderView& view )
{
	const Settings settings = this->settings();
	if( !enabled() || settings.shadowStrength <= 0.0f )
		return XMFLOAT4( 0.0f, 0.0f, 0.0f, 0.0f );
	// Карта привязана к сетке текселей: при движении камеры тень не дрожит
	const float texel = shadowMapWorldSize / shadowMapSize;
	const float originX = std::floor( ( view.position.x - shadowMapWorldSize * 0.5f ) / texel ) * texel;
	const float originZ = std::floor( ( view.position.z - shadowMapWorldSize * 0.5f ) / texel ) * texel;
	const float middle = settings.layerBottomAltitude + 0.5f * settings.layerHeight;
	return XMFLOAT4( originX, originZ, 1.0f / shadowMapWorldSize, middle );
}

void VolumetricCloud::update( const FrameContext& frame )
{
	// Снос ветром — только пока ветер уровня дует: с -nowind облака и их тени стоят, кадры с одной точки совпадают
	if( !m_wind )
		return;
	const WindParameters wind = m_wind->parameters();
	if( wind.strength <= 0.0f )
		return;
	const float length = std::max( std::sqrt( wind.direction.x * wind.direction.x + wind.direction.y * wind.direction.y ), 1e-6f );
	const float distance = settings().windSpeed * frame.elapsedTime / 1000.0f;
	// Шум сдвигается против ветра — облака плывут по ветру
	m_windOffset.x -= wind.direction.x / length * distance;
	m_windOffset.y -= wind.direction.y / length * distance;
}

VolumetricCloudBuffer VolumetricCloud::currentParameters( const FrameContext& frame )
{
	const Settings settings = this->settings();
	VolumetricCloudBuffer params = {};
	params.previousViewProjection = XMMatrixTranspose( m_previousViewProjection );
	params.layerBottom = settings.layerBottomAltitude;
	params.layerTop = params.layerBottom + std::max( settings.layerHeight, 1.0f );
	params.coverage = settings.coverage;
	params.cloudDensity = settings.density;
	params.cloudAlbedo = settings.albedo;
	params.shapeScale = 1.0f / std::max( settings.shapeScale, 1.0f );
	params.windOffset = m_windOffset;
	params.detailScale = 1.0f / std::max( settings.detailScale, 1.0f );
	params.weatherScale = 1.0f / std::max( settings.weatherScale, 1.0f );
	m_atmosphere->cloudLight( params.lightDirection, params.lightColor );
	params.historyWeight = m_historyValid ? historyWeight : 0.0f;
	params.maxDistance = settings.tracingMaxDistance;
	const XMFLOAT4 shadow = shadowParameters( frame.view );
	params.shadowOrigin = XMFLOAT2( shadow.x, shadow.y );
	params.shadowSize = shadowMapWorldSize;
	params.shadowStrength = settings.shadowStrength;
	params.traceSize = XMUINT2( m_traceSize[0], m_traceSize[1] );
	params.frameIndex = m_frameIndex;
	params.outputScale = 1.0f / m_atmosphere->skyNormalization();
	return params;
}

void VolumetricCloud::generateNoise()
{
	DMD3D& d3d = DMD3D::instance();
	d3d.beginPass( PassDesc{ "Cloud noise", {}, {}, 0, 0, {},
							 { { &m_shapeUAV, "cloud shape noise" }, { &m_detailUAV, "cloud detail noise" }, { &m_weatherUAV, "cloud weather map" } } } );
	m_shapeShader.setUAVBuffer( 0, m_shapeUAV );
	m_shapeShader.dispatchGroups( shapeSize / 4, shapeSize / 4, shapeSize / 4 );
	m_detailShader.setUAVBuffer( 0, m_detailUAV );
	m_detailShader.dispatchGroups( detailSize / 4, detailSize / 4, detailSize / 4 );
	m_weatherShader.setUAVBuffer( 0, m_weatherUAV );
	m_weatherShader.dispatchGroups( weatherSize / 8, weatherSize / 8, 1 );
	m_noiseReady = true;
}

void VolumetricCloud::compute( const FrameContext& frame )
{
	if( !enabled() )
		return;
	if( !m_noiseReady )
		generateNoise();

	// Облака — в половине кадра (размер кадра мог смениться)
	DMD3D& d3d = DMD3D::instance();
	const uint32_t width = ( d3d.backBufferWidth() + 1 ) / 2;
	const uint32_t height = ( d3d.backBufferHeight() + 1 ) / 2;
	if( ( width != m_traceSize[0] || height != m_traceSize[1] ) && !createTraceTargets( width, height ) )
	{
		m_initialized = false;
		return;
	}

	// Камера прыгнула (команда camera, телепорт) — смена плана
	const XMFLOAT3& camera = frame.view.position;
	const float dx = camera.x - m_previousCamera.x;
	const float dy = camera.y - m_previousCamera.y;
	const float dz = camera.z - m_previousCamera.z;
	if( dx * dx + dy * dy + dz * dz > cutDistance * cutDistance )
	{
		m_historyValid = false;
		m_frameIndex = 0;
	}

	VolumetricCloudBuffer params = currentParameters( frame );
	const std::vector<PassDesc::Read> noiseReads = { { &m_shapeSRV, "cloud shape noise" }, { &m_detailSRV, "cloud detail noise" },
													 { &m_weatherSRV, "cloud weather map" } };
	const auto bindNoise = [&]
	{
		d3d.setSRV( shapeSlot, m_shapeSRV );
		d3d.setSRV( detailSlot, m_detailSRV );
		d3d.setSRV( weatherSlot, m_weatherSRV );
	};

	// 1. Тень облаков — к источнику теней сцены (солнце, ночью — луна)
	{
		VolumetricCloudBuffer shadowParams = params;
		shadowParams.lightDirection = frame.toShadowLight;
		Device::updateResourceData<VolumetricCloudBuffer>( m_constants, shadowParams );
		d3d.beginPass( PassDesc{ "Cloud shadow", {}, {}, 0, 0, noiseReads, { { &m_shadowUAV, "cloud shadow" } } } );
		d3d.setConstantBuffer( 4, m_constants );
		bindNoise();
		m_shadowShader.setUAVBuffer( 0, m_shadowUAV );
		m_shadowShader.dispatchGroups( shadowMapSize / 8, shadowMapSize / 8, 1 );
		// Всем приёмникам тени солнца (Shaders/cloud_shadow.sh): привязка завершает проход над картой
		d3d.setSRV( SLOT_CLOUD_SHADOW, m_shadowSRV );
	}

	// 2. Луч главного вида через слой; прошлый кадр — для смешения
	const uint32_t previous = m_current;
	m_current ^= 1;
	std::vector<PassDesc::Read> traceReads = noiseReads;
	traceReads.push_back( { &m_cloudsSRV[previous], "cloud history" } );
	traceReads.push_back( { &m_atmosphere->transmittanceLut(), "transmittance LUT" } );
	traceReads.push_back( { &m_atmosphere->multipleScatteringLut(), "multiple scattering LUT" } );
	Device::updateResourceData<VolumetricCloudBuffer>( m_constants, params );
	d3d.beginPass( PassDesc{ "Volumetric cloud", {}, {}, 0, 0, traceReads, { { &m_cloudsUAV[m_current], "volumetric cloud" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	m_atmosphere->bindForClouds();
	bindNoise();
	d3d.setSRV( historySlot, m_cloudsSRV[previous] );
	m_traceShader.setUAVBuffer( 0, m_cloudsUAV[m_current] );
	m_traceShader.dispatchGroups( ( m_traceSize[0] + 7 ) / 8, ( m_traceSize[1] + 7 ) / 8, 1 );
	// Для прохода sky (Shaders/cloud_composite.ps): привязка завершает проход над облаками
	d3d.setSRV( historySlot, m_cloudsSRV[m_current] );

	m_previousViewProjection = frame.view.viewProjection;
	m_previousCamera = camera;
	m_historyValid = true;
	++m_frameIndex;
}

void VolumetricCloud::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	if( enabled() && view.index == 0 )
		collector.addCustom( passBit( MeshPass::sky ) );
}

void VolumetricCloud::renderCustom( const RenderContext& )
{
	// Свет в текстуре — в единицах запекания неба, делённый на нормировку: в кд/м² — × освещённость от солнца × нормировка
	CloudCompositeBuffer params = {};
	params.cloudScale = m_atmosphere->skyNormalization();
	Device::updateResourceData<CloudCompositeBuffer>( m_compositeConstants, params );
	DMD3D& d3d = DMD3D::instance();
	d3d.setConstantBuffer( 4, m_compositeConstants );
	d3d.setSRV( 0, m_cloudsSRV[m_current] );
	// Поверх фона неба, только там, где сцена ничего не нарисовала
	m_compositeShader.draw( BlendState::alpha, DepthState::readOnlyNearOrEqual );
}

}
