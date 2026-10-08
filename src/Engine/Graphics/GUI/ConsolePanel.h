#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "Properties/PropertyContainer.h"

struct ImGuiInputTextCallbackData;

// Окно Output — вкладки Console и Log, как консоль ~ и Output Log в UE.
// Console: строка ввода на тех же консольных командах, что удалённое управление (ConsoleCommands, docs/remote.md):
// набранное выполняется в начале следующего кадра (takePending — DMGraphics), ответ — в окне; история ↑ / ↓,
// дополнение Tab по командам и путям свойств для get / set / list.
// Log: последние строки log.txt (Logger::recent) с поиском и подсветкой ошибок и предупреждений
class ConsolePanel
{
public:
	// commands — имена команд, containers — окна свойств (пути для дополнения)
	void draw( bool* open, const std::vector<std::string>& commands, const std::vector<PropertyContainer*>& containers );
	std::vector<std::string> takePending();
	// Ответ команды (строки и статус ok / error:) — в вывод
	void appendReply( const std::string& text );
	// Фокус на строку ввода (горячая клавиша консоли)
	void focusInput() { m_focusInput = true; }

private:
	enum class Kind { text, input, error };
	struct Line
	{
		std::string text;
		Kind kind;
	};

	void drawConsole();
	void drawLog();
	static int inputCallback( ImGuiInputTextCallbackData* data );
	void complete( ImGuiInputTextCallbackData* data );
	void history( ImGuiInputTextCallbackData* data );
	void propertyPaths( PropertyContainer& container, const std::string& path, bool windowsOnly, std::vector<std::string>& paths ) const;

	std::vector<Line> m_output;
	std::string m_input;
	std::vector<std::string> m_history;
	int m_historyPosition = -1;	// −1 — новая строка
	std::vector<std::string> m_pending;
	bool m_scrollToBottom = false;
	bool m_focusInput = false;

	const std::vector<std::string>* m_commands = nullptr;
	const std::vector<PropertyContainer*>* m_containers = nullptr;

	std::string m_logFilter;
	std::vector<std::string> m_logLines;
	uint64_t m_logTotal = ~0ull;
	bool m_logErrorsOnly = false;
};
