#pragma once
#include <functional>
#include <string>
#include <vector>

// Прогресс долгой работы, как FScopedSlowTask в UE: задача объявляет общий объём и входит в его части
// (enterProgressFrame), задачи вкладываются — доля вложенной идёт внутри текущей части внешней. Показывает их обработчик
// (setHandler: экран загрузки DMGraphics); без обработчика задачи ничего не делают, поэтому их можно ставить и в код,
// который работает и в кадре (перестройка русел). Части отмечаются на границах этапов: обработчик может нарисовать кадр,
// а он сбрасывает привязки и состояние GPU. Показ — только в enterProgressFrame (создание задачи кадр не рисует)
class SlowTask
{
public:
	// Текст — части вложенных задач через « — » (у части без текста — текст её задачи), доля — 0…1 от всей работы
	using Handler = std::function<void( const std::string& message, float progress )>;
	static void setHandler( Handler handler );

	SlowTask( float totalWork, std::string message = {} );
	~SlowTask();
	SlowTask( const SlowTask& ) = delete;
	SlowTask& operator=( const SlowTask& ) = delete;

	// Следующая часть работы объёмом amount (в единицах totalWork); прежняя считается сделанной
	void enterProgressFrame( float amount = 1.0f, std::string message = {} );

private:
	static void report();

	float m_totalWork = 1.0f;
	float m_completedWork = 0.0f;
	float m_frameWork = 0.0f;
	std::string m_message;
	std::string m_frameMessage;

	static std::vector<SlowTask*> s_stack;
	static Handler s_handler;
};
