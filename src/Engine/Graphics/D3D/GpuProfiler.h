#pragma once

#include <string>
#include <utility>
#include <vector>

// Время GPU по областям кадра и метки событий для PIX. Веха M5 переезда на D3D12 (docs/d3d12_migration.md): куча
// запросов TIMESTAMP, ResolveQueryData в readback, PIXBeginEvent / PIXEndEvent с именами областей. До неё области
// только считаются, времена — нули
class GpuProfiler
{
public:
	bool initialize();
	void beginFrame();
	void endFrame();
	// Области могут вкладываться; кадр не должен содержать больше maxScopes областей
	void beginScope( const std::string& name );
	void endScope();
	// Время областей, мс, в порядке их начала, и время всего кадра между beginFrame и endFrame
	const std::vector<std::pair<std::string, float>>& results() const { return m_results; }
	float frameMilliseconds() const { return m_frameMilliseconds; }

private:
	static constexpr uint32_t maxScopes = 64;

	std::vector<std::pair<std::string, float>> m_results;
	std::vector<std::pair<std::string, float>> m_frameScopes;
	float m_frameMilliseconds = 0.0f;
	bool m_frameOpen = false;
};
