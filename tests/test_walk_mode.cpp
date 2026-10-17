// Ходьба по рельефу (WalkMode) на персонаже мира столкновений: падение на землю, шаг по ровному, стена выше ступеньки,
// сход с уступа и падение с обрыва, соскальзывание с крутого склона, прыжок, упор в коробку. Рельеф — подделка с шагом
// 0,25 м (стена в полметра — стена, а не пологий скат), клавиши — Input::setKeyDown
#include <doctest/doctest.h>
#include <cmath>
#include <memory>
#include "Camera\DMCamera.h"
#include "Camera\WalkMode.h"
#include "Engine\Input\Input.h"
#include "Engine\Simulation\World.h"
#include "fake_terrain.h"

using namespace GS;
using namespace DirectX;

namespace
{

constexpr float frame = 1.0f / 60.0f;
constexpr float eyeHeight = 1.7f;	// «Eye height» окна «Walk» по умолчанию
// Персонаж держит зазор до земли (CharacterVirtual: padding 2 см) — высота глаз с этим допуском
constexpr float heightTolerance = 0.03f;

// Мир с землёй столкновений по подделке рельефа
struct WalkWorld
{
	explicit WalkWorld( std::function<float( float x, float z )> height ) : terrain( std::move( height ), 1024 )
	{
		world.setTerrain( &terrain );
		REQUIRE( world.physics().buildTerrain( terrain ) );
	}

	FakeTerrain terrain;
	World world;
};

void press( uint8_t key, bool down )
{
	Input::instance().setKeyDown( key, down );
	Input::instance().Frame();
}

void walk( WalkMode& mode, DMCamera& camera, World& world, float seconds )
{
	for( float t = 0.0f; t < seconds; t += frame )
		mode.update( frame, camera, world );
}

// Камера смотрит на +Z (рыскание 0) с высоты y над точкой (x, z)
void start( WalkMode& mode, DMCamera& camera, World& world, float x, float y, float z )
{
	camera.SetPosition( x, y, z );
	camera.SetRotation( 0.0f, 0.0f, 0.0f );
	mode.setEnabled( true, camera, &world );
}

}

TEST_CASE( "WalkMode: falls from the air and stands at eye height on the ground" )
{
	WalkWorld flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat.world, 128.0f, 30.0f, 128.0f );
	walk( mode, camera, flat.world, 3.0f );
	CHECK( std::abs( camera.position().y - ( 10.0f + eyeHeight ) ) < heightTolerance );
	CHECK( mode.eyeAboveFeet() == doctest::Approx( eyeHeight ).epsilon( 0.01 ) );
}

TEST_CASE( "WalkMode: W walks forward on flat ground at walking speed" )
{
	WalkWorld flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat.world, 128.0f, 11.7f, 128.0f );
	walk( mode, camera, flat.world, 0.5f );
	const float before = camera.position().z;
	press( DIK_W, true );
	walk( mode, camera, flat.world, 2.0f );
	press( DIK_W, false );
	const float travelled = camera.position().z - before;
	CHECK( travelled > 2.0f );		// 1,4 м/с, разгон — доли секунды
	CHECK( travelled < 3.0f );
	CHECK( camera.position().x == doctest::Approx( 128.0f ).epsilon( 1e-4 ) );
}

TEST_CASE( "WalkMode: a wall higher than the step stops the walk" )
{
	// Ступень 0,5 м на z = 140 («Step height» 0,4): вверх по ней не шагнуть
	WalkWorld wall( []( float, float z ) { return z < 140.0f ? 10.0f : 10.5f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, wall.world, 128.0f, 11.7f, 135.0f );
	press( DIK_W, true );
	walk( mode, camera, wall.world, 8.0f );
	press( DIK_W, false );
	CHECK( camera.position().z < 140.0f );
	CHECK( std::abs( camera.position().y - ( 10.0f + eyeHeight ) ) < heightTolerance );
}

TEST_CASE( "WalkMode: walks down a small ledge, falls from a cliff" )
{
	// Уступ 0,3 м (меньше «Step height» 0,4) на z = 140, обрыв 3 м на z = 150
	WalkWorld ledges( []( float, float z ) { return z < 140.0f ? 10.0f : z < 150.0f ? 9.7f : 6.7f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, ledges.world, 128.0f, 11.7f, 138.0f );
	press( DIK_W, true );
	walk( mode, camera, ledges.world, 3.0f );
	CHECK( camera.position().z > 140.0f );
	CHECK( camera.position().z < 150.0f );
	CHECK( std::abs( camera.position().y - ( 9.7f + eyeHeight ) ) < heightTolerance );
	walk( mode, camera, ledges.world, 6.0f );
	press( DIK_W, false );
	walk( mode, camera, ledges.world, 2.0f );
	CHECK( camera.position().z > 150.0f );
	CHECK( std::abs( camera.position().y - ( 6.7f + eyeHeight ) ) < heightTolerance );
}

TEST_CASE( "WalkMode: slides down a slope steeper than the limit" )
{
	// Склон 60° вверх по x (предел — 40°)
	const float slope = std::tan( 60.0f * 3.14159265f / 180.0f );
	WalkWorld steep( [slope]( float x, float ) { return 10.0f + slope * ( x - 128.0f ); } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, steep.world, 130.0f, 10.0f + slope * 2.0f + eyeHeight, 128.0f );
	walk( mode, camera, steep.world, 2.0f );
	CHECK( camera.position().x < 129.5f );
}

TEST_CASE( "WalkMode: jumps up and lands back" )
{
	WalkWorld flat( []( float, float ) { return 10.0f; } );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat.world, 128.0f, 11.7f, 128.0f );
	walk( mode, camera, flat.world, 0.5f );
	press( DIK_SPACE, true );
	walk( mode, camera, flat.world, 0.3f );
	press( DIK_SPACE, false );
	CHECK( camera.position().y > 10.0f + eyeHeight + 0.5f );		// 4,5 м/с вверх — около метра
	walk( mode, camera, flat.world, 2.0f );
	CHECK( std::abs( camera.position().y - ( 10.0f + eyeHeight ) ) < heightTolerance );
}

TEST_CASE( "WalkMode: a box stops the walk" )
{
	WalkWorld flat( []( float, float ) { return 10.0f; } );
	// Коробка 4 × 2 × 1 м поперёк пути, передняя грань — на z = 134,5
	flat.world.physics().addStaticBox( XMFLOAT3( 0.0f, 0.0f, 0.0f ), XMFLOAT3( 2.0f, 1.0f, 0.5f ), XMMatrixTranslation( 128.0f, 11.0f, 135.0f ) );
	DMCamera camera;
	WalkMode mode;
	start( mode, camera, flat.world, 128.0f, 11.7f, 128.0f );
	press( DIK_W, true );
	walk( mode, camera, flat.world, 8.0f );
	press( DIK_W, false );
	CHECK( camera.position().z < 134.5f - 0.3f + 0.05f );	// у грани — на радиус капсулы
	CHECK( camera.position().z > 133.5f );					// дошёл до неё
	mode.setEnabled( false, camera, &flat.world );
	CHECK( flat.world.physics().characterCount() == 0 );
}
