#pragma once

#include <string>
#include <vector>
#include "FrameStats.h"

// Статистика кадра: оверлей поверх сцены (FPS, кадр CPU и GPU, график времени кадра) и панель Stats — таблица времени
// CPU и GPU проходов и объектов с сортировкой и прочие счётчики (FrameStats: «имя = формат»)
class StatsPanel
{
public:
	// Раз за кадр, до draw: счётчики кадра в историю
	void update( const GS::FrameStats& stats );
	// Оверлей у верхнего левого угла области сцены (corner — точка экрана)
	void drawOverlay( float cornerX, float cornerY );
	void draw( bool* open );

private:
	struct Timing
	{
		std::string name;
		float cpu = -1.0f;	// мс; < 0 — нет
		float gpu = -1.0f;
	};
	struct Counter
	{
		std::string name;
		std::string format;	// printf для значения с единицами
		float value = 0.0f;
	};

	std::vector<Timing> m_timings;
	std::vector<Counter> m_counters;
	float m_gpuFrame = 0.0f;
	static constexpr int historySize = 300;
	float m_frameHistory[historySize] = {};
	float m_gpuHistory[historySize] = {};
	int m_historyOffset = 0;
	std::string m_filter;
};
