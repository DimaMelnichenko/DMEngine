#pragma once

#include <string>
#include <utility>
#include <vector>
#include "GpuResources.h"
#include "ReadbackRing.h"

// Время GPU по областям кадра и метки событий для PIX. Запросы TIMESTAMP в куче DMD3D (по паре на область и пара на
// кадр, свой участок на кадр в полёте), в конце кадра ResolveQueryData в буфер и копия в ReadbackRing: результат читается
// без ожидания и отстаёт от кадра на 2–3 кадра, имена областей того кадра хранятся рядом с копией. Каждая область — ещё
// PIXBeginEvent / PIXEndEvent в командном списке: в захвате PIX кадр разбит на те же области, что в «Statistic».
// Метка времени пишется в точке команды и не ждёт окончания работы GPU: достоверен итог кадра и проходов, часть работы
// области (пиксели после последнего вызова) попадает в следующую
class GpuProfiler
{
public:
	bool initialize();
	void beginFrame();
	void endFrame();
	// Области могут вкладываться; кадр не должен содержать больше maxScopes областей — лишние не замеряются
	void beginScope( const std::string& name );
	void endScope();
	// Время областей, мс, в порядке их начала, и время всего кадра между beginFrame и endFrame — для кадра, который GPU
	// уже закончил (2–3 кадра назад)
	const std::vector<std::pair<std::string, float>>& results() const { return m_results; }
	float frameMilliseconds() const { return m_frameMilliseconds; }

private:
	static constexpr uint32_t maxScopes = 64;
	static constexpr uint32_t queriesPerFrame = 2 + 2 * maxScopes;	// [0, 1] — кадр, [2 + 2i, 3 + 2i] — область i
	static constexpr uint32_t readbackCount = 4;					// копий в кольце: GPU отстаёт на 1–2 кадра
	struct Timestamps
	{
		uint64_t values[queriesPerFrame];
	};

	uint32_t queryBase() const { return ( m_frame % DMD3D::frameCount ) * queriesPerFrame; }
	void readResults();

	std::vector<std::string> m_frameScopes;			// области текущего кадра в порядке начала
	std::vector<uint32_t> m_openScopes;				// стек начатых областей (индексы в m_frameScopes)
	std::vector<std::string> m_pushedScopes[readbackCount];	// области кадров, чьи копии лежат в кольце
	Buffer m_resolved[DMD3D::frameCount];			// ResolveQueryData пишет сюда, оттуда — копия в кольцо
	ReadbackRing<Timestamps> m_readback;
	std::vector<std::pair<std::string, float>> m_results;
	float m_frameMilliseconds = 0.0f;
	uint32_t m_frame = 0;
	bool m_frameOpen = false;
};
