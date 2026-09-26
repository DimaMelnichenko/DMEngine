#pragma once

#include <string>
#include "DirectX.h"

// Источник света. Параметры названы как в UE (Attenuation Radius, Inner / Outer Cone Angle) и KHR_lights_punctual
class DMLight
{
public:
	enum LightType { Dir, Point, Spot };

public:
	DMLight( LightType );

	bool enabled() const;
	LightType type( ) const;

	void setColor( const XMFLOAT3& );
	XMFLOAT3 color() const;

	// Положение точечного и прожектора, м
	void setPosition( const XMFLOAT3& );
	const XMFLOAT3& position() const;

	// Направление, в котором идёт свет (направленный и прожектор); хранится нормированным
	void setDirection( const XMFLOAT3& );
	XMFLOAT3 direction() const;

	// Радиус, на котором свет точечного и прожектора плавно спадает до нуля, м; 0 — без обрезания
	void setAttenuationRadius( float );
	float attenuationRadius() const;

	// Конус прожектора: углы от оси до края, градусы. Внутри внутреннего — полная яркость, за внешним — нет света
	void setConeAngles( float inner, float outer );
	float innerConeAngle() const;
	float outerConeAngle() const;

	static LightType strToType( const std::string& );

private:
	bool m_enabled = true;
	XMFLOAT3 m_color = { 1.0f, 1.0f, 1.0f };
	LightType m_type;
	XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 m_direction = { 0.0f, -1.0f, 0.0f };
	float m_attenuationRadius = 0.0f;
	float m_innerConeAngle = 0.0f;	// как в KHR_lights_punctual
	float m_outerConeAngle = 45.0f;
};
