#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include "Engine\Simulation\World.h"
#include "Skeleton.h"

namespace GS
{

class AnimationLibrary;

// Что играет скелет: шаг по скорости и повороту (LocomotionBlendSpace скелета) или один клип. Время — без свёртки по
// длительности (свёртка — при выборке клипа), поэтому прошлое и текущее смешиваются линейно и на стыке цикла
struct AnimationPlayback
{
	enum class Mode : uint8_t
	{
		locomotion,		// шаг по скорости и повороту: фаза клипов на ходу, своё время у покоя
		clip			// один клип: зацикленный — по кругу, разовый — до последнего кадра или до следующего в очереди
	};
	Mode mode = Mode::locomotion;
	uint32_t clip = 0;			// клип в режиме clip (AnimationLibrary)
	double time = 0.0;			// время клипа (clip) или покоя (locomotion), с
	double phase = 0.0;			// фаза шага, циклы (locomotion)
	float speed = 0.0f;			// сглаженная скорость по земле, м/с (locomotion)
	float turn = 0.0f;			// сглаженный поворот, рад/с: + — к +X от +Z (locomotion)
};

// Анимация сущности — как состояние Anim Instance в UE: скелет, что играет сейчас и на прошлом шаге (для смешения при
// отрисовке по доле шага, как PreviousTransform), переход от прежнего (crossfade: прежнее — застывшей позой, доля растёт за
// fadeTime) и очередь — что играть, когда разовый клип дойдёт до конца (цепочка «лечь → дышать лёжа», «развернуться → шаг»)
struct AnimationState
{
	static constexpr float fadeTime = 0.25f;	// с
	static constexpr uint32_t maxQueue = 4;

	uint32_t skeleton = 0;
	AnimationPlayback current;
	AnimationPlayback previous;
	AnimationPlayback fadeFrom;
	float fade = 1.0f;			// доля перехода к current, 1 — перехода нет
	float previousFade = 1.0f;
	AnimationPlayback queue[maxQueue];
	uint32_t queued = 0;
};

// Сменить то, что играет, — с переходом от того, что видно сейчас (очередь не трогает)
void playAnimation( AnimationState& state, const AnimationPlayback& next );
// Цепочка: первое — сейчас, с переходом; остальные (до maxQueue) — по очереди, когда клип дойдёт до конца: перед следующим
// клип играет один проход, и зацикленный. Последний в цепочке зацикленный клип и шаг по скорости играют до новой команды
void playSequence( AnimationState& state, const AnimationPlayback* items, size_t count );
// Клип-действие (лечь, развернуться, прыжок) ведёт сущность сам, как монтаж с Root Motion в UE: поведение её не двигает и не
// поворачивает, передвижение — движением корня клипа (MovementSystem, AnimationClip::rootMotionDelta); шаг по скорости — нет
inline bool holdsPlace( const AnimationState& state )
{
	return state.current.mode == AnimationPlayback::Mode::clip;
}
// Один клип с начала
AnimationPlayback clipPlayback( uint32_t clip, double time = 0.0 );

// Шаг анимации сущностей с AnimationState (после MovementSystem): у шага по скорости — фактические скорость и поворот по
// смещению и повороту Transform за шаг (упёрлась — встаёт), сглаженные; фаза — по LocomotionBlendSpace скелета; у клипа —
// время, разовый у конца — следующий из очереди. Позу не считает: её собирает отрисовка (evaluatePose) только у видимых
class AnimationSystem : public SimulationSystem
{
public:
	static constexpr float speedSmoothing = 0.15f;	// постоянная времени сглаживания скорости и поворота, с

	const char* name() const override { return "Animation"; }
	void step( World& world, float seconds ) override;
};

// LOD анимации по расстоянию (как Update Rate Optimizations в UE): шаг обновления позы в кадрах —
// 2^⌊distance / lodDistance⌋, не больше maxSkip (1 — каждый кадр; lodDistance ≤ 0 — тоже)
inline uint32_t animationUpdateStep( float distance, float lodDistance, uint32_t maxSkip )
{
	if( !( lodDistance > 0.0f ) || maxSkip <= 1 )
		return 1;
	uint32_t step = 1;
	for( float level = distance / lodDistance; level >= 1.0f && step < maxSkip; level -= 1.0f )
		step *= 2;
	return step < maxSkip ? step : maxSkip;
}
// Считать ли позу в этом кадре: у сущностей с одним шагом кадры разнесены по номеру сущности — за step кадров каждая ровно раз
inline bool updatesThisFrame( uint64_t frame, uint32_t serial, uint32_t step )
{
	return step <= 1 || ( frame + serial ) % step == 0;
}

// Рабочие массивы позы — у вызывающего, без выделений памяти в кадре
struct PoseScratch
{
	std::vector<JointTransform> a, b, c;
};

// Локальные места костей в момент между прошлым и текущим шагом (alpha — World::interpolation). false — скелета нет
bool evaluatePose( const AnimationLibrary& library, const AnimationState& state, float alpha, PoseScratch& scratch,
				   std::vector<JointTransform>& pose );

}
