#include "Wind.h"
#include <algorithm>
#include <cmath>

namespace GS
{

namespace
{

void addSlider( PropertyContainer& properties, const char* name, float value, float low, float high )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
}

}

Wind::Wind()
{
	m_properties.setName( "Wind" );
}

void Wind::initialize( const std::optional<Settings>& settings )
{
	// Без строки Wind у уровня — ветра нет (сила 0), но окно есть: ветер можно включить и сохранить с уровнем
	const Settings initial = settings ? *settings : Settings();
	// Направление в GUI — Yaw, как у источников света: 0 — на север (+Z), 90 — на восток (+X)
	addSlider( m_properties, "Yaw", XMConvertToDegrees( std::atan2( initial.direction.x, initial.direction.z ) ), -180.0f, 180.0f );
	addSlider( m_properties, "Strength", initial.strength, 0.0f, 2.0f );
	addSlider( m_properties, "Speed (m/s)", initial.speed, 0.0f, 20.0f );
	addSlider( m_properties, "Min gust amount", initial.minGustAmount, 0.0f, 2.0f );
	addSlider( m_properties, "Max gust amount", initial.maxGustAmount, 0.0f, 2.0f );
	addSlider( m_properties, "Gust size (m)", initial.gustSize, 1.0f, 200.0f );
}

Wind::Settings Wind::settings()
{
	Settings settings;
	const float yaw = XMConvertToRadians( m_properties["Yaw"].data<float>() );
	settings.direction = XMFLOAT3( std::sin( yaw ), 0.0f, std::cos( yaw ) );
	settings.strength = m_properties["Strength"].data<float>();
	settings.speed = m_properties["Speed (m/s)"].data<float>();
	settings.minGustAmount = m_properties["Min gust amount"].data<float>();
	settings.maxGustAmount = m_properties["Max gust amount"].data<float>();
	settings.gustSize = m_properties["Gust size (m)"].data<float>();
	return settings;
}

WindParameters Wind::parameters()
{
	const Settings current = settings();
	WindParameters parameters;
	parameters.direction = XMFLOAT2( current.direction.x, current.direction.z );
	parameters.strength = std::max( current.strength, 0.0f );
	parameters.speed = current.speed;
	parameters.gustMin = current.minGustAmount;
	parameters.gustMax = std::max( current.maxGustAmount, current.minGustAmount );
	parameters.gustSize = std::max( current.gustSize, 1.0f );
	return parameters;
}

void Wind::disable()
{
	m_properties["Strength"].setData( 0.0f );
}

PropertyContainer* Wind::properties()
{
	return &m_properties;
}

}
