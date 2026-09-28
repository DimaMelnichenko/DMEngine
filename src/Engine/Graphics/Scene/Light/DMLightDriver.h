#pragma once

#include "DirectX.h"
#include <memory>
#include <optional>
#include <vector>
#include "DMLight.h"
#include "SunPosition.h"
#include "D3D\DMStructuredBuffer.h"
#include "Properties\PropertyContainer.h"

// Источники света уровня (таблица LevelLights): буфер для шейдеров (g_lights в Shaders/lighting.sh) и окно GUI
// «Lights» — по подокну на источник. Направленные стоят первыми, солнце — первый включённый из них: по нему считаются
// небо и каскадные тени. Направление первого направленного может задавать время суток (SunPosition), а свет солнца
// у земли — атмосфера (setSunTransmittance). Буфер загружается на GPU, только когда источники изменились
class DMLightDriver
{
public:
	using LightList = std::vector<DMLight>;
	// Размер буфера источников: лишние включённые источники не освещают
	static constexpr uint32_t maxLights = 32;
public:
	DMLightDriver( );
	~DMLightDriver();
	bool Initialize();
	// Источники уровня; порядок направленных между собой — как в базе. sunPosition — место и время уровня (строка
	// SunPosition): тогда направление первого направленного источника задаёт оно, а Pitch / Yaw у него нет
	void load( LightList lights, const std::optional<SunPosition::Settings>& sunPosition = std::nullopt );
	// Раз за кадр до отрисовки: значения из GUI — в источники, время суток — в направление солнца
	// seconds — реальное время кадра: с ним идёт время суток (SunPosition::advance)
	void update( float seconds = 0.0f );
	// Пропускание атмосферы от земли к солнцу (SkyAtmosphere::sunTransmittance) — после update(): на него умножается
	// свет солнца с atmosphereSunLight
	void setSunTransmittance( const XMFLOAT3& transmittance );
	// Упаковка буфера источников, на GPU — если он изменился, и в слот; возвращает число источников в буфере
	uint32_t setBuffer( int8_t slot, SRVType type );
	// Солнце — первый включённый направленный источник: направление на свет (нормированное) и яркость над
	// атмосферой. Включённых источников нет — запасной свет, как в setBuffer; есть, но не направленные — яркость 0 (ночь)
	void directionalLight( XMFLOAT3& direction, XMFLOAT3& color ) const;
	// Индекс солнца в буфере источников (g_lights в шейдерах) или −1, если направленного нет. Направленные
	// в буфере первыми, поэтому солнце — 0; оно же и в directionalLight()
	int sunLightIndex() const;
	// Настройки теней солнца; castShadows = false, если солнца нет
	DMLight::ShadowSettings sunShadows() const;
	// Освещённость от солнца над атмосферой, лк (яркость его цвета × интенсивность): на неё умножаются запечённые
	// для солнца 1 лк небо и освещение окружением (cb_skyLightScale)
	float sunIlluminance() const;
	// Освещённость от солнца у земли, лк: после атмосферы, если у солнца atmosphereSunLight
	float sunGroundIlluminance() const;
	// Место и время уровня; nullptr — солнце по своим Pitch / Yaw
	const SunPosition* sunPosition() const;
	// Запасной свет, когда включённых источников нет: белое солнце, лк
	static constexpr float fallbackIlluminance = 100000.0f;

	const LightList& lights() const;
	PropertyContainer* properties();

private:
	// Направление запасного света, когда источников нет
	static XMFLOAT3 fallbackDirection();
	const DMLight* sun() const;
	// Цвет солнца в буфере: яркость, у земли — с пропусканием атмосферы
	XMFLOAT3 sunRadiance( const DMLight& light ) const;
	void createProperties( const DMLight& light, uint32_t index );

	LightList m_light_list;
	// Свойства источника в GUI; адрес не меняется: его хранит GUI
	struct LightControls
	{
		std::unique_ptr<PropertyContainer> properties;
		XMFLOAT2 rotation;	// Pitch / Yaw, по которым последний раз задано направление
	};
	std::vector<LightControls> m_controls;
	PropertyContainer m_properties;
	std::unique_ptr<SunPosition> m_sunPosition;
	int m_sunPositionLight = -1;	// источник, направление которого задаёт m_sunPosition: первый направленный
	XMFLOAT3 m_sunTransmittance = { 1.0f, 1.0f, 1.0f };

	// Раскладка — struct Light в Shaders/lighting.sh
	struct alignas( 16 ) LightBuffer
	{
		XMFLOAT3 position;
		int type;
		XMFLOAT3 direction;		// куда идёт свет, нормированное
		float attenuationRadius;
		XMFLOAT3 color;
		float cosOuterCone;
		float cosInnerCone;
		XMFLOAT3 padding;
	};
	static_assert( sizeof( LightBuffer ) == 64, "LightBuffer must match struct Light in Shaders/lighting.sh" );
	DMStructuredBuffer m_structBuffer;
	std::vector<LightBuffer> m_lightParamBuffer;
	bool m_bufferChanged = true;
};

