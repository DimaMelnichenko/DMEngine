#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Сторож кадра (как детектор зависаний ThreadHeartBeat в UE): главный поток отмечает шаги кадра (stage, Scope), свой
// поток дважды в секунду смотрит, сколько длится текущий шаг. Дольше limit — строка «Frame watchdog» в лог: кадр, шаг,
// сколько стоит, последняя начатая метка (event: проходы и объекты кадра — DMD3D::beginEvent) и что скажет probe (у
// DMGraphics — ход GPU по fence); в первый раз — ещё стек главного потока с символами (PDB рядом с exe), как отчёт
// о зависании в UE. Пока стоит — снова раз в limit × 3, и строка, когда кадр пошёл. Зависание видно в log.txt без
// отладчика, а Tools/engine.py и Tools/run.ps1 по таймауту печатают эти строки
class FrameWatchdog
{
public:
	static FrameWatchdog& instance();
	~FrameWatchdog();

	void start( std::chrono::milliseconds limit, std::function<std::string()> probe );
	void stop();

	// Главный поток: начался шаг кадра — name строковый литерал (хранится указатель); newFrame — с него начинается кадр
	void stage( const char* name, bool newFrame = false );
	// Главный поток: метка внутри шага (имя копируется) — проход или объект кадра, как событие PIX
	void event( const char* name );
	// Ожидание внутри шага (GPU, swap chain): на время области — свой шаг, потом прежний с прежним началом
	class Scope
	{
	public:
		explicit Scope( const char* name );
		~Scope();
		Scope( const Scope& ) = delete;
		Scope& operator=( const Scope& ) = delete;

	private:
		const char* m_previous;
		int64_t m_previousStart;
	};

private:
	FrameWatchdog() = default;
	void run();
	void logMainThreadStack();
	static int64_t now();	// мкс steady_clock

	std::atomic<const char*> m_stage{ nullptr };
	std::atomic<int64_t> m_stageStart{ 0 };
	std::atomic<uint64_t> m_frame{ 0 };
	std::chrono::milliseconds m_limit{ 10000 };
	std::function<std::string()> m_probe;
	std::mutex m_eventMutex;
	char m_event[96] = {};
	std::thread m_thread;
	void* m_mainThread = nullptr;	// HANDLE главного потока — того, что зовёт start
	std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_stopping = false;
};
