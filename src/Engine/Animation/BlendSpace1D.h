#pragma once

#include <cstdint>
#include <vector>

namespace GS
{

// Шаг по скорости — как Blend Space 1D в UE: точки «клип — скорость» (Animations.locomotion_speed), по скорости — две соседние
// точки и вес второй. Клипы на ходу смешиваются одной фазой (sync group: нормированное время общее), и фаза идёт так, чтобы
// смешанный шаг давал заданную скорость: циклов в секунду = скорость / Σ вес × длина шага клипа (скорость клипа × его
// длительность) — копыта не скользят; выше последней точки — тот же клип быстрее. Точка со скоростью 0 — покой: в фазу не
// входит, у него своё время, а клип на ходу рядом с ним идёт в своём темпе (смешанные ноги и так идут медленнее)
class BlendSpace1D
{
public:
	struct Sample
	{
		uint32_t clip = 0;
		float speed = 0.0f;		// м/с; 0 — покой
		float duration = 0.0f;	// длительность клипа, с
	};
	struct Weighted
	{
		uint32_t clip = 0;
		float weight = 0.0f;
		float duration = 0.0f;
		bool moving = false;	// на ходу: время клипа — фаза × длительность; покой — своё время
	};
	struct Result
	{
		Weighted samples[2];
		uint32_t count = 0;
		float phaseRate = 0.0f;	// циклов фазы в секунду
	};

	// Точки — по возрастанию скорости
	void addSample( uint32_t clip, float speed, float duration );
	bool empty() const { return m_samples.empty(); }
	const std::vector<Sample>& samples() const { return m_samples; }
	Result evaluate( float speed ) const;

private:
	std::vector<Sample> m_samples;
};

}
