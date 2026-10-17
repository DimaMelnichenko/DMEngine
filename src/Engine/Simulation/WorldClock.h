#pragma once

#include <cstdint>

namespace GS
{

// Мировые часы — одно время для неба, звуков и распорядка существ (как часы мира и время суток в играх с циклом дня и
// ночи). Календарь — дата и час по местным часам — идёт со скоростью Time scale (во сколько раз быстрее реального, как
// timescale в играх Bethesda; 0 — стоит) и стоит на паузе; через полночь — следующий день, месяц, год. Время игры —
// секунды с запуска без скорости: шаги симуляции (World), cb_gameTime шейдеров (рябь воды, звёзды); для сравнения кадров
// его останавливают (-freezetime). Органы часов в GUI — подокно «Sun position» (SunPosition), начальное время — строка
// SunPosition уровня; у уровня без неё — 12:00, время стоит
class WorldClock
{
public:
	struct Date
	{
		int32_t year = 2026;
		int32_t month = 6;
		int32_t day = 21;
	};

	// Реальные секунды кадра: календарь — × Time scale (кроме паузы), время игры — как есть (если идёт)
	void advance( double seconds );

	// Время суток, часы 0…24
	double hours() const { return m_hours; }
	void setHours( double hours );
	Date date() const { return m_date; }
	// Месяц — 1…12, день — в пределах месяца
	void setDate( const Date& date );
	float timeScale() const { return m_timeScale; }
	void setTimeScale( float scale ) { m_timeScale = scale; }
	bool paused() const { return m_paused; }
	void setPaused( bool paused ) { m_paused = paused; }

	// Время игры, с: сумма реальных секунд кадров с запуска
	double gameSeconds() const { return m_gameSeconds; }
	bool gameTimeRunning() const { return m_gameTimeRunning; }
	void setGameTimeRunning( bool running ) { m_gameTimeRunning = running; }

	static int32_t daysInMonth( int32_t year, int32_t month );

private:
	Date m_date;
	// Время суток точнее, чем во float окна: при реальной скорости приращение кадра (~10⁻⁶ ч) — порядка шага float у 12 ч
	double m_hours = 12.0;
	float m_timeScale = 0.0f;
	bool m_paused = false;
	double m_gameSeconds = 0.0;
	bool m_gameTimeRunning = true;
};

}
