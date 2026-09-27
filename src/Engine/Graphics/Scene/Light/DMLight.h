#pragma once

#include <string>
#include "DirectX.h"

// Источник света уровня (строка LevelLights). Параметры названы как в UE (Attenuation Radius, Inner / Outer Cone
// Angle, настройки Cascaded Shadow Maps у Directional Light) и KHR_lights_punctual (типы, цвет и интенсивность
// раздельно)
class DMLight
{
public:
	enum LightType { Dir, Point, Spot };

	// Тени направленного источника — каскадные карты (ShadowCascades), настройки как у Directional Light в UE
	struct ShadowSettings
	{
		bool castShadows = false;
		float dynamicShadowDistance = 200.0f;		// до скольких метров от камеры есть тени
		float cascadeDistributionExponent = 3.0f;	// во сколько раз каждый каскад длиннее предыдущего
		float cascadeTransitionFraction = 0.1f;		// доля каскада, где он смешивается со следующим
		float shadowDistanceFadeoutFraction = 0.1f;	// доля дистанции, на которой тень плавно уходит
		float shadowBias = 1.0f;					// сдвиг точки к солнцу, текселей каскада
		float normalBias = 1.0f;					// сдвиг точки по нормали, текселей каскада
		float shadowSlopeBias = 2.0f;				// наклонное смещение глубины в растеризаторе теней
	};

public:
	DMLight( LightType );

	uint32_t id = 0;	// строка LevelLights; 0 — источника нет в базе
	std::string name;

	bool enabled() const;
	void setEnabled( bool );
	LightType type( ) const;

	// Цвет (линейный) и интенсивность раздельно, как в glTF и UE; в шейдер идёт их произведение — radiance()
	void setColor( const XMFLOAT3& );
	XMFLOAT3 color() const;
	void setIntensity( float );
	float intensity() const;
	XMFLOAT3 radiance() const;

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

	const ShadowSettings& shadowSettings() const;
	void setShadowSettings( const ShadowSettings& );

	// Тип по имени из базы — как type в KHR_lights_punctual: directional, point, spot
	static LightType strToType( const std::string& );
	static const char* typeName( LightType );

private:
	bool m_enabled = true;
	XMFLOAT3 m_color = { 1.0f, 1.0f, 1.0f };
	float m_intensity = 1.0f;
	LightType m_type;
	XMFLOAT3 m_position = { 0.0f, 0.0f, 0.0f };
	XMFLOAT3 m_direction = { 0.0f, -1.0f, 0.0f };
	float m_attenuationRadius = 0.0f;
	float m_innerConeAngle = 0.0f;	// как в KHR_lights_punctual
	float m_outerConeAngle = 45.0f;
	ShadowSettings m_shadowSettings;
};
