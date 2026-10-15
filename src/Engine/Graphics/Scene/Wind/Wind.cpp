#include "Wind.h"
#include "Properties\SettingsProperties.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace GS
{

namespace
{

Property* addSlider( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit = "" )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setControlType( GUIControlType::SLIDER );
	property->setUnit( unit );
	return property;
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
	// Выключатель кадра, не данные уровня: -nowind останавливает ветер им, а сила остаётся — сохранение её не затрёт
	m_properties.insert( "Enabled", true )->setTooltip( "Off - plants and clouds stand still (-nowind); not saved with the level" );
	// Направление в GUI — Yaw, как у источников света: 0 — на север (+Z), 90 — на восток (+X)
	addSlider( m_properties, "Yaw", XMConvertToDegrees( std::atan2( initial.direction.x, initial.direction.z ) ), -180.0f, 180.0f, "deg" );
	addSettingProperties( m_properties, windSchema(), initial );
	// Поле ветра по рельефу — подокно «Field» по описанию (windFieldSchema): правка пересчитывает поле, когда закончилась
	// (Scene::applyEditorChanges)
	m_fieldProperties.setName( "Field" );
	m_fieldProperties.insert( "Use field", true )->setTooltip( "Off - plants, ripples and particles take the even wind of the level "
															   "(to compare); not saved with the level" );
	addSettingProperties( m_fieldProperties, windFieldSchema(), initial.field );
	m_properties.addSubContainer( &m_fieldProperties );
}

Wind::Settings Wind::settings()
{
	Settings settings;
	const float yaw = XMConvertToRadians( m_properties["Yaw"].data<float>() );
	settings.direction = XMFLOAT3( std::sin( yaw ), 0.0f, std::cos( yaw ) );
	readSettingProperties( m_properties, windSchema(), settings );
	readSettingProperties( m_fieldProperties, windFieldSchema(), settings.field );
	return settings;
}

WindParameters Wind::parameters()
{
	const Settings current = settings();
	WindParameters parameters;
	parameters.direction = XMFLOAT2( current.direction.x, current.direction.z );
	parameters.strength = m_properties["Enabled"].data<bool>() ? std::max( current.strength, 0.0f ) : 0.0f;
	parameters.speed = current.speed;
	parameters.gustMin = current.minGustAmount;
	parameters.gustMax = std::max( current.maxGustAmount, current.minGustAmount );
	parameters.gustSize = std::max( current.gustSize, 1.0f );
	return parameters;
}

void Wind::disable()
{
	m_properties["Enabled"].setData( false );
}

bool Wind::fieldEnabled()
{
	return m_fieldProperties["Use field"].data<bool>();
}

PropertyContainer* Wind::properties()
{
	return &m_properties;
}

}
