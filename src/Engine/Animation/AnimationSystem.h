#pragma once

#include <cstdint>
#include <vector>
#include "Engine\Simulation\World.h"
#include "Skeleton.h"

namespace GS
{

class AnimationLibrary;

// Что играет скелет: шаг по скорости (BlendSpace1D скелета) или один клип. Время — без свёртки по длительности (свёртка — при
// выборке клипа), поэтому прошлое и текущее смешиваются линейно и на стыке цикла
struct AnimationPlayback
{
	enum class Mode : uint8_t
	{
		locomotion,		// шаг по скорости: фаза клипов на ходу, своё время у покоя
		clip			// один клип: зацикленный — по кругу, разовый — до последнего кадра
	};
	Mode mode = Mode::locomotion;
	uint32_t clip = 0;			// клип в режиме clip (AnimationLibrary)
	double time = 0.0;			// время клипа (clip) или покоя (locomotion), с
	double phase = 0.0;			// фаза шага, циклы (locomotion)
	float speed = 0.0f;			// сглаженная скорость по земле, м/с (locomotion)
};

// Анимация сущности — как состояние Anim Instance в UE: скелет, что играет сейчас и на прошлом шаге (для смешения при
// отрисовке по доле шага, как PreviousTransform), переход от прежнего (crossfade: прежнее — застывшей позой, доля растёт за
// fadeTime)
struct AnimationState
{
	static constexpr float fadeTime = 0.25f;	// с

	uint32_t skeleton = 0;
	AnimationPlayback current;
	AnimationPlayback previous;
	AnimationPlayback fadeFrom;
	float fade = 1.0f;			// доля перехода к current, 1 — перехода нет
	float previousFade = 1.0f;
};

// Сменить то, что играет, — с переходом от того, что видно сейчас
void playAnimation( AnimationState& state, const AnimationPlayback& next );
// Клип-действие (лечь, удар) держит место сущности: поведение и передвижение её не двигают и не поворачивают, как монтаж в
// UE отключает ввод движения (движение корня из клипа — позже); шаг по скорости — нет
inline bool holdsPlace( const AnimationState& state )
{
	return state.current.mode == AnimationPlayback::Mode::clip;
}
// Один клип с начала
AnimationPlayback clipPlayback( uint32_t clip, double time = 0.0 );

// Шаг анимации сущностей с AnimationState (после MovementSystem): у шага по скорости — фактическая скорость по смещению
// Transform за шаг (упёрлась — встаёт), сглаженная; фаза — по BlendSpace1D скелета; у клипа — время. Позу не считает: её
// собирает отрисовка (evaluatePose) только у видимых
class AnimationSystem : public SimulationSystem
{
public:
	static constexpr float speedSmoothing = 0.15f;	// постоянная времени сглаживания скорости, с

	const char* name() const override { return "Animation"; }
	void step( World& world, float seconds ) override;
};

// Рабочие массивы позы — у вызывающего, без выделений памяти в кадре
struct PoseScratch
{
	std::vector<JointTransform> a, b, c;
};

// Локальные места костей в момент между прошлым и текущим шагом (alpha — World::interpolation). false — скелета нет
bool evaluatePose( const AnimationLibrary& library, const AnimationState& state, float alpha, PoseScratch& scratch,
				   std::vector<JointTransform>& pose );

}
