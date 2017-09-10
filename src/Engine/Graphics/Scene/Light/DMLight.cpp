#include "DMLight.h"


DMLight::DMLight( DMD3D* _parent, LightType _type ) : DMSceneObject( _parent )
{
	m_type = _type;
	m_color = D3DXVECTOR3( 1.0, 1.0, 1.0 );
	m_enabled = true;
	m_cast_shadow = false;
	m_camera_shadow = nullptr;
}

DMLight::~DMLight(void)
{

}

void DMLight::setEnabled( bool )
{

}

bool DMLight::enabled()
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

D3DXVECTOR3 DMLight::color( )
{
	return m_color;
}

void DMLight::setDirection( D3DXVECTOR3& _direction )
{
	memcpy( &m_direction, &_direction, sizeof( D3DXVECTOR3 ) );
}

void DMLight::setDirection( float x, float y, float z )
{
	m_direction.x = x;
	m_direction.y = y;
	m_direction.z = z;
}

D3DXVECTOR3 DMLight::direction()
{
	return m_direction;
}

DMLight::LightType DMLight::type()
{
	return m_type;
}

void DMLight::setCastShadow( bool cast_shadow )
{
	m_cast_shadow = cast_shadow;
}

float DMLight::texelSize()
{
	return m_texel_size;
}

float DMLight::spotAngle()
{
	return m_spot_angle;
}

void DMLight::setMask( ID3D11ShaderResourceView* mask )
{
	m_mask_srv = mask;
}

DMRenderTexture* DMLight::shadow_rt()
{
	return m_shadow_rt.get();
}

ID3D11ShaderResourceView* DMLight::mask_srv()
{
	return m_mask_srv;
}

void DMLight::setShadowParam( float fov_spot_angle, unsigned int texture_size )
{
	//if( fov_spot_angle > D3DX_PI || fov_spot_angle <= 0.0 || texture_size == 0 )
	//{	
	//	return;
	//}

	if( m_camera_shadow )
	{
		m_camera_shadow.release();
	}

	m_spot_angle = fov_spot_angle;

	m_camera_shadow = std::unique_ptr<DMCamera>( new DMCamera() );
	m_camera_shadow->Initialize( DMCamera::CT_PERSPECTIVE, texture_size, texture_size, 0.1, attenuation, fov_spot_angle );

	if( m_shadow_rt )
	{
		m_shadow_rt.release();
	}

	DMRenderTexture* rt = new DMRenderTexture( m_dmd3d );

	rt->Initialize( DXGI_FORMAT_R32_FLOAT, texture_size, texture_size );

	m_shadow_rt = std::unique_ptr<DMRenderTexture>( rt );

	m_texel_size = 1.0 / texture_size;
}

bool DMLight::castShadow()
{
	return m_cast_shadow;
}

DMCamera* DMLight::shadowCamera()
{
	m_camera_shadow->SetPosition( position() );
	m_camera_shadow->SetDirection( m_direction );
	m_camera_shadow->Render();
	return m_camera_shadow.get();
}