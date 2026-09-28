#pragma once

#include "DirectX.h"
#include "Properties\PropertyContainer.h"

// Положение солнца по месту и времени, как Sun Position в UE: широта, долгота, часовой пояс, дата и время по местным
// часам дают высоту солнца над горизонтом и азимут (формулы NOAA Solar Calculator по J. Meeus, «Astronomical
// Algorithms»; точность — сотые доли градуса, рефракция у горизонта не учитывается). Строка SunPosition уровня
// (Levels.sun_position) задаёт направление солнца — первого направленного источника — вместо его Pitch / Yaw.
// Время идёт само со скоростью Time scale (во сколько раз быстрее реального, как timescale в играх Bethesda; 0 — стоит)
// и переходит через полночь в следующий день. В GUI — подокно «Sun position» в окне «Lights», там же пауза
class SunPosition
{
public:
	// Строка SunPosition
	struct Settings
	{
		float latitude = 0.0f;		// градусы, север > 0
		float longitude = 0.0f;		// градусы, восток > 0
		float timeZone = 0.0f;		// часы от UTC; летнее время — ещё час
		float northOffset = 0.0f;	// поворот севера от +Z вокруг вертикали (к +X), градусы
		int32_t year = 2026;
		int32_t month = 6;
		int32_t day = 21;
		float timeOfDay = 12.0f;	// часы по местным часам, 0…24 (Solar Time в UE)
		float timeScale = 0.0f;		// во сколько раз игровое время быстрее реального; 0 — время стоит
	};

	// Высота над горизонтом и азимут — от севера по часовой стрелке, через восток; градусы
	struct Angles
	{
		float elevation = 0.0f;
		float azimuth = 0.0f;
	};

	explicit SunPosition( const Settings& settings );

	// Текущие значения из GUI — для сохранения уровня
	Settings settings() const;
	// Время идёт: seconds реального времени кадра × Time scale; через полночь — следующий день. Пауза («Time paused»)
	// и нулевая скорость время не двигают
	void advance( float seconds );
	Angles angles() const;
	// Направление на солнце в мире: север — +Z, повёрнутый на North offset, восток — +X (как Yaw у источника), вверх — +Y
	XMFLOAT3 toSun() const;
	PropertyContainer* properties();

	static Angles compute( const Settings& settings );

private:
	PropertyContainer m_properties;
	int32_t m_year;		// год в GUI не правится: положение солнца в один день разных лет почти одно
	// Время суток точнее, чем во float свойства: при реальной скорости приращение кадра (~10⁻⁶ ч) — порядка шага float
	// у 12 ч. Свойство — его округление; правка свойства в GUI (значение не совпало с записанным) берётся заново
	double m_hours;
	float m_hoursShown;
};
