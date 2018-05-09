#pragma once

#include "DirectX.h"
#include "DMTransformBuffer.h"

class DMLight
{
public:
	enum LightType { Dir, Point, Spot };

public:
	DMLight( LightType );
	DMLight( const DMLight& );
	DMLight& operator=( const DMLight& );
	//DMLight( DMLight&& ) = delete;
	//DMLight& operator=( DMLight&& ) = delete;
	~DMLight(void);
	void update( float );
	
	float m_attenuation;
	
	void setEnabled( bool );
	bool enabled() const;

	LightType type( ) const;

	void setColor( float r, float g, float b );
	void setColor( D3DXVECTOR3& );
	D3DXVECTOR3 color() const;

	void setDirection( D3DXVECTOR3& );
	void setDirection( float, float, float );
	D3DXVECTOR3 direction() const;
	
	float spotAngle() const;

	DMTransformBuffer m_transformBuffer;

	static LightType strToType( const std::string& );

private:
	bool m_enabled;
	D3DXVECTOR3 m_color;
	LightType m_type;
	D3DXVECTOR3 m_direction;
	float m_spot_angle;	
};

