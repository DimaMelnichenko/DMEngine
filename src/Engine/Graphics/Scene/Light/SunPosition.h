#pragma once

#include "DirectX.h"
#include "Properties\PropertyContainer.h"
#include "Level\LevelSettings.h"
#include "Engine\Simulation\WorldClock.h"

// Положение солнца по месту и времени, как Sun Position в UE: широта, долгота, часовой пояс, дата и время по местным
// часам дают высоту солнца над горизонтом и азимут (формулы NOAA Solar Calculator по J. Meeus, «Astronomical
// Algorithms»; точность — сотые доли градуса, рефракция у горизонта не учитывается). Строка SunPosition уровня
// (Levels.sun_position) задаёт направление солнца — первого направленного источника — вместо его Pitch / Yaw.
// Время — у мировых часов (GS::WorldClock: идут со скоростью Time scale, через полночь — следующий день); подокно
// «Sun position» в окне «Lights» — и место, и органы часов: Time of day, Month, Day, Time scale, Time paused.
// По тем же месту и времени — луна (moon(): положение по Meeus, гл. 47, фаза и освещённость — её свет задаёт второй
// источник атмосферы, Atmosphere Sun Light Index 1) и поворот звёздного неба (equatorialFrame)
class SunPosition
{
public:
	// Строка SunPosition (Level/LevelSettings.h)
	using Settings = GS::SunPositionSettings;

	// Высота над горизонтом и азимут — от севера по часовой стрелке, через восток; градусы
	struct Angles
	{
		float elevation = 0.0f;
		float azimuth = 0.0f;
	};

	// Луна для наблюдателя: положение с параллаксом (у горизонта луна ниже почти на градус), фаза и свет
	struct Moon
	{
		Angles angles;
		float distance = 384400.0f;			// от центра Земли, км
		float phaseAngle = 0.0f;			// угол фазы (солнце — луна — Земля), градусы: 0 — полнолуние, 180 — новолуние
		float illuminatedFraction = 1.0f;	// освещённая доля диска
		float illuminance = 0.0f;			// освещённость от луны над атмосферой, лк, с фазой (по звёздной величине)
		float fullIlluminance = 0.0f;		// освещённость от полной луны на этом расстоянии, лк — яркость диска
		float eclipticLongitude = 0.0f;		// геоцентрические эклиптические долгота и широта, градусы — для проверки
		float eclipticLatitude = 0.0f;
		// Оптическая либрация (Meeus, гл. 53) — селенографические долгота и широта центра видимого диска, градусы: долгота > 0 —
		// виден край у Моря Кризисов, широта > 0 — северный полюс луны повёрнут к Земле. Позиционный угол оси — от севера
		// неба к востоку, градусы. Геоцентрические, без физической (~0,03°) и суточной (до ~1°) либрации
		float librationLongitude = 0.0f;
		float librationLatitude = 0.0f;
		float positionAngle = 0.0f;
	};

	// Окно — по строке уровня, часы — на её дату, время и скорость
	SunPosition( const Settings& settings, GS::WorldClock& clock );

	// Текущие значения из GUI — для сохранения уровня и расчёта светил: место и время, которое показывает окно (после
	// showClock — время часов), год — у часов
	Settings settings() const;
	// Настройки пресета окружения (на лету): в окно и в часы
	void apply( const Settings& settings );
	// Правки окна — в часы (время суток, дата, скорость, пауза); до шага часов, иначе шаг их затрёт
	void takeWindowEdits();
	// Часы — в окно (время суток и дата); после шага часов
	void showClock();
	Angles angles() const;
	// Направление на солнце в мире: север — +Z, повёрнутый на North offset, восток — +X (как Yaw у источника), вверх — +Y
	DirectX::XMFLOAT3 toSun() const;
	Moon moon() const;
	DirectX::XMFLOAT3 toMoon() const;
	// Экваториальный базис в мире (оси — к точке весеннего равноденствия, к прямому восхождению 90° и к северному
	// полюсу мира): небо поворачивается за сутки со звёздным временем. Звёзды фона задаются в нём
	void equatorialFrame( DirectX::XMFLOAT3& x, DirectX::XMFLOAT3& y, DirectX::XMFLOAT3& z ) const;
	PropertyContainer* properties();

	static Angles compute( const Settings& settings );
	static Moon computeMoon( const Settings& settings );
	// Направление в мире по высоте и азимуту: север — +Z, повёрнутый на northOffset, восток — +X, вверх — +Y
	static DirectX::XMFLOAT3 direction( const Angles& angles, float northOffset );

private:
	void setClock( const Settings& settings );

	PropertyContainer m_properties;
	// Время суток точнее, чем во float свойства, — у часов; свойство — его округление: правка в GUI (значение не совпало с
	// часами) берётся заново. Год в GUI не правится (положение солнца в один день разных лет почти одно) — тоже у часов
	GS::WorldClock& m_clock;
};
