// Ходьба по рельефу (WalkMode): падение на землю, шаг по ровному, стена круче предела, сход с уступа и падение с обрыва,
// соскальзывание с крутого склона, прыжок. Рельеф — подделка TerrainHeightSource, клавиши — Input::setKeyDown
#include <doctest/doctest.h>
#include <cmath>
#include "Camera\DMCamera.h"
#include "Camera\WalkMode.h"
#include "Engine\Input\Input.h"
#include "fake_terrain.h"

using namespace GS;

namespace
{

constexpr float frame = 1.0f / 60.0f;
constexpr float eyeHeight = 1.7f;	// «Eye height» окна «Walk» по умолчанию

void press( uint8_t key, bool down )
{
	Input::instance().setKeyDown( key, down );
	Input::instance().Frame();
}

void walk( WalkMode& mode, DMCamera& camera, const TerrainHeightSource& terrain, float seconds )
{
	for( float t = 0.0f; t < seconds; t += frame )
		mode.update( frame, camera, terrain );
}

// Камера смотрит на +Z (рыскание 0) с высоты y над точкой (x, z)
void start( WalkMode& mode, DMCamera& camera, const TerrainHeightSource& terrain, float x, float y, float z )
{
	camera.SetPosition( x, y, z );
	camera.SetRotation( 0.0f, 0.0f, 0.0f );
	mode.setEnabled( true, camera, &terrain );
}

}

TEST_CASE( "WalkMode: falls from the air and stands at eye height on the ground" )
{
	const FakeTerrain flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat, 128.0f, 30.0f, 128.0f );
	walk( mode, camera, flat, 3.0f );
	CHECK( camera.position().y == doctest::Approx( 10.0f + eyeHeight ).epsilon( 0.001 ) );
	CHECK( mode.eyeAboveFeet() == doctest::Approx( eyeHeight ).epsilon( 0.01 ) );
}

TEST_CASE( "WalkMode: W walks forward on flat ground at walking speed" )
{
	const FakeTerrain flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat, 128.0f, 11.7f, 128.0f );
	walk( mode, camera, flat, 0.5f );
	const float before = camera.position().z;
	press( DIK_W, true );
	walk( mode, camera, flat, 2.0f );
	press( DIK_W, false );
	const float travelled = camera.position().z - before;
	CHECK( travelled > 2.0f );		// 1,4 м/с, разгон — доли секунды
	CHECK( travelled < 3.0f );
	CHECK( camera.position().x == doctest::Approx( 128.0f ) );
}

TEST_CASE( "WalkMode: a wall steeper than the limit stops the walk" )
{
	// Ступень 0,5 м на z = 140: вверх по ней не шагнуть (уклон — вертикаль)
	const FakeTerrain wall( []( float, float z ) { return z < 140.0f ? 10.0f : 10.5f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, wall, 128.0f, 11.7f, 135.0f );
	press( DIK_W, true );
	walk( mode, camera, wall, 8.0f );
	press( DIK_W, false );
	CHECK( camera.position().z < 140.0f );
	CHECK( camera.position().y == doctest::Approx( 10.0f + eyeHeight ).epsilon( 0.001 ) );
}

TEST_CASE( "WalkMode: walks down a small ledge, falls from a cliff" )
{
	// Уступ 0,3 м (меньше «Step height» 0,4) на z = 140, обрыв 3 м на z = 150
	const FakeTerrain ledges( []( float, float z ) { return z < 140.0f ? 10.0f : z < 150.0f ? 9.7f : 6.7f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, ledges, 128.0f, 11.7f, 138.0f );
	press( DIK_W, true );
	walk( mode, camera, ledges, 3.0f );
	CHECK( camera.position().z > 140.0f );
	CHECK( camera.position().z < 150.0f );
	CHECK( camera.position().y == doctest::Approx( 9.7f + eyeHeight ).epsilon( 0.01 ) );
	walk( mode, camera, ledges, 6.0f );
	press( DIK_W, false );
	walk( mode, camera, ledges, 2.0f );
	CHECK( camera.position().z > 150.0f );
	CHECK( camera.position().y == doctest::Approx( 6.7f + eyeHeight ).epsilon( 0.01 ) );
}

TEST_CASE( "WalkMode: slides down a slope steeper than the limit" )
{
	// Склон 60° вверх по x (предел — 40°)
	const float slope = std::tan( 60.0f * 3.14159265f / 180.0f );
	const FakeTerrain steep( [slope]( float x, float ) { return 10.0f + slope * ( x - 128.0f ); } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, steep, 130.0f, 10.0f + slope * 2.0f + eyeHeight, 128.0f );
	walk( mode, camera, steep, 2.0f );
	CHECK( camera.position().x < 129.5f );
}

TEST_CASE( "WalkMode: jumps up and lands back" )
{
	const FakeTerrain flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat, 128.0f, 11.7f, 128.0f );
	walk( mode, camera, flat, 0.2f );
	press( DIK_SPACE, true );
	walk( mode, camera, flat, 0.3f );
	press( DIK_SPACE, false );
	CHECK( camera.position().y > 10.0f + eyeHeight + 0.5f );		// 4,5 м/с вверх — около метра
	walk( mode, camera, flat, 2.0f );
	CHECK( camera.position().y == doctest::Approx( 10.0f + eyeHeight ).epsilon( 0.001 ) );
}
