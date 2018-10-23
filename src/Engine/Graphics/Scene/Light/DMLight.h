#pragma once

#include "DirectX.h"
#include "DMTransformBuffer.h"

class DMLight
{
public:
	enum LightType { Dir, Point, Spot };

public:
	DMLight();
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
	void setType( LightType type );

	void setColor( float r, float g, float b );
	void setColor( XMFLOAT3& );
	XMFLOAT3 color() const;

	void setDirection( XMFLOAT3& );
	void setDirection( float, float, float );
	XMFLOAT3 direction() const;
	
	float spotAngle() const;

	DMTransformBuffer m_transformBuffer;

	static LightType strToType( const std::string& );

private:
	bool m_enabled;
	XMFLOAT3 m_color;
	LightType m_type;
	XMFLOAT3 m_direction;
	float m_spot_angle;	
};

