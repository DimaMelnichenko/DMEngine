#include "PostProcess.h"
#include "Shaders\slots.h"
#include <algorithm>
#include <cmath>
#include "D3D\DMD3D.h"

namespace GS
{

PostProcess::Tonemapper PostProcess::tonemapperFromName( const std::string& name )
{
	return name == "None" ? Tonemapper::none : name == "ACES" ? Tonemapper::aces : Tonemapper::agx;
}

const char* PostProcess::tonemapperName( Tonemapper tonemapper )
{
	switch( tonemapper )
	{
		case Tonemapper::none: return "None";
		case Tonemapper::aces: return "ACES";
		default: return "AgX";
	}
}

bool PostProcess::initialize( const Settings& settings )
{
	if( !m_shader.load( "Shaders\\tonemap.ps" ) )
		return false;

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( Parameters ), m_constantBuffer ) )
		return false;

	m_properties.setName( "Post process" );

	auto prop = m_properties.insert( "Exposure compensation (EV)", settings.exposureCompensation );
	prop->setLow( -6.0f );
	prop->setHigh( 6.0f );
	prop->setControlType( GUIControlType::SLIDER );

	prop = m_properties.insert( "Tonemapper (0 none, 1 ACES, 2 AgX)", static_cast<int32_t>( settings.tonemapper ) );
	prop->setLow( 0.0f );
	prop->setHigh( 2.0f );
	prop->setControlType( GUIControlType::SLIDER );

	return true;
}

PostProcess::Settings PostProcess::settings()
{
	Settings settings;
	settings.exposureCompensation = m_properties["Exposure compensation (EV)"].data<float>();
	settings.tonemapper = static_cast<Tonemapper>( std::clamp( m_properties["Tonemapper (0 none, 1 ACES, 2 AgX)"].data<int32_t>(), 0, 2 ) );
	return settings;
}

void PostProcess::render()
{
	DMD3D& d3d = DMD3D::instance();
	const com_unique_ptr<ID3D11ShaderResourceView>& sceneColor = d3d.sceneColor();
	d3d.setBackBufferTarget();

	Parameters params = {};
	params.exposure = std::exp2( m_properties["Exposure compensation (EV)"].data<float>() );
	params.tonemapper = m_properties["Tonemapper (0 none, 1 ACES, 2 AgX)"].data<int32_t>();
	Device::updateResourceData<Parameters>( m_constantBuffer.get(), params );
	d3d.setConstantBuffer( SRVType::ps, SLOT_CB_PASS, m_constantBuffer );
	d3d.setSRV( SRVType::ps, 0, sceneColor );

	m_shader.draw();
}

PropertyContainer* PostProcess::properties()
{
	return &m_properties;
}

}
