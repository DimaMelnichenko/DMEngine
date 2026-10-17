#include "DMLightDriver.h"
#include "Properties\SettingsProperties.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Logger\Logger.h"

using namespace DirectX;

namespace
{

template<class TYPE>
void addControl( PropertyContainer& properties, const char* name, const TYPE& value, GUIControlType control, float low, float high,
				 const char* unit = "" )
{
	Property* property = properties.insert( name, value );
	property->setControlType( control );
	property->setLow( low );
	property->setHigh( high );
	property->setUnit( unit );
}

}

DMLightDriver::DMLightDriver()
{
	m_properties.setName( "Lights" );
}


DMLightDriver::~DMLightDriver(void)
{
}

bool DMLightDriver::Initialize()
{
	m_structBuffer.createBuffer( sizeof( LightBuffer ), maxLights, "Lights" );
	m_lightParamBuffer.reserve( maxLights );

	return true;
}

void DMLightDriver::load( LightList lights, GS::WorldClock& clock, const std::optional<SunPosition::Settings>& sunPosition )
{
	// Устойчивая сортировка: направленные первыми в порядке базы, солнце — первый включённый из них
	std::stable_sort( lights.begin(), lights.end(), []( const DMLight& a, const DMLight& b )
	{
		return (int)a.type() < (int)b.type();
	} );
	m_light_list = std::move( lights );

	m_controls.clear();
	m_removed.clear();
	m_sunPosition.reset();
	m_sunPositionLight = -1;
	m_moonPositionLight = -1;
	if( sunPosition )
	{
		if( !m_light_list.empty() && m_light_list[0].type() == DMLight::Dir )
		{
			m_sunPosition = std::make_unique<SunPosition>( *sunPosition, clock );
			m_sunPositionLight = 0;
			const SunPosition::Angles angles = m_sunPosition->angles();
			LOG( "Sun position: elevation " + std::to_string( angles.elevation ) + ", azimuth " + std::to_string( angles.azimuth ) );
			for( uint32_t i = 1; i < m_light_list.size(); ++i )
			{
				if( m_light_list[i].type() == DMLight::Dir && m_light_list[i].atmosphereSunLightIndex() == 1 )
				{
					m_moonPositionLight = static_cast<int>( i );
					break;
				}
			}
			const SunPosition::Moon moon = m_sunPosition->moon();
			LOG( "Moon: elevation " + std::to_string( moon.angles.elevation ) + ", azimuth " + std::to_string( moon.angles.azimuth ) +
				 ", illuminated " + std::to_string( moon.illuminatedFraction ) + ", " + std::to_string( moon.illuminance ) + " lx" +
				 ", libration " + std::to_string( moon.librationLongitude ) + ", " + std::to_string( moon.librationLatitude ) +
				 ", axis " + std::to_string( moon.positionAngle ) +
				 ( m_moonPositionLight < 0 ? " (no light with atmosphere_sun_light_index 1)" : "" ) );
			// Проверка рядов по примеру 47.a Meeus: 12.04.1992 0h TD — λ 133,16°, β −3,23°, Δ 368 410 км
			SunPosition::Settings check;
			check.year = 1992;
			check.month = 4;
			check.day = 12;
			check.timeOfDay = 0.0f;
			const SunPosition::Moon example = SunPosition::computeMoon( check );
			LOG( "Moon check (Meeus 47.a: 133.16, -3.23, 368410): " + std::to_string( example.eclipticLongitude ) + ", " +
				 std::to_string( example.eclipticLatitude ) + ", " + std::to_string( example.distance ) );
			// Либрация и позиционный угол оси — пример 53.a того же момента: l −1,206°, b 4,194°, P 15,08° (с точной долготой
			// луны; у наших рядов долгота на 0,09° больше — l на ~0,09° больше)
			LOG( "Moon libration check (Meeus 53.a: -1.206, 4.194, 15.08): " + std::to_string( example.librationLongitude ) + ", " +
				 std::to_string( example.librationLatitude ) + ", " + std::to_string( example.positionAngle ) );
		}
		else
			LOG( "Level has a sun position, but no directional light to move" );
	}
	for( uint32_t i = 0; i < m_light_list.size(); ++i )
	{
		const TimeRole role = static_cast<int>( i ) == m_sunPositionLight ? TimeRole::sun :
							  static_cast<int>( i ) == m_moonPositionLight ? TimeRole::moon : TimeRole::none;
		m_controls.push_back( createProperties( m_light_list[i], m_nextKey++, role ) );
	}
	updateStructure();

	if( m_light_list.size() > maxLights )
		LOG( "Level has " + std::to_string( m_light_list.size() ) + " lights, only " + std::to_string( maxLights ) + " enabled ones are used" );
}

XMFLOAT3 DMLightDriver::directionFromRotation( const XMFLOAT2& rotation )
{
	const float pitch = XMConvertToRadians( rotation.x );
	const float yaw = XMConvertToRadians( rotation.y );
	return XMFLOAT3( std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ), std::cos( pitch ) * std::cos( yaw ) );
}

XMFLOAT2 DMLightDriver::rotationFromDirection( const XMFLOAT3& direction )
{
	return XMFLOAT2( XMConvertToDegrees( std::asin( std::clamp( direction.y, -1.0f, 1.0f ) ) ),
					 XMConvertToDegrees( std::atan2( direction.x, direction.z ) ) );
}

PropertyContainer* DMLightDriver::lightProperties( size_t index )
{
	return index < m_controls.size() ? m_controls[index].properties.get() : nullptr;
}

bool DMLightDriver::directionFromTime( size_t index ) const
{
	return static_cast<int>( index ) == m_sunPositionLight || static_cast<int>( index ) == m_moonPositionLight;
}

bool DMLightDriver::moonFromTime( size_t index ) const
{
	return static_cast<int>( index ) == m_moonPositionLight;
}

PropertyContainer* DMLightDriver::sunPositionProperties()
{
	return m_sunPosition ? m_sunPosition->properties() : nullptr;
}

DMLightDriver::LightControls DMLightDriver::createProperties( const DMLight& light, uint32_t key, TimeRole role )
{
	LightControls controls;
	controls.key = key;
	controls.role = role;
	// Имя с номером в списке — updateStructure
	controls.properties = std::make_unique<PropertyContainer>( light.name );
	PropertyContainer& properties = *controls.properties;

	properties.insert( "Enabled", light.enabled() );
	if( light.type() == DMLight::Dir )
		properties.insert( "Atmosphere sun light", light.atmosphereSunLight() );
	addControl( properties, "Color", light.color(), GUIControlType::COLOR, 0.0f, 1.0f );
	// Направленный — люксы (полуденное солнце ~100 000), точечный и прожектор — канделы (лампа 100 Вт ~130).
	// Освещённость от луны при SunPosition — расчётная (фаза, расстояние): только для чтения
	addControl( properties, "Intensity", light.intensity(), role == TimeRole::moon ? GUIControlType::LABEL : GUIControlType::SLIDER, 0.0f,
				light.type() == DMLight::Dir ? 150000.0f : 20000.0f, light.type() == DMLight::Dir ? "lx" : "cd" );

	controls.rotation = rotationFromDirection( light.direction() );
	// Направление солнца и луны со временем суток задаёт SunPosition
	if( light.type() != DMLight::Point && role == TimeRole::none )
	{
		addControl( properties, "Pitch", controls.rotation.x, GUIControlType::SLIDER, -90.0f, 90.0f, "deg" );
		addControl( properties, "Yaw", controls.rotation.y, GUIControlType::SLIDER, -180.0f, 180.0f, "deg" );
	}
	if( light.type() != DMLight::Dir )
	{
		// Границы 0, 0 — без ограничения
		addControl( properties, "Position", light.position(), GUIControlType::DRAG, 0.0f, 0.0f, "m" );
		addControl( properties, "Attenuation radius", light.attenuationRadius(), GUIControlType::SLIDER, 0.0f, 500.0f, "m" );
	}
	if( light.type() == DMLight::Spot )
	{
		addControl( properties, "Inner cone angle", light.innerConeAngle(), GUIControlType::SLIDER, 0.0f, 89.0f, "deg" );
		addControl( properties, "Outer cone angle", light.outerConeAngle(), GUIControlType::SLIDER, 0.0f, 89.0f, "deg" );
	}
	if( light.type() == DMLight::Dir )
	{
		const DMLight::ShadowSettings& shadows = light.shadowSettings();
		addSettingProperties( properties, DMLight::shadowSchema(), shadows );
	}

	properties.addButton( deleteButton, "Remove the light from the level (Delete in the viewport; Save level deletes its row, Ctrl+Z returns it)",
						  [this, key]() -> PropertyContainer::ButtonAction
	{
		const int index = indexOf( key );
		if( index < 0 )
			return {};
		const std::string name = "Delete " + m_controls[index].properties->name();
		removeLight( key );
		return { [this, key] { restoreLight( key ); }, [this, key] { removeLight( key ); }, name };
	} );
	return controls;
}

DMLight DMLightDriver::newLight( DMLight::LightType type, const XMFLOAT3& position, const XMFLOAT3& direction ) const
{
	DMLight light( type );
	// Имя по типу, с номером, если такое уже есть (и у убранных: их вернёт отмена)
	const std::string base = type == DMLight::Dir ? "Directional light" : type == DMLight::Spot ? "Spot light" : "Point light";
	auto taken = [this]( const std::string& name )
	{
		return std::any_of( m_light_list.begin(), m_light_list.end(), [&name]( const DMLight& other ) { return other.name == name; } ) ||
			   std::any_of( m_removed.begin(), m_removed.end(), [&name]( const RemovedLight& other ) { return other.light.name == name; } );
	};
	light.name = base;
	for( int number = 2; taken( light.name ); ++number )
		light.name = base + " " + std::to_string( number );
	light.setPosition( position );
	light.setDirection( direction );
	// Как новые источники в UE: направленный — 10 лк, без атмосферы и теней (солнце уровня — свой источник); точечный и
	// прожектор — лампа ~100 Вт (100 кд) с радиусом 10 м, конус прожектора 0…44°
	light.setIntensity( type == DMLight::Dir ? 10.0f : 100.0f );
	light.setAttenuationRadius( type == DMLight::Dir ? 0.0f : 10.0f );
	light.setConeAngles( 0.0f, 44.0f );
	light.setAtmosphereSunLight( false );
	return light;
}

uint32_t DMLightDriver::addLight( DMLight light )
{
	light.id = 0;
	const uint32_t key = m_nextKey++;
	LightControls controls = createProperties( light, key, TimeRole::none );
	// Значения по умолчанию — «сохранённые»: Details отмечает правки после них, а сам новый источник — до «Save level»
	controls.properties->markSaved();
	// В конец своего типа: направленные — первыми, солнце остаётся первым
	size_t at = 0;
	while( at < m_light_list.size() && (int)m_light_list[at].type() <= (int)light.type() )
		++at;
	m_light_list.insert( m_light_list.begin() + at, std::move( light ) );
	m_controls.insert( m_controls.begin() + at, std::move( controls ) );
	updateStructure();
	return key;
}

bool DMLightDriver::removeLight( uint32_t key )
{
	const int index = indexOf( key );
	if( index < 0 )
		return false;
	m_removed.push_back( { std::move( m_light_list[index] ), std::move( m_controls[index] ) } );
	m_light_list.erase( m_light_list.begin() + index );
	m_controls.erase( m_controls.begin() + index );
	updateStructure();
	return true;
}

bool DMLightDriver::restoreLight( uint32_t key )
{
	const auto removed = std::find_if( m_removed.begin(), m_removed.end(), [key]( const RemovedLight& light ) { return light.controls.key == key; } );
	if( removed == m_removed.end() )
		return false;
	// На прежнее место: список упорядочен по типу, внутри типа — по ключу (порядок базы, затем добавленные)
	const int type = (int)removed->light.type();
	size_t at = 0;
	while( at < m_light_list.size() && ( (int)m_light_list[at].type() < type ||
										 ( (int)m_light_list[at].type() == type && m_controls[at].key < key ) ) )
		++at;
	m_light_list.insert( m_light_list.begin() + at, std::move( removed->light ) );
	m_controls.insert( m_controls.begin() + at, std::move( removed->controls ) );
	m_removed.erase( removed );
	updateStructure();
	return true;
}

uint32_t DMLightDriver::key( size_t index ) const
{
	return index < m_controls.size() ? m_controls[index].key : 0;
}

int DMLightDriver::indexOf( uint32_t key ) const
{
	for( size_t i = 0; i < m_controls.size(); ++i )
	{
		if( key != 0 && m_controls[i].key == key )
			return static_cast<int>( i );
	}
	return -1;
}

std::vector<uint32_t> DMLightDriver::removedIds() const
{
	std::vector<uint32_t> ids;
	for( const RemovedLight& removed : m_removed )
	{
		if( removed.light.id != 0 )
			ids.push_back( removed.light.id );
	}
	return ids;
}

void DMLightDriver::markSaved( const LightList& saved )
{
	for( size_t i = 0; i < m_light_list.size() && i < saved.size(); ++i )
		m_light_list[i].id = saved[i].id;
	// Строк убранных больше нет: вернёт отмена — вставится заново
	for( RemovedLight& removed : m_removed )
		removed.light.id = 0;
	updateStructure();
}

void DMLightDriver::updateStructure()
{
	m_sunPositionLight = -1;
	m_moonPositionLight = -1;
	m_properties.subContainer().clear();
	if( m_sunPosition )
		m_properties.addSubContainer( m_sunPosition->properties() );
	for( size_t i = 0; i < m_controls.size(); ++i )
	{
		const DMLight& light = m_light_list[i];
		LightControls& controls = m_controls[i];
		if( controls.role == TimeRole::sun )
			m_sunPositionLight = static_cast<int>( i );
		else if( controls.role == TimeRole::moon )
			m_moonPositionLight = static_cast<int>( i );
		// Номер в имени — путь команд set / press различает одноимённые
		const std::string name = light.name.empty() ? DMLight::typeName( light.type() ) : light.name;
		controls.properties->setName( std::to_string( i ) + ": " + name + " (" + DMLight::typeName( light.type() ) + ")" );
		// Нового источника в базе ещё нет
		controls.properties->setStructureModified( light.id == 0 );
		m_properties.addSubContainer( controls.properties.get() );
	}
	// Убранный источник ещё есть в базе — окно изменено до «Save level»
	m_properties.setStructureModified( std::any_of( m_removed.begin(), m_removed.end(), []( const RemovedLight& removed ) { return removed.light.id != 0; } ) );
}

void DMLightDriver::takeClockEdits()
{
	if( m_sunPosition )
		m_sunPosition->takeWindowEdits();
}

void DMLightDriver::update()
{
	if( m_sunPosition )
		m_sunPosition->showClock();

	for( size_t i = 0; i < m_light_list.size(); ++i )
	{
		DMLight& light = m_light_list[i];
		LightControls& controls = m_controls[i];
		PropertyContainer& properties = *controls.properties;

		// Освещённость от луны — по её фазе и расстоянию, как направление; в GUI — расчётная
		if( static_cast<int>( i ) == m_moonPositionLight )
			properties["Intensity"].setData( m_sunPosition->moon().illuminance );

		light.setEnabled( properties["Enabled"].data<bool>() );
		light.setColor( properties["Color"].data<XMFLOAT3>() );
		light.setIntensity( properties["Intensity"].data<float>() );
		if( static_cast<int>( i ) == m_sunPositionLight )
		{
			const XMFLOAT3 toSun = m_sunPosition->toSun();
			light.setDirection( XMFLOAT3( -toSun.x, -toSun.y, -toSun.z ) );
		}
		else if( static_cast<int>( i ) == m_moonPositionLight )
		{
			const XMFLOAT3 toMoon = m_sunPosition->toMoon();
			light.setDirection( XMFLOAT3( -toMoon.x, -toMoon.y, -toMoon.z ) );
		}
		else if( light.type() != DMLight::Point )
		{
			// Направление пересчитывается из углов, только когда их сдвинули: иначе оно осталось бы как в базе
			const XMFLOAT2 rotation( properties["Pitch"].data<float>(), properties["Yaw"].data<float>() );
			if( rotation.x != controls.rotation.x || rotation.y != controls.rotation.y )
			{
				light.setDirection( directionFromRotation( rotation ) );
				controls.rotation = rotation;
			}
		}
		if( light.type() != DMLight::Dir )
		{
			light.setPosition( properties["Position"].data<XMFLOAT3>() );
			light.setAttenuationRadius( properties["Attenuation radius"].data<float>() );
		}
		if( light.type() == DMLight::Spot )
			light.setConeAngles( properties["Inner cone angle"].data<float>(), properties["Outer cone angle"].data<float>() );
		if( light.type() == DMLight::Dir )
		{
			light.setAtmosphereSunLight( properties["Atmosphere sun light"].data<bool>() );
			DMLight::ShadowSettings shadows;
			readSettingProperties( properties, DMLight::shadowSchema(), shadows );
			light.setShadowSettings( shadows );
		}
	}
}

void DMLightDriver::setAtmosphereTransmittance( int index, const XMFLOAT3& transmittance )
{
	m_atmosphereTransmittance[index == 1 ? 1 : 0] = transmittance;
}

XMFLOAT3 DMLightDriver::sunRadiance( const DMLight& light ) const
{
	XMFLOAT3 radiance = light.radiance();
	if( light.atmosphereSunLight() )
	{
		const XMFLOAT3& transmittance = m_atmosphereTransmittance[light.atmosphereSunLightIndex()];
		radiance.x *= transmittance.x;
		radiance.y *= transmittance.y;
		radiance.z *= transmittance.z;
	}
	return radiance;
}

std::vector<const DMLight*> DMLightDriver::bufferLights() const
{
	const DMLight* sunLight = sun();
	const DMLight* moonLight = moon();
	auto groundLuminance = [this]( const DMLight* light )
	{
		const XMFLOAT3 color = sunRadiance( *light );
		return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
	};
	float brightest = 0.0f;
	for( const DMLight* light : { sunLight, moonLight } )
		if( light )
			brightest = std::max( brightest, groundLuminance( light ) );

	std::vector<const DMLight*> lights;
	for( const DMLight& light : m_light_list )
	{
		if( !light.enabled() || lights.size() == maxLights )
			continue;
		if( ( &light == sunLight || &light == moonLight ) && groundLuminance( &light ) < brightest * negligibleFraction )
			continue;
		lights.push_back( &light );
	}
	return lights;
}

uint32_t DMLightDriver::setBuffer( int8_t slot )
{
	const DMLight* sunLight = sun();
	std::vector<LightBuffer> buffer;
	buffer.reserve( maxLights );
	for( const DMLight* lightPointer : bufferLights() )
	{
		const DMLight& light = *lightPointer;

		LightBuffer lightBuffer = {};
		lightBuffer.position = light.position();
		lightBuffer.type = (int)light.type();
		lightBuffer.direction = light.direction();
		lightBuffer.attenuationRadius = light.attenuationRadius();
		// Светила атмосферы (солнце и луна) у земли — с пропусканием атмосферы к ним
		lightBuffer.color = &light == sunLight || &light == moon() ? sunRadiance( light ) : light.radiance();
		lightBuffer.cosOuterCone = cosf( XMConvertToRadians( light.outerConeAngle() ) );
		lightBuffer.cosInnerCone = cosf( XMConvertToRadians( light.innerConeAngle() ) );
		buffer.push_back( lightBuffer );
	}

	if( buffer.empty() )
	{
		LightBuffer lightBuffer = {};
		lightBuffer.type = DMLight::Dir;
		lightBuffer.direction = fallbackDirection();
		lightBuffer.color = XMFLOAT3( fallbackIlluminance, fallbackIlluminance, fallbackIlluminance );
		buffer.push_back( lightBuffer );
	}

	// Участок кольца живёт один кадр: буфер пишется каждый кадр (десятки источников — сотни байт)
	m_lightParamBuffer = std::move( buffer );
	m_structBuffer.updateData( m_lightParamBuffer.data(), m_lightParamBuffer.size() * sizeof( LightBuffer ) );

	m_structBuffer.setToSlot( slot );

	return static_cast<uint32_t>( m_lightParamBuffer.size() );
}

const DMLight* DMLightDriver::sun() const
{
	for( const auto& light : m_light_list )
	{
		if( light.enabled() )
			return light.type() == DMLight::Dir ? &light : nullptr;
	}
	return nullptr;
}

void DMLightDriver::directionalLight( XMFLOAT3& direction, XMFLOAT3& color ) const
{
	XMFLOAT3 lightDirection = fallbackDirection();
	color = XMFLOAT3( fallbackIlluminance, fallbackIlluminance, fallbackIlluminance );
	if( const DMLight* light = sun() )
	{
		lightDirection = light->direction();
		color = light->radiance();
	}
	else if( std::any_of( m_light_list.begin(), m_light_list.end(), []( const DMLight& light ) { return light.enabled(); } ) )
	{
		// Источники есть, а солнца нет — ночь: запасное солнце только у уровня совсем без света, как в setBuffer
		color = XMFLOAT3( 0.0f, 0.0f, 0.0f );
	}
	direction = XMFLOAT3( -lightDirection.x, -lightDirection.y, -lightDirection.z );
}

const DMLight* DMLightDriver::moon() const
{
	const DMLight* sunLight = sun();
	for( const auto& light : m_light_list )
	{
		if( light.enabled() && light.type() == DMLight::Dir && light.atmosphereSunLightIndex() == 1 && &light != sunLight )
			return &light;
	}
	return nullptr;
}

bool DMLightDriver::moonLight( XMFLOAT3& direction, XMFLOAT3& color ) const
{
	const DMLight* light = moon();
	if( !light )
		return false;
	const XMFLOAT3 lightDirection = light->direction();
	direction = XMFLOAT3( -lightDirection.x, -lightDirection.y, -lightDirection.z );
	color = light->radiance();
	return true;
}

int DMLightDriver::bufferIndex( const DMLight* light ) const
{
	const std::vector<const DMLight*> lights = bufferLights();
	const auto it = std::find( lights.begin(), lights.end(), light );
	return it == lights.end() ? -1 : static_cast<int>( it - lights.begin() );
}

int DMLightDriver::shadowLight( XMFLOAT3& direction, DMLight::ShadowSettings& settings ) const
{
	// Тени — от того светила над горизонтом, чей свет у земли ярче: днём и в сумерках — солнца, когда оно почти
	// село, — луны (если она отбрасывает тени)
	const DMLight* best = nullptr;
	float bestLuminance = 0.0f;
	for( const DMLight* light : { sun(), moon() } )
	{
		if( !light || light->direction().y >= 0.0f || !light->shadowSettings().castShadows || bufferIndex( light ) < 0 )
			continue;
		const XMFLOAT3 color = sunRadiance( *light );
		const float groundLuminance = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
		if( !best || groundLuminance > bestLuminance )
		{
			best = light;
			bestLuminance = groundLuminance;
		}
	}
	for( const DMLight* light : { best } )
	{
		if( !light )
			continue;
		const int index = bufferIndex( light );
		const XMFLOAT3 lightDirection = light->direction();
		direction = XMFLOAT3( -lightDirection.x, -lightDirection.y, -lightDirection.z );
		settings = light->shadowSettings();
		return index;
	}
	direction = XMFLOAT3( 0.0f, -1.0f, 0.0f );
	settings = DMLight::ShadowSettings();
	settings.castShadows = false;
	return -1;
}

float DMLightDriver::sunIlluminance() const
{
	XMFLOAT3 direction;
	XMFLOAT3 color;
	directionalLight( direction, color );
	return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

float DMLightDriver::sunGroundIlluminance() const
{
	const DMLight* light = sun();
	if( !light )
		return sunIlluminance();
	const XMFLOAT3 color = sunRadiance( *light );
	return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

const SunPosition* DMLightDriver::sunPosition() const
{
	return m_sunPosition.get();
}

SunPosition* DMLightDriver::sunPosition()
{
	return m_sunPosition.get();
}

const DMLightDriver::LightList& DMLightDriver::lights() const
{
	return m_light_list;
}

PropertyContainer* DMLightDriver::properties()
{
	return &m_properties;
}

XMFLOAT3 DMLightDriver::fallbackDirection()
{
	const float component = 1.0f / sqrtf( 3.0f );
	return XMFLOAT3( -component, -component, component );
}
