#pragma once

#include <string>
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
	~DMLight(void);
	void update( float );
	
	float m_attenuation;
	bool enabled() const;

	LightType type( ) const;
	void setColor( XMFLOAT3& );
	XMFLOAT3 color() const;
	XMFLOAT3 direction() const;

	DMTransformBuffer m_transformBuffer;

	static LightType strToType( const std::string& );

private:
	bool m_enabled;
	XMFLOAT3 m_color;
	LightType m_type;
	XMFLOAT3 m_direction;
	float m_spot_angle;	
};

