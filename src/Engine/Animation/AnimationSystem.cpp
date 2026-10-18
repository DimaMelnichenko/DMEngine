#include "AnimationSystem.h"
#include <algorithm>
#include <cmath>
#include "AnimationLibrary.h"
#include "AnimationPose.h"
#include "Engine\Simulation\Components.h"

namespace GS
{

namespace
{

// Время и фаза растут без свёртки; далеко от нуля — сдвиг на целое число циклов у текущего и прошлого вместе (точность float
// у позы не теряется, смешение по доле шага не ломается)
constexpr double wrapAt = 1.0e4;

void wrapTime( double& current, double& previous, double period )
{
	if( current < wrapAt || !( period > 0.0 ) )
		return;
	const double shift = std::floor( previous / period ) * period;
	current -= shift;
	previous -= shift;
}

// То, что играет, между прошлым и текущим шагом; сменилось за шаг (другой режим или клип) — текущее
AnimationPlayback interpolatePlayback( const AnimationPlayback& from, const AnimationPlayback& to, float alpha )
{
	if( from.mode != to.mode || from.clip != to.clip )
		return to;
	AnimationPlayback result = to;
	result.time = from.time + ( to.time - from.time ) * alpha;
	result.phase = from.phase + ( to.phase - from.phase ) * alpha;
	result.speed = from.speed + ( to.speed - from.speed ) * alpha;
	return result;
}

void samplePlayback( const AnimationLibrary& library, uint32_t skeletonId, const Skeleton& skeleton,
					 const AnimationPlayback& playback, std::vector<JointTransform>& temp, std::vector<JointTransform>& pose )
{
	if( playback.mode == AnimationPlayback::Mode::clip )
	{
		if( const AnimationClip* clip = library.clip( playback.clip ) )
			clip->sample( playback.time, pose );
		else
			restPose( skeleton, pose );
		return;
	}
	const BlendSpace1D* locomotion = library.locomotion( skeletonId );
	if( !locomotion )
	{
		restPose( skeleton, pose );
		return;
	}
	const BlendSpace1D::Result result = locomotion->evaluate( playback.speed );
	auto clipTime = [&]( const BlendSpace1D::Weighted& sample )
	{
		return sample.moving ? playback.phase * sample.duration : playback.time;
	};
	library.clip( result.samples[0].clip )->sample( clipTime( result.samples[0] ), pose );
	if( result.count == 2 && result.samples[1].weight > 0.0f )
	{
		library.clip( result.samples[1].clip )->sample( clipTime( result.samples[1] ), temp );
		blendPoses( pose, temp, result.samples[1].weight, pose );
	}
}

}

AnimationPlayback clipPlayback( uint32_t clip, double time )
{
	AnimationPlayback playback;
	playback.mode = AnimationPlayback::Mode::clip;
	playback.clip = clip;
	playback.time = time;
	return playback;
}

void playAnimation( AnimationState& state, const AnimationPlayback& next )
{
	state.fadeFrom = state.current;
	state.current = next;
	state.previous = next;
	state.fade = 0.0f;
	state.previousFade = 0.0f;
}

void AnimationSystem::step( World& world, float seconds )
{
	const AnimationLibrary& library = world.animations();
	entt::registry& registry = world.registry();
	const float smoothing = 1.0f - std::exp( -seconds / speedSmoothing );
	for( auto [entity, state] : registry.view<AnimationState>().each() )
	{
		state.previous = state.current;
		state.previousFade = state.fade;
		state.fade = std::min( state.fade + seconds / AnimationState::fadeTime, 1.0f );
		AnimationPlayback& playback = state.current;
		playback.time += seconds;

		if( playback.mode == AnimationPlayback::Mode::clip )
		{
			if( const AnimationClip* clip = library.clip( playback.clip ) )
			{
				if( clip->looping() )
					wrapTime( playback.time, state.previous.time, clip->duration() );
				else
				{
					// Разовый стоит на последнем кадре — время дальше не нужно
					playback.time = std::min( playback.time, static_cast<double>( clip->duration() ) + 1.0 );
					state.previous.time = std::min( state.previous.time, playback.time );
				}
			}
			continue;
		}

		// Фактическая скорость по земле: упёрлась в ствол или в воду — встаёт, хотя поведение и просит идти
		float measured = 0.0f;
		const Transform* transform = registry.try_get<Transform>( entity );
		const PreviousTransform* before = registry.try_get<PreviousTransform>( entity );
		if( transform && before && seconds > 0.0f )
			measured = std::hypot( transform->position.x - before->transform.position.x,
								   transform->position.z - before->transform.position.z ) / seconds;
		else if( const Locomotion* locomotion = registry.try_get<Locomotion>( entity ) )
			measured = std::hypot( locomotion->velocity.x, locomotion->velocity.y );
		playback.speed += ( measured - playback.speed ) * smoothing;

		if( const BlendSpace1D* locomotion = library.locomotion( state.skeleton ) )
		{
			playback.phase += locomotion->evaluate( playback.speed ).phaseRate * seconds;
			wrapTime( playback.phase, state.previous.phase, 1.0 );
			// Время покоя — по длительности его клипа (точка со скоростью 0)
			const std::vector<BlendSpace1D::Sample>& samples = locomotion->samples();
			if( samples.front().speed == 0.0f )
				wrapTime( playback.time, state.previous.time, samples.front().duration );
		}
	}
}

bool evaluatePose( const AnimationLibrary& library, const AnimationState& state, float alpha, PoseScratch& scratch,
				   std::vector<JointTransform>& pose )
{
	const Skeleton* skeleton = library.skeleton( state.skeleton );
	if( !skeleton )
		return false;
	const AnimationPlayback playback = interpolatePlayback( state.previous, state.current, alpha );
	samplePlayback( library, state.skeleton, *skeleton, playback, scratch.a, pose );
	const float fade = state.previousFade + ( state.fade - state.previousFade ) * alpha;
	if( fade < 1.0f )
	{
		// Прежнее — застывшей позой, на которой его сменили
		samplePlayback( library, state.skeleton, *skeleton, state.fadeFrom, scratch.a, scratch.b );
		blendPoses( scratch.b, pose, fade, pose );
	}
	return true;
}

}
