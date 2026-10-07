#include "VolumetricFog.h"
#include "Shaders\slots.h"
#include "Shaders\fog_constants.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"

using namespace DirectX;

namespace GS
{

namespace
{

constexpr DXGI_FORMAT volumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;	// свет — с нормировкой, половинной точности хватает
// Доля прошлого кадра в ячейке (r.VolumetricFog.HistoryWeight в UE): шум сдвига точки гаснет за ~10 кадров
constexpr float historyWeight = 0.9f;
constexpr uint32_t jitterPeriod = 16;
// Объём начинается чуть дальше ближней плоскости (NearOffset в UE): у самой камеры слои не тратятся
constexpr float nearOffset = 0.1f;

// Плотность в окне — в 1/км: у дымки она тысячные доли 1/м, ползунок с тремя знаками их не показал бы
const char* const densityProperty = "Density (per km)";
const char* const secondDensityProperty = "Second density (per km)";

// Последовательность Халтона: сдвиги точки в ячейке по кадрам равномерно заполняют её
float halton( uint32_t index, uint32_t base )
{
	float result = 0.0f;
	float fraction = 1.0f / base;
	while( index > 0 )
	{
		result += fraction * ( index % base );
		index /= base;
		fraction /= base;
	}
	return result;
}

void addSlider( PropertyContainer& properties, const char* name, float value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
}

}

bool VolumetricFog::initialize( const std::optional<Settings>& settings )
{
	if( !m_lightShader.Initialize( "Shaders\\volumetric_fog.cs", "mainLight" ) ||
		!m_integrateShader.Initialize( "Shaders\\volumetric_fog.cs", "mainIntegrate" ) ||
		!DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constants ) ||
		!createVolumes() )
		return false;

	m_hasRow = settings.has_value();
	const Settings values = settings.value_or( Settings() );
	m_properties.setName( "Height fog" );
	// Строка без плотности — туман выключен (так его сохраняет снятый флажок)
	m_properties.insert( "Enabled", m_hasRow && ( values.layer.density > 0.0f || values.secondLayer.density > 0.0f ) );
	addSlider( m_properties, densityProperty, values.layer.density * 1000.0f, 0.0f, 20.0f );
	addSlider( m_properties, "Height", values.layer.height, -100.0f, 500.0f );
	addSlider( m_properties, "Height falloff", values.layer.heightFalloff, 0.0f, 0.5f );
	addSlider( m_properties, secondDensityProperty, values.secondLayer.density * 1000.0f, 0.0f, 200.0f );
	addSlider( m_properties, "Second height", values.secondLayer.height, -100.0f, 500.0f );
	addSlider( m_properties, "Second height falloff", values.secondLayer.heightFalloff, 0.0f, 1.0f );
	m_properties.insert( "Albedo", values.albedo )->setControlType( GUIControlType::COLOR );
	addSlider( m_properties, "Scattering distribution", values.scatteringDistribution, -0.9f, 0.9f );
	m_properties.insert( "Volumetric", values.volumetric );
	addSlider( m_properties, "View distance", values.viewDistance, 20.0f, 1000.0f );
	return true;
}

bool VolumetricFog::createVolumes()
{
	// Сетка — по размеру кадра: ячейка VOLUMETRIC_FOG_TILE² пикселей
	DMD3D& d3d = DMD3D::instance();
	m_gridSize[0] = ( d3d.backBufferWidth() + VOLUMETRIC_FOG_TILE - 1 ) / VOLUMETRIC_FOG_TILE;
	m_gridSize[1] = ( d3d.backBufferHeight() + VOLUMETRIC_FOG_TILE - 1 ) / VOLUMETRIC_FOG_TILE;
	m_gridSize[2] = VOLUMETRIC_FOG_DEPTH;

	TextureDesc desc;
	desc.width = m_gridSize[0];
	desc.height = m_gridSize[1];
	desc.depth = m_gridSize[2];
	desc.format = volumeFormat;
	desc.usage = TextureUsage::unorderedAccess | TextureUsage::shaderResource;
	for( uint32_t i = 0; i < 2; ++i )
	{
		if( !d3d.createTexture( desc, nullptr, m_lighting[i] ) ||
			!d3d.createStorageView( m_lighting[i], {}, m_lightingUAV[i] ) ||
			!d3d.createShaderView( m_lighting[i], {}, m_lightingSRV[i] ) )
			return false;
		d3d.setName( m_lighting[i], i == 0 ? "Volumetric fog lighting A" : "Volumetric fog lighting B" );
	}
	if( !d3d.createTexture( desc, nullptr, m_integrated ) ||
		!d3d.createStorageView( m_integrated, {}, m_integratedUAV ) ||
		!d3d.createShaderView( m_integrated, {}, m_integratedSRV ) )
		return false;
	d3d.setName( m_integrated, "Volumetric fog" );
	m_historyValid = false;
	return true;
}

bool VolumetricFog::resize()
{
	return createVolumes();
}

bool VolumetricFog::enabled()
{
	return m_properties["Enabled"].data<bool>();
}

bool VolumetricFog::active()
{
	return enabled() && m_properties["Volumetric"].data<bool>();
}

void VolumetricFog::cameraCut()
{
	m_historyValid = false;
	m_jitterIndex = 0;
}

FogParameters VolumetricFog::frameParameters( const RenderView& view, float normalization )
{
	FogParameters fog;
	if( !enabled() )
		return fog;

	PropertyContainer& p = m_properties;
	fog.layer0 = XMFLOAT4( p[densityProperty].data<float>() / 1000.0f, p["Height"].data<float>(),
						   p["Height falloff"].data<float>(), view.farPlane );
	fog.layer1 = XMFLOAT4( p[secondDensityProperty].data<float>() / 1000.0f, p["Second height"].data<float>(),
						   p["Second height falloff"].data<float>(), p["Scattering distribution"].data<float>() );
	fog.albedo = p["Albedo"].data<XMFLOAT3>();
	m_scale = std::max( normalization, 1e-6f );
	fog.scale = m_scale;

	if( active() )
	{
		// Слои по глубине, как GetVolumetricFogGridZParams в UE: слой = log₂(z · B + O) · S, слой 0 — на near,
		// последний — на дальности объёма
		const double nearDepth = view.nearPlane + nearOffset;
		const double farDepth = std::max<double>( p["View distance"].data<float>(), nearDepth + 1.0 );
		const double S = VOLUMETRIC_FOG_DISTRIBUTION;
		const double O = ( farDepth - nearDepth * std::exp2( VOLUMETRIC_FOG_DEPTH / S ) ) / ( farDepth - nearDepth );
		const double B = ( 1.0 - O ) / nearDepth;
		fog.gridZ = XMFLOAT3( static_cast<float>( B ), static_cast<float>( O ), static_cast<float>( S ) );
		fog.volumeDistance = static_cast<float>( farDepth );
	}
	return fog;
}

void VolumetricFog::render( const RenderView& view, const ShaderView& shadowMap )
{
	if( !active() )
	{
		m_historyValid = false;
		return;
	}

	const uint32_t previous = m_current;
	m_current ^= 1;

	// Раскладка — VolumetricFogBuffer в Shaders/volumetric_fog.cs
	Parameters params = {};
	params.previousViewProjection = XMMatrixTranspose( m_previousViewProjection );
	const uint32_t jitter = m_jitterIndex % jitterPeriod + 1;
	params.jitter = XMFLOAT3( halton( jitter, 2 ), halton( jitter, 3 ), halton( jitter, 5 ) );
	params.historyWeight = m_historyValid ? historyWeight : 0.0f;
	std::copy( std::begin( m_gridSize ), std::end( m_gridSize ), params.gridSize );
	params.historyScale = m_previousScale / m_scale;
	params.inverseScale = 1.0f / m_scale;
	Device::updateResourceData<Parameters>( m_constants, params );

	DMD3D& d3d = DMD3D::instance();
	// Свет в ячейках: карта теней — после её прохода и привязки, прошлый кадр — для смешения
	d3d.beginPass( PassDesc{ "Volumetric fog light", {}, {}, 0, 0,
							 { { &shadowMap, "shadow map" }, { &m_lightingSRV[previous], "fog lighting history" } },
							 { { &m_lightingUAV[m_current], "fog lighting" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 0, m_lightingSRV[previous] );
	m_lightShader.setUAVBuffer( 0, m_lightingUAV[m_current] );
	constexpr uint32_t lightGroup = 4;		// = numthreads mainLight
	m_lightShader.dispatchGroups( ( m_gridSize[0] + lightGroup - 1 ) / lightGroup, ( m_gridSize[1] + lightGroup - 1 ) / lightGroup,
								  ( m_gridSize[2] + lightGroup - 1 ) / lightGroup );

	// Накопление вдоль лучей: поток — столбец сетки
	d3d.beginPass( PassDesc{ "Volumetric fog integrate", {}, {}, 0, 0,
							 { { &m_lightingSRV[m_current], "fog lighting" } },
							 { { &m_integratedUAV, "volumetric fog" } } } );
	d3d.setConstantBuffer( 4, m_constants );
	d3d.setSRV( 1, m_lightingSRV[m_current] );
	m_integrateShader.setUAVBuffer( 0, m_integratedUAV );
	constexpr uint32_t integrateGroup = 8;	// = numthreads mainIntegrate
	m_integrateShader.dispatchGroups( ( m_gridSize[0] + integrateGroup - 1 ) / integrateGroup,
									  ( m_gridSize[1] + integrateGroup - 1 ) / integrateGroup, 1 );

	// Проходам сцены (applyHeightFog): привязка завершает проход над объёмом
	d3d.setSRV( SLOT_VOLUMETRIC_FOG, m_integratedSRV );

	m_previousViewProjection = view.viewProjection;
	m_previousScale = m_scale;
	m_historyValid = true;
	++m_jitterIndex;
}

std::optional<VolumetricFog::Settings> VolumetricFog::settings()
{
	if( !m_hasRow && !enabled() )
		return std::nullopt;

	PropertyContainer& p = m_properties;
	Settings settings;
	settings.layer = { p[densityProperty].data<float>() / 1000.0f, p["Height"].data<float>(), p["Height falloff"].data<float>() };
	settings.secondLayer = { p[secondDensityProperty].data<float>() / 1000.0f, p["Second height"].data<float>(),
							 p["Second height falloff"].data<float>() };
	settings.albedo = p["Albedo"].data<XMFLOAT3>();
	settings.scatteringDistribution = p["Scattering distribution"].data<float>();
	settings.volumetric = p["Volumetric"].data<bool>();
	settings.viewDistance = p["View distance"].data<float>();
	// Выключенный в окне туман сохраняется строкой без плотности: строка уровня остаётся, тумана нет
	if( !enabled() )
		settings.layer.density = settings.secondLayer.density = 0.0f;
	return settings;
}

}
