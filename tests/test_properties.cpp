// Свойства окон (Property, PropertyContainer) и описание полей настроек (SettingsSchema): номер правки, сохранённое
// значение, окно ↔ структура
#include <doctest/doctest.h>
#include <string>
#include "Properties\PropertyContainer.h"
#include "Properties\SettingsProperties.h"

using namespace DirectX;

namespace
{

struct TestSettings
{
	float speed = 2.0f;
	int32_t count = 3;
	bool enabled = true;
	XMFLOAT3 color = XMFLOAT3( 0.1f, 0.2f, 0.3f );
	SettingRange band{ 1.0f, 2.0f };
	std::string name = "row";	// только в базе — окна у строк нет
};

const SettingsSchema<TestSettings>& testSchema()
{
	using Settings = TestSettings;
	static const SettingsSchema<Settings> schema = {
		{ "speed", "Speed", DM_SETTING( speed ), 0.0f, 10.0f, "m/s" },
		{ "count", "Count", DM_SETTING( count ), 0.0f, 10.0f },
		{ "enabled", "Enabled", DM_SETTING( enabled ) },
		{ "color", "Color", DM_SETTING( color ), 0.0f, 0.0f, "", "", settingColor },
		{ "band_from,band_to", "Band", DM_SETTING( band ), 0.0f, 5.0f, "", "", settingRangeControl },
		{ "name", nullptr, DM_SETTING( name ) },
	};
	return schema;
}

}

TEST_CASE( "Property: revision grows only when the value changes" )
{
	PropertyContainer container( "Test" );
	Property* speed = container.insert( "Speed", 1.0f );
	const uint64_t first = speed->revision();
	speed->setData( 1.0f );
	CHECK( speed->revision() == first );
	speed->setData( 2.0f );
	CHECK( speed->revision() > first );
	CHECK( container.revision() >= speed->revision() );
}

TEST_CASE( "PropertyContainer: revision includes sub-containers" )
{
	PropertyContainer container( "Test" );
	PropertyContainer sub( "Sub" );
	container.insert( "Speed", 1.0f );
	Property* height = sub.insert( "Height", 5.0f );
	container.addSubContainer( &sub );
	const uint64_t before = container.revision();
	height->setData( 6.0f );
	CHECK( container.revision() > before );
}

TEST_CASE( "Property: saved value, modified and reset" )
{
	PropertyContainer container( "Test" );
	Property* speed = container.insert( "Speed", 2.0f );
	speed->markSaved();
	CHECK_FALSE( speed->modified() );
	speed->setData( 3.0f );
	CHECK( speed->modified() );
	CHECK( container.modified() );
	speed->resetToSaved();
	CHECK( speed->data<float>() == doctest::Approx( 2.0f ) );
	CHECK_FALSE( speed->modified() );
}

TEST_CASE( "SettingsSchema: window from settings and back" )
{
	TestSettings initial;
	PropertyContainer window( "Settings" );
	addSettingProperties( window, testSchema(), initial );
	CHECK( window.exists( "Speed" ) );
	CHECK( window.exists( "Band" ) );
	CHECK_FALSE( window.exists( "name" ) );		// поле только в базе — свойства нет

	window["Speed"].setData( 5.0f );
	window["Count"].setData( int32_t( 7 ) );
	window["Enabled"].setData( false );
	window["Color"].setData( XMFLOAT3( 1.0f, 0.5f, 0.25f ) );
	window["Band"].setData( XMFLOAT2( 2.0f, 4.0f ) );

	TestSettings result;
	readSettingProperties( window, testSchema(), result );
	CHECK( result.speed == doctest::Approx( 5.0f ) );
	CHECK( result.count == 7 );
	CHECK_FALSE( result.enabled );
	CHECK( result.color.y == doctest::Approx( 0.5f ) );
	CHECK( result.band.from == doctest::Approx( 2.0f ) );
	CHECK( result.band.to == doctest::Approx( 4.0f ) );
	CHECK( result.name == "row" );		// поля без окна не трогаются
}
