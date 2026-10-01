#pragma once

#include <string>
#include <utility>
#include <vector>

namespace GS
{

// Счётчики кадра для окна «Statistic»: строка формата printf с одним числом («Meshes = %.0f») и значение. Пишут рендерер
// и DMGraphics, показывает GUI (GUI::Begin); DMGraphics очищает их в конце кадра — с интерфейсом и без него
struct FrameStats
{
	void add( const std::string& format, float value ) { counters.emplace_back( format, value ); }
	void clear() { counters.clear(); }

	std::vector<std::pair<std::string, float>> counters;
};

}
