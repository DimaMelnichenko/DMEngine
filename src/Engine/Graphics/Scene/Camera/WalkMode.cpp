#include "WalkMode.h"
#include <algorithm>
#include <cmath>
#include "DMCamera.h"
#include "Engine\Input\Input.h"
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

}

WalkMode::WalkMode()
{
	m_properties.setName( "Walk" );
	addValue( m_properties, "Walk speed", 1.4f, 0.5f, 5.0f, "m/s", "Walking, W A S D" );
	addValue( m_properties, "Run speed", 5.0f, 1.0f, 15.0f, "m/s", "With Shift" );
	addValue( m_properties, "Eye height", 1.7f, 0.3f, 3.0f, "m", "Eyes above the feet" );
	addValue( m_properties, "Jump speed", 4.5f, 0.0f, 15.0f, "m/s", "Space; 4.5 m/s - about 1 m up" );
	addValue( m_properties, "Gravity", 9.81f, 1.0f, 30.0f, "m/s2", "Fall when walking starts in the air and after a jump" );
	addValue( m_properties, "Max slope", 40.0f, 10.0f, 80.0f, "deg", "Steeper slopes can't be climbed and slide down" );
	addValue( m_properties, "Step height", 0.4f, 0.0f, 1.5f, "m", "A drop deeper than this in one step is a fall, a smaller one is walked down" );
}

void WalkMode::setEnabled( bool enabled, DMCamera& camera, const TerrainHeightSource* terrain )
{
	if( enabled == m_enabled )
		return;
	m_enabled = enabled && terrain;
	camera.setKeyboardMovement( !m_enabled );
	if( !m_enabled )
		return;

	// С места камеры — падение до земли (под землёй — сразу на поверхность)
	const XMFLOAT3& eye = camera.position();
	const float eyeHeight = m_properties["Eye height"].data<float>();
	m_feet = XMFLOAT3( eye.x, eye.y - eyeHeight, eye.z );
	float ground = 0.0f;
	if( terrain->surfaceHeight( m_feet.x, m_feet.z, ground ) && m_feet.y < ground )
		m_feet.y = ground;
	m_velocity = XMFLOAT2( 0.0f, 0.0f );
	m_verticalSpeed = 0.0f;
	m_onGround = false;
	m_eyeLift = 0.0f;
}

XMFLOAT3 WalkMode::surfaceNormal( float x, float z, const TerrainHeightSource& terrain ) const
{
	const TerrainHeight height = terrain.terrainHeight();
	const float step = height.mapSize ? height.worldSize / height.mapSize : 1.0f;
	float left = 0.0f, right = 0.0f, back = 0.0f, front = 0.0f;
	terrain.surfaceHeight( x - step, z, left );
	terrain.surfaceHeight( x + step, z, right );
	terrain.surfaceHeight( x, z - step, back );
	terrain.surfaceHeight( x, z + step, front );
	XMFLOAT3 normal;
	XMStoreFloat3( &normal, XMVector3Normalize( XMVectorSet( left - right, 2.0f * step, back - front, 0.0f ) ) );
	return normal;
}

bool WalkMode::canStep( float fromHeight, const XMFLOAT2& to, float distance, const TerrainHeightSource& terrain, float& toHeight ) const
{
	if( !terrain.surfaceHeight( to.x, to.y, toHeight ) )
		return false;
	// Вниз — всегда; вверх — не круче предела. Без допусков на подъём: при сотнях кадров в секунду шаг кадра — миллиметры,
	// и любой допуск в сантиметр пустил бы на стену
	const float rise = toHeight - fromHeight;
	if( rise <= 0.0f )
		return true;
	const float maxSlope = std::tan( XMConvertToRadians( m_properties["Max slope"].data<float>() ) );
	return rise <= distance * maxSlope;
}

void WalkMode::update( float seconds, DMCamera& camera, const TerrainHeightSource& terrain )
{
	if( !m_enabled )
		return;
	seconds = std::min( seconds, 0.1f );	// после долгого кадра (загрузка, отладчик) — не проваливаться
	const PropertyContainer& p = m_properties;

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

	float ground = m_feet.y;
	terrain.surfaceHeight( m_feet.x, m_feet.z, ground );

	// Крутой склон под ногами — съезжаешь вниз по нему
	const float maxSlopeCos = std::cos( XMConvertToRadians( p["Max slope"].data<float>() ) );
	if( m_onGround )
	{
		const XMFLOAT3 normal = surfaceNormal( m_feet.x, m_feet.z, terrain );
		if( normal.y < maxSlopeCos )
		{
			const float slide = p["Gravity"].data<float>() * ( 1.0f - normal.y ) * seconds;
			m_velocity.x += normal.x * slide;
			m_velocity.y += normal.z * slide;
		}
	}

	// Шаг по горизонтали: целиком, иначе вдоль одной из осей (скольжение вдоль крутого склона), иначе стоим
	const XMFLOAT2 move( m_velocity.x * seconds, m_velocity.y * seconds );
	const float distance = std::sqrt( move.x * move.x + move.y * move.y );
	if( distance > 0.0f )
	{
		const float from = m_onGround ? ground : m_feet.y;
		float toHeight = 0.0f;
		const XMFLOAT2 candidates[] = { XMFLOAT2( m_feet.x + move.x, m_feet.z + move.y ), XMFLOAT2( m_feet.x + move.x, m_feet.z ),
										XMFLOAT2( m_feet.x, m_feet.z + move.y ) };
		bool moved = false;
		for( const XMFLOAT2& to : candidates )
		{
			const float dx = to.x - m_feet.x;
			const float dz = to.y - m_feet.z;
			const float step = std::sqrt( dx * dx + dz * dz );
			if( step > 0.0f && canStep( from, to, step, terrain, toHeight ) )
			{
				m_feet.x = to.x;
				m_feet.z = to.y;
				moved = true;
				break;
			}
		}
		if( !moved )
			m_velocity = XMFLOAT2( 0.0f, 0.0f );
		terrain.surfaceHeight( m_feet.x, m_feet.z, ground );
	}

	// По вертикали: прыжок с земли, гравитация в воздухе, приземление
	const bool jump = input.IsUpMove();
	if( m_onGround && jump && !m_jumpHeld )
	{
		m_verticalSpeed = p["Jump speed"].data<float>();
		m_onGround = false;
	}
	m_jumpHeld = jump;
	if( m_onGround )
	{
		// Вниз по склону — прилипаем к земле; вверх — взгляд догоняет плавно
		if( ground > m_feet.y )
			m_eyeLift -= ground - m_feet.y;
		if( m_feet.y - ground > p["Step height"].data<float>() )
			m_onGround = false;	// обрыв — падаем
		else
			m_feet.y = ground;
	}
	if( !m_onGround )
	{
		m_verticalSpeed -= p["Gravity"].data<float>() * seconds;
		m_feet.y += m_verticalSpeed * seconds;
		if( m_feet.y <= ground )
		{
			m_feet.y = ground;
			m_verticalSpeed = 0.0f;
			m_onGround = true;
		}
	}
	m_eyeLift = approach( m_eyeLift, 0.0f, 15.0f, seconds );

	const float eyeHeight = p["Eye height"].data<float>();
	camera.SetPosition( m_feet.x, m_feet.y + eyeHeight + m_eyeLift, m_feet.z );
}

}
