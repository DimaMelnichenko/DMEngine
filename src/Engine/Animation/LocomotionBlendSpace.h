#pragma once

#include <cstdint>
#include <vector>

namespace GS
{

// Шаг по скорости и повороту — как Blend Space 2D в UE (скорость × поворот), но без триангуляции: точки клипов — ряды
// аллюров. Ряд — клип без поворота со своей скоростью (Animations.locomotion_speed; покой — 0); к ряду ближайшей скорости
// прикреплены клипы на ходу с поворотом — влево и вправо (Animations.locomotion_turn, рад/с: − к −X, влево; + к +X, вправо).
// По скорости — два соседних ряда и вес второго; в ряду — смесь прямого клипа и варианта в сторону поворота с весом
// |поворот| / поворот варианта (до 1; варианта в эту сторону нет — прямо). Клипы на ходу смешиваются одной фазой (sync
// group: нормированное время общее), и фаза идёт так, чтобы смешанный шаг давал заданную скорость: циклов в секунду =
// скорость / Σ вес × длина шага клипа (скорость клипа × его длительность) — копыта не скользят; выше последнего ряда — тот же
// аллюр быстрее. Ряд со скоростью 0 — покой: в фазу не входит, у него своё время, а клип на ходу рядом с ним идёт в своём
// темпе (смешанные ноги и так идут медленнее)
class LocomotionBlendSpace
{
public:
	struct Sample
	{
		uint32_t clip = 0;
		float speed = 0.0f;		// м/с; 0 — покой
		float duration = 0.0f;	// длительность клипа, с
		float turn = 0.0f;		// поворот на ходу, рад/с; 0 — прямо
	};
	// Ряд аллюра: прямой клип и, если есть, клипы с поворотом влево и вправо
	struct Row
	{
		Sample straight;
		Sample left;
		Sample right;
		bool hasLeft = false;
		bool hasRight = false;
	};
	struct Weighted
	{
		uint32_t clip = 0;
		float weight = 0.0f;
		float duration = 0.0f;
		bool moving = false;	// на ходу: время клипа — фаза × длительность; покой — своё время
	};
	static constexpr uint32_t maxWeighted = 4;	// два ряда, в каждом — прямой и вариант с поворотом
	struct Result
	{
		Weighted samples[maxWeighted];
		uint32_t count = 0;
		float phaseRate = 0.0f;	// циклов фазы в секунду
	};

	// Точка: turn = 0 — ряд аллюра, иначе — вариант с поворотом к ряду ближайшей скорости (ряды и варианты в любом порядке)
	void addSample( uint32_t clip, float speed, float duration, float turn = 0.0f );
	bool empty() const { return m_rows.empty(); }
	// Ряды по возрастанию скорости
	const std::vector<Row>& rows() const { return m_rows; }
	Result evaluate( float speed, float turn = 0.0f ) const;

private:
	// Варианты с поворотом — к рядам заново (ряд мог появиться позже варианта)
	void attachTurns();

	std::vector<Row> m_rows;
	std::vector<Sample> m_turns;
};

}
