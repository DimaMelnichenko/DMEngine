#pragma once

#include <windows.h>
#include <deque>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include "ConsoleCommands.h"

namespace GS
{

// Удалённое управление запущенным движком, как Remote Control API в UE, но локально: именованный канал
// \\.\pipe\DMEngine, по строке на команду (ConsoleCommands), ответ — строки текста, последняя ok или error: ….
// Канал читает свой поток и кладёт команды в очередь; выполняет их главный поток в начале кадра (poll), следующую
// команду соединения поток читает только после ответа на предыдущую. Клиент — Tools/engine.py. Включается
// параметром -remote; удалённые (сетевые) клиенты отклоняются, писать в канал может только сам пользователь
class RemoteControlServer
{
public:
	static constexpr const wchar_t* pipeName = L"\\\\.\\pipe\\DMEngine";

	RemoteControlServer() = default;
	~RemoteControlServer();

	bool start();
	// Поток канала выходит; ответ, уже отданный главным потоком (quit), он успевает дописать
	void stop();
	// Главный поток: выполнить пришедшие команды
	void poll( ConsoleCommands& commands );

private:
	struct Request
	{
		std::string line;
		std::promise<std::string> reply;
	};

	// Одно соединение клиента с экземпляром канала: команды до его отключения или остановки сервера
	void serve( HANDLE pipe );
	// Одна команда: в очередь и ожидание ответа главного потока; false — сервер останавливается
	bool submit( const std::string& line, std::string& reply );
	// Ожидание операции с каналом; stoppable — прервать по остановке (подключение, чтение), иначе — дождаться (запись)
	bool waitIo( HANDLE pipe, OVERLAPPED& overlapped, DWORD& bytes, bool stoppable );

	std::thread m_thread;
	HANDLE m_stopEvent = nullptr;
	HANDLE m_ioEvent = nullptr;
	std::mutex m_mutex;
	std::deque<Request> m_requests;
};

}
