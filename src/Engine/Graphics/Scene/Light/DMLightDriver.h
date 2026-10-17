#pragma once

#include "DirectX.h"
#include <memory>
#include <optional>
#include <vector>
#include "DMLight.h"
#include "SunPosition.h"
#include "D3D\DMStructuredBuffer.h"
#include "Properties\PropertyContainer.h"

// Источники света уровня (таблица LevelLights): буфер для шейдеров (g_lights в Shaders/lights.sh) и окно GUI
// «Lights» — по подокну на источник. Направленные стоят первыми, солнце — первый включённый из них: по нему считаются
// небо и каскадные тени. Луна — направленный с atmosphereSunLightIndex 1 (второе светило атмосферы, как в UE). Время
// суток (SunPosition) задаёт направление солнца и направление и освещённость луны, свет светил у земли — атмосфера
// (setAtmosphereTransmittance). Тени — от солнца, а когда оно под горизонтом — от луны (shadowLight). Буфер
// загружается на GPU, только когда источники изменились
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
	// SunPosition): тогда направление первого направленного источника задаёт оно, а Pitch / Yaw у него нет; время —
	// у мировых часов clock (их органы — подокно «Sun position»: SunPosition ставит часы на время строки)
	void load( LightList lights, GS::WorldClock& clock, const std::optional<SunPosition::Settings>& sunPosition = std::nullopt );
	// Правки времени в окне «Sun position» — в мировые часы; раз за кадр до их шага
	void takeClockEdits();
	// Раз за кадр до отрисовки, после шага часов: время часов — в окно, значения из GUI — в источники, время суток — в
	// направление солнца и луны
	void update();
	// Пропускание атмосферы от земли к светилу index (0 — солнце, 1 — луна; SkyAtmosphere::sunTransmittance) — после
	// update(): на него умножается свет светила с atmosphereSunLight
	void setAtmosphereTransmittance( int index, const DirectX::XMFLOAT3& transmittance );
	// Упаковка буфера источников, на GPU — если он изменился, и в слот; возвращает число источников в буфере
	uint32_t setBuffer( int8_t slot );
	// Солнце — первый включённый направленный источник: направление на свет (нормированное) и яркость над
	// атмосферой. Включённых источников нет — запасной свет, как в setBuffer; есть, но не направленные — яркость 0 (ночь)
	void directionalLight( DirectX::XMFLOAT3& direction, DirectX::XMFLOAT3& color ) const;
	// Луна — включённый направленный с atmosphereSunLightIndex 1: направление на неё и яркость над атмосферой;
	// false — луны у уровня нет
	bool moonLight( DirectX::XMFLOAT3& direction, DirectX::XMFLOAT3& color ) const;
	// Источник каскадных теней: солнце, пока оно над горизонтом, иначе луна над горизонтом; у него — тени. Индекс в
	// буфере источников (−1 — теней нет), направление на него и настройки каскадов
	int shadowLight( DirectX::XMFLOAT3& direction, DMLight::ShadowSettings& settings ) const;
	// Освещённость от солнца над атмосферой, лк (яркость его цвета × интенсивность): на неё умножаются запечённые
	// для солнца 1 лк небо и освещение окружением (cb_skyLightScale)
	float sunIlluminance() const;
	// Освещённость от солнца у земли, лк: после атмосферы, если у солнца atmosphereSunLight
	float sunGroundIlluminance() const;
	// Место и время уровня; nullptr — солнце по своим Pitch / Yaw
	const SunPosition* sunPosition() const;
	SunPosition* sunPosition();
	// Запасной свет, когда включённых источников нет: белое солнце, лк
	static constexpr float fallbackIlluminance = 100000.0f;

	const LightList& lights() const;
	// Солнце — первый включённый направленный источник, луна — направленный с atmosphere_sun_light_index 1; nullptr — нет
	const DMLight* sun() const;
	const DMLight* moon() const;
	PropertyContainer* properties();

	// Вьюпорт редактора (выбор и гизмо источников, Viewport): подокно свойств источника index в окне «Lights»; солнце и
	// луну при SunPosition ведёт время суток (directionFromTime, луна — moonFromTime): их тянут временем — свойство
	// «Time of day» подокна «Sun position» (sunPositionProperties; nullptr — места и времени у уровня нет)
	PropertyContainer* lightProperties( size_t index );
	bool directionFromTime( size_t index ) const;
	bool moonFromTime( size_t index ) const;
	PropertyContainer* sunPositionProperties();
	// Направление по углам, как Rotation у источника в UE: Pitch < 0 — свет идёт вниз, Yaw — поворот вокруг вертикали
	// от +Z к +X, градусы
	static DirectX::XMFLOAT3 directionFromRotation( const DirectX::XMFLOAT2& rotation );
	static DirectX::XMFLOAT2 rotationFromDirection( const DirectX::XMFLOAT3& direction );

	// Добавление и удаление в редакторе (кнопки окна «Lights», Delete во вьюпорте). У источника — ключ, постоянный в
	// сеансе (по нему его находят отмена и вьюпорт: номер в списке после добавления и удаления сдвигается). Новый встаёт
	// в конец своего типа; убранный хранится до выхода вместе с подокном — restoreLight возвращает его на прежнее место,
	// и история правок его свойств остаётся верной. Солнце и луна времени суток остаются ими и после возврата; убрали
	// солнце — время суток никого не ведёт (солнцем неба становится первый включённый направленный, как при выключении).
	// Строки базы — «Save level»: новые (id 0) вставляются, убранные удаляются (removedIds, markSaved)
	uint32_t addLight( DMLight light );
	bool removeLight( uint32_t key );
	bool restoreLight( uint32_t key );
	// Новый источник с настройками по умолчанию, как у новых источников в UE, и незанятым именем («Point light 2»)
	DMLight newLight( DMLight::LightType type, const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& direction ) const;
	// Ключ источника index (0 — нет такого) и номер по ключу (−1 — убран или нет такого)
	uint32_t key( size_t index ) const;
	int indexOf( uint32_t key ) const;
	// Строки LevelLights убранных источников, которые ещё есть в базе
	std::vector<uint32_t> removedIds() const;
	// Уровень сохранён: saved — копия lights() с id вставленных строк; убранных в базе больше нет
	void markSaved( const LightList& saved );
	// Кнопка подокна источника, которая его убирает (с отменой); её же нажимает Delete во вьюпорте
	static constexpr const char* deleteButton = "Delete";

private:
	// Направление запасного света, когда источников нет
	static DirectX::XMFLOAT3 fallbackDirection();
	// Источники буфера по порядку: включённые, кроме светила атмосферы, свет которого у земли меньше
	// negligibleFraction от самого яркого из них (днём — луна, ночью — солнце под горизонтом): каждый источник в буфере
	// шейдеры освещения считают в каждом пикселе
	std::vector<const DMLight*> bufferLights() const;
	// Номер источника в буфере; −1 — не попал в буфер
	int bufferIndex( const DMLight* light ) const;
	static constexpr float negligibleFraction = 1e-5f;
	// Цвет светила атмосферы в буфере: яркость, у земли — с пропусканием атмосферы к нему
	DirectX::XMFLOAT3 sunRadiance( const DMLight& light ) const;

	// Какое светило времени суток (SunPosition) ведёт направление источника
	enum class TimeRole { none, sun, moon };
	// Свойства источника в GUI; адрес не меняется: его хранит GUI
	struct LightControls
	{
		std::unique_ptr<PropertyContainer> properties;
		DirectX::XMFLOAT2 rotation;	// Pitch / Yaw, по которым последний раз задано направление
		uint32_t key = 0;
		TimeRole role = TimeRole::none;
	};
	LightControls createProperties( const DMLight& light, uint32_t key, TimeRole role );
	// После добавления и удаления: номера светил времени суток, подокна по порядку с номерами в именах, отметка
	// несохранённого (новый источник, убранный из базы)
	void updateStructure();

	LightList m_light_list;
	std::vector<LightControls> m_controls;
	// Убранные источники — до выхода (отмена удаления)
	struct RemovedLight
	{
		DMLight light;
		LightControls controls;
	};
	std::vector<RemovedLight> m_removed;
	uint32_t m_nextKey = 1;
	PropertyContainer m_properties;
	std::unique_ptr<SunPosition> m_sunPosition;
	int m_sunPositionLight = -1;	// источник, направление которого задаёт m_sunPosition: первый направленный уровня
	int m_moonPositionLight = -1;	// луна, которую ведёт m_sunPosition: направленный с atmosphereSunLightIndex 1
	DirectX::XMFLOAT3 m_atmosphereTransmittance[2] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };

	// Раскладка — struct Light в Shaders/lights.sh
	struct alignas( 16 ) LightBuffer
	{
		DirectX::XMFLOAT3 position;
		int type;
		DirectX::XMFLOAT3 direction;		// куда идёт свет, нормированное
		float attenuationRadius;
		DirectX::XMFLOAT3 color;
		float cosOuterCone;
		float cosInnerCone;
		DirectX::XMFLOAT3 padding;
	};
	static_assert( sizeof( LightBuffer ) == 64, "LightBuffer must match struct Light in Shaders/lights.sh" );
	DMStructuredBuffer m_structBuffer;
	std::vector<LightBuffer> m_lightParamBuffer;
};

