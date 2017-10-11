#include "DMLight.h"


DMLight::DMLight( LightType _type )
{
	m_type = _type;
	m_color = D3DXVECTOR3( 1.0, 1.0, 1.0 );
	m_enabled = true;
	m_cast_shadow = false;
}

DMLight::~DMLight(void)
{

}

DMLight& DMLight::operator=( const DMLight& other ) 
{
	m_enabled = other.m_enabled;
	m_cast_shadow = other.m_cast_shadow;
	m_color = other.m_color;
	m_type = other.m_type;
	m_direction = other.m_direction;
	m_spot_angle = other.m_spot_angle;
	m_camera_shadow = other.m_camera_shadow;
	m_mask_srv = other.m_mask_srv;
	m_texel_size = other.m_texel_size;
	m_shadow_rt = other.m_shadow_rt;

	return *this;
}

DMLight::DMLight( const DMLight& other ) : DMSceneObject( other )
{
	m_enabled = other.m_enabled;
	m_cast_shadow =	 other.m_cast_shadow;
	m_color = other.m_color;
	m_type = other.m_type;
	m_direction = other.m_direction;
	m_spot_angle = other.m_spot_angle;
	m_camera_shadow = other.m_camera_shadow;
	m_mask_srv = other.m_mask_srv;
	m_texel_size = other.m_texel_size;
	m_shadow_rt = other.m_shadow_rt;
}

DMLight::DMLight( DMLight&& other ) : DMSceneObject( other )
{
	std::swap( m_enabled, other.m_enabled );
	std::swap( m_cast_shadow, other.m_cast_shadow );
	std::swap( m_color, other.m_color );
	std::swap( m_type, other.m_type );
	std::swap( m_direction, other.m_direction );
	std::swap( m_spot_angle, other.m_spot_angle );
	std::swap( m_camera_shadow, other.m_camera_shadow );
	std::swap( m_mask_srv, other.m_mask_srv );
	std::swap( m_texel_size, other.m_texel_size );
	std::swap( m_shadow_rt, other.m_shadow_rt );
}

void DMLight::setEnabled( bool )
{

}

bool DMLight::enabled() const
{
	return m_enabled;
}

void DMLight::update( float time )
{
	DMSceneObject::update( time );
}

void DMLight::setColor( float r, float g, float b )
{
	m_color.x = r;
	m_color.y = g;
	m_color.z = b;
}

void DMLight::setColor( D3DXVECTOR3& _color )
{
	memcpy( &m_color, &_color, sizeof( D3DXVECTOR3 ) );
}

D3DXVECTOR3 DMLight::color( ) const
{
	return m_color;
}

void DMLight::setDirection( D3DXVECTOR3& _direction )
{
	memcpy( &m_direction, &_direction, sizeof( D3DXVECTOR3 ) );
	updateCamera();
}

void DMLight::setDirection( float x, float y, float z )
{
	m_direction.x = x;
	m_direction.y = y;
	m_direction.z = z;
	updateCamera();
}

D3DXVECTOR3 DMLight::direction() const
{
	return m_direction;
}

DMLight::LightType DMLight::type() const
{
	return m_type;
}

void DMLight::setCastShadow( bool cast_shadow )
{
	m_cast_shadow = cast_shadow;
}

float DMLight::texelSize() const
{
	return m_texel_size;
}

float DMLight::spotAngle() const
{
	return m_spot_angle;
}

void DMLight::setMask( ID3D11ShaderResourceView* mask )
{
	m_mask_srv = mask;
}

const DMRenderTexture& DMLight::shadow_rt() const
{
	return m_shadow_rt;
}

ID3D11ShaderResourceView* DMLight::mask_srv() const
{
	return m_mask_srv;
}

void DMLight::setShadowParam( float fov_spot_angle, unsigned int texture_size )
{
	//if( fov_spot_angle > D3DX_PI || fov_spot_angle <= 0.0 || texture_size == 0 )
	//{	
	//	return;
	//}
	
	m_spot_angle = fov_spot_angle;
	
	m_camera_shadow.Initialize( DMCamera::CT_PERSPECTIVE, texture_size, texture_size, 0.1, attenuation, fov_spot_angle );

	m_shadow_rt.Initialize( DXGI_FORMAT_R32_FLOAT, texture_size, texture_size );

	m_texel_size = 1.0 / texture_size;
}

bool DMLight::castShadow() const
{
	return m_cast_shadow;
}

void DMLight::updateCamera()
{
	m_camera_shadow.SetPosition( position() );
	m_camera_shadow.SetDirection( m_direction );
	m_camera_shadow.Render();
}

const DMCamera& DMLight::shadowCamera() const
{	
	return m_camera_shadow;
}