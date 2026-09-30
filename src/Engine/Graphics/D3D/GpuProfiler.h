#pragma once

#include <string>
#include <utility>
#include <vector>
#include <d3d11_1.h>
#include "Utils\utilites.h"

// Время GPU по областям кадра и метки событий для RenderDoc / PIX. Область — пара запросов D3D11_QUERY_TIMESTAMP
// внутри D3D11_QUERY_TIMESTAMP_DISJOINT кадра и событие ID3DUserDefinedAnnotation с тем же именем. Результаты
// читаются без ожидания GPU, с отставанием на frameLatency кадров; пока они не готовы, остаются прошлые
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
	// Без vsync CPU уходит вперёд GPU на несколько кадров (очередь DXGI — до 3): запас на 5 кадров, чтобы запросы
	// успевали выполниться до повторного использования
	static constexpr uint32_t frameLatency = 5;
	static constexpr uint32_t maxScopes = 64;

	struct Scope
	{
		std::string name;
		uint32_t begin;	// индексы запросов в Frame::timestamps
		uint32_t end;
	};

	struct Frame
	{
		com_unique_ptr<ID3D11Query> disjoint;
		com_unique_ptr<ID3D11Query> frameBegin;
		com_unique_ptr<ID3D11Query> frameEnd;
		std::vector<com_unique_ptr<ID3D11Query>> timestamps;
		std::vector<Scope> scopes;
		std::vector<uint32_t> openScopes;
		uint32_t usedTimestamps = 0;
		bool pending = false;
	};

	bool createQuery( D3D11_QUERY type, com_unique_ptr<ID3D11Query>& query );
	uint64_t timestamp( ID3D11Query* query ) const;
	void collect( Frame& frame );

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	com_unique_ptr<ID3DUserDefinedAnnotation> m_annotation;
	Frame m_frames[frameLatency];
	uint32_t m_current = 0;
	bool m_frameOpen = false;

	std::vector<std::pair<std::string, float>> m_results;
	float m_frameMilliseconds = 0.0f;
};
