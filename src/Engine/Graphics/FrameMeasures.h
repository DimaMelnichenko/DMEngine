#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace GS
{

// Средние значений кадра за несколько секунд — замеры stat gpu и stat cpu удалённого управления и строки «GPU average» /
// «CPU average» в log.txt после прогрева (их печатает Tools/run.ps1): значения кадра по именам, одноимённые за кадр
// складываются (объект в нескольких проходах и каскадах), затем усредняются по кадрам, где они были
class FrameMeasures
{
public:
	using Values = std::vector<std::pair<std::string, float>>;

	// title — начало строки: «GPU», «CPU»
	explicit FrameMeasures( std::string title ) : m_title( std::move( title ) ) {}

	// Новый замер на seconds секунд с этого кадра; done получает строку «<title> average over N frames, ms: имя значение; …»
	void start( float seconds, std::function<void( const std::string& )> done );
	bool active() const { return !m_measures.empty(); }
	// Значения кадра (первое — весь кадр: по нему считаются кадры замера); законченные замеры — строкой в done, в конце
	// строки — tail() (счётчики кадра: меши, экспозиция), если он задан
	void accumulate( const Values& values, const std::function<std::string()>& tail = {} );

private:
	struct Average
	{
		std::string name;
		double sum = 0.0;
		uint32_t count = 0;
	};
	struct Measure
	{
		std::vector<Average> averages;	// первое — весь кадр
		std::chrono::steady_clock::time_point start;
		std::chrono::duration<float> duration;
		std::function<void( const std::string& )> done;
	};

	std::string m_title;
	std::vector<Measure> m_measures;
};

}
