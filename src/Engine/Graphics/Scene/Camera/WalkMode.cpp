#include "WalkMode.h"
#include <algorithm>
#include <cmath>
#include "DMCamera.h"
#include "Engine\Input\Input.h"
#include "Engine\Simulation\World.h"
#include "Terrain\TerrainHeightSource.h"

using namespace DirectX;

namespace GS
{

namespace
{

Property* addValue( PropertyContainer& properties, const char* name, float value, float low, float high, const char* unit,
					const char* tooltip )
{
	Property* property = properties.insert( name, value );
	property->setLow( low );
	property->setHigh( high );
	property->setUnit( unit )->setTooltip( tooltip );
	return property;
}

// Скорость к цели не быстрее rate · dt (экспоненциальное приближение): разгон и торможение без рывка
float approach( float current, float target, float rate, float seconds )
{
	return target + ( current - target ) * std::exp( -rate * seconds );
}

bool sameSettings( const PhysicsWorld::CharacterSettings& a, const PhysicsWorld::CharacterSettings& b )
{
	return a.radius == b.radius && a.height == b.height && a.maxSlope == b.maxSlope && a.stepHeight == b.stepHeight &&
		   a.maxWadeDepth == b.maxWadeDepth;
}

}

WalkMode::WalkMode()
{
	m_properties.setName( "Walk" );
	addValue( m_properties, "Walk speed", 1.4f, 0.5f, 5.0f, "m/s", "Walking, W A S D" );
	addValue( m_properties, "Run speed", 5.0f, 1.0f, 15.0f, "m/s", "With Shift" );
	addValue( m_properties, "Eye height", 1.7f, 0.3f, 3.0f, "m", "Eyes above the feet; the capsule is 0.1 m taller" );
	addValue( m_properties, "Radius", 0.3f, 0.1f, 1.0f, "m", "Capsule of the walker: models and creatures stop it at this distance" );
	addValue( m_properties, "Jump speed", 4.5f, 0.0f, 15.0f, "m/s", "Space; 4.5 m/s - about 1 m up" );
	addValue( m_properties, "Gravity", 9.81f, 1.0f, 30.0f, "m/s2", "Fall when walking starts in the air and after a jump" );
	addValue( m_properties, "Max slope", 40.0f, 10.0f, 80.0f, "deg", "Steeper slopes can't be climbed and slide down" );
	addValue( m_properties, "Step height", 0.4f, 0.0f, 1.5f, "m",
			  "Higher steps are walls, lower ones are stepped up; a drop deeper than this is a fall, a smaller one is walked down" );
	addValue( m_properties, "Max wade depth", 1.0f, 0.0f, 3.0f, "m", "Deeper water stops the walk (along the shore you go on); 0 - water is no obstacle" );
}

PhysicsWorld::CharacterSettings WalkMode::characterSettings() const
{
	PhysicsWorld::CharacterSettings settings;
	settings.radius = m_properties["Radius"].data<float>();
	settings.height = std::max( m_properties["Eye height"].data<float>() + 0.1f, 2.0f * settings.radius + 0.05f );
	settings.maxSlope = m_properties["Max slope"].data<float>();
	settings.stepHeight = m_properties["Step height"].data<float>();
	settings.maxWadeDepth = m_properties["Max wade depth"].data<float>();
	return settings;
}

void WalkMode::setEnabled( bool enabled, DMCamera& camera, World* world )
{
	if( enabled == m_enabled )
		return;
	if( m_enabled && m_world )
		m_world->physics().removeCharacter( m_character );
	m_character = 0;
	m_enabled = enabled && world && world->terrain();
	m_world = m_enabled ? world : nullptr;
	camera.setKeyboardMovement( !m_enabled );
	if( !m_enabled )
		return;

	// С места камеры — падение до земли (под землёй — сразу на поверхность)
	const XMFLOAT3& eye = camera.position();
	const float eyeHeight = m_properties["Eye height"].data<float>();
	m_feet = XMFLOAT3( eye.x, eye.y - eyeHeight, eye.z );
	float ground = 0.0f;
	if( world->terrain()->surfaceHeight( m_feet.x, m_feet.z, ground ) && m_feet.y < ground )
		m_feet.y = ground;
	m_applied = characterSettings();
	m_character = world->physics().createCharacter( m_applied, m_feet );
	m_velocity = XMFLOAT2( 0.0f, 0.0f );
	m_onGround = false;
	m_eyeLift = 0.0f;
}

float WalkMode::eyeAboveFeet() const
{
	return m_enabled ? m_properties["Eye height"].data<float>() + m_eyeLift : 0.0f;
}

void WalkMode::update( float seconds, DMCamera& camera, World& world )
{
	if( !m_enabled || &world != m_world )
		return;
	seconds = std::min( seconds, 0.1f );	// после долгого кадра (загрузка, отладчик) — не проваливаться
	const PropertyContainer& p = m_properties;
	PhysicsWorld& physics = world.physics();
	// Правка окна (рост, радиус, уклон, ступенька, брод) — капсула заново
	const PhysicsWorld::CharacterSettings settings = characterSettings();
	if( !sameSettings( settings, m_applied ) )
	{
		physics.setCharacterSettings( m_character, settings );
		m_applied = settings;
	}

	// Куда хочет идти: W A S D по направлению взгляда по горизонтали
	Input& input = Input::instance();
	XMFLOAT2 wish( 0.0f, 0.0f );
	if( input.IsForwarPressed() )
		wish.y += 1.0f;
	if( input.IsBackwardPressed() )
		wish.y -= 1.0f;
	if( input.IsRightStride() )
		wish.x += 1.0f;
	if( input.IsLeftStride() )
		wish.x -= 1.0f;
	const float yaw = XMConvertToRadians( camera.rotation().y );
	const float sinYaw = std::sin( yaw );
	const float cosYaw = std::cos( yaw );
	XMFLOAT2 direction( wish.x * cosYaw + wish.y * sinYaw, -wish.x * sinYaw + wish.y * cosYaw );
	const float length = std::sqrt( direction.x * direction.x + direction.y * direction.y );
	const bool run = input.isKeyDown( DIK_LSHIFT ) || input.isKeyDown( DIK_RSHIFT );
	const float speed = p[run ? "Run speed" : "Walk speed"].data<float>();
	XMFLOAT2 target( 0.0f, 0.0f );
	if( length > 0.0f )
		target = XMFLOAT2( direction.x / length * speed, direction.y / length * speed );

	// Разгон и торможение: на земле быстро, в воздухе почти без управления
	const float control = m_onGround ? 12.0f : 1.0f;
	m_velocity.x = approach( m_velocity.x, target.x, control, seconds );
	m_velocity.y = approach( m_velocity.y, target.y, control, seconds );

	// Прыжок — по нажатию
	const bool jump = input.IsUpMove();
	PhysicsWorld::CharacterMove move;
	move.velocity = m_velocity;
	move.jumpSpeed = jump && !m_jumpHeld ? p["Jump speed"].data<float>() : 0.0f;
	move.gravity = p["Gravity"].data<float>();
	m_jumpHeld = jump;

	const float before = m_feet.y;
	const PhysicsWorld::CharacterState state = physics.moveCharacter( m_character, move, seconds );
	m_feet = state.feet;
	// Шаг вверх на земле — взгляд догоняет плавно
	if( state.onGround && m_onGround && m_feet.y > before )
		m_eyeLift -= m_feet.y - before;
	m_onGround = state.onGround;
	m_eyeLift = approach( m_eyeLift, 0.0f, 15.0f, seconds );

	const float eyeHeight = p["Eye height"].data<float>();
	camera.SetPosition( m_feet.x, m_feet.y + eyeHeight + m_eyeLift, m_feet.z );
}

}
