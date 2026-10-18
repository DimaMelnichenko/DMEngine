#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Skeleton.h"
#include "AnimationClip.h"
#include "LocomotionBlendSpace.h"

namespace GS
{

// Скелеты и клипы уровня — служба мира (World::animations(), как рельеф и столкновения): id — строк Skeletons и Animations
// базы (их кладёт загрузка уровня); у скелета — его клипы и шаг по скорости и повороту из клипов с locomotion_speed и
// locomotion_turn. Ядро без базы:
// файлы читает Skeleton::load / AnimationClip::load, что грузить — решает движок
class AnimationLibrary
{
public:
	void addSkeleton( uint32_t id, const std::string& name, std::shared_ptr<const Skeleton> skeleton );
	// locomotionSpeed < 0 — клип не точка шага по скорости и повороту; locomotionTurn — поворот клипа на ходу, рад/с (0 —
	// прямо: ряд аллюра). false — у клипа другое число костей или нет скелета
	bool addClip( uint32_t id, uint32_t skeleton, const std::string& name, std::shared_ptr<const AnimationClip> clip,
				  float locomotionSpeed, float locomotionTurn = 0.0f );
	void clear();

	const Skeleton* skeleton( uint32_t id ) const;
	const std::string& skeletonName( uint32_t id ) const;
	const AnimationClip* clip( uint32_t id ) const;
	const std::string& clipName( uint32_t id ) const;
	// Клип скелета по имени; 0 — нет
	uint32_t findClip( uint32_t skeleton, const std::string& name ) const;
	// Клипы скелета по порядку добавления
	const std::vector<uint32_t>& clips( uint32_t skeleton ) const;
	// Шаг по скорости скелета; nullptr — точек нет
	const LocomotionBlendSpace* locomotion( uint32_t skeleton ) const;
	size_t skeletonCount() const { return m_skeletons.size(); }
	size_t clipCount() const { return m_clips.size(); }

private:
	struct SkeletonEntry
	{
		std::string name;
		std::shared_ptr<const Skeleton> skeleton;
		std::vector<uint32_t> clips;
		LocomotionBlendSpace locomotion;
	};
	struct ClipEntry
	{
		std::string name;
		uint32_t skeleton = 0;
		std::shared_ptr<const AnimationClip> clip;
	};
	std::unordered_map<uint32_t, SkeletonEntry> m_skeletons;
	std::unordered_map<uint32_t, ClipEntry> m_clips;
};

}
