#include "AnimationLibrary.h"

namespace GS
{

namespace
{
const std::string noName;
const std::vector<uint32_t> noClips;
}

void AnimationLibrary::addSkeleton( uint32_t id, const std::string& name, std::shared_ptr<const Skeleton> skeleton )
{
	SkeletonEntry& entry = m_skeletons[id];
	entry.name = name;
	entry.skeleton = std::move( skeleton );
}

bool AnimationLibrary::addClip( uint32_t id, uint32_t skeleton, const std::string& name,
								std::shared_ptr<const AnimationClip> clip, float locomotionSpeed )
{
	auto owner = m_skeletons.find( skeleton );
	if( owner == m_skeletons.end() || !clip || clip->jointCount() != owner->second.skeleton->jointCount() )
		return false;
	if( locomotionSpeed >= 0.0f )
		owner->second.locomotion.addSample( id, locomotionSpeed, clip->duration() );
	owner->second.clips.push_back( id );
	m_clips[id] = ClipEntry{ name, skeleton, std::move( clip ) };
	return true;
}

void AnimationLibrary::clear()
{
	m_skeletons.clear();
	m_clips.clear();
}

const Skeleton* AnimationLibrary::skeleton( uint32_t id ) const
{
	auto it = m_skeletons.find( id );
	return it != m_skeletons.end() ? it->second.skeleton.get() : nullptr;
}

const std::string& AnimationLibrary::skeletonName( uint32_t id ) const
{
	auto it = m_skeletons.find( id );
	return it != m_skeletons.end() ? it->second.name : noName;
}

const AnimationClip* AnimationLibrary::clip( uint32_t id ) const
{
	auto it = m_clips.find( id );
	return it != m_clips.end() ? it->second.clip.get() : nullptr;
}

const std::string& AnimationLibrary::clipName( uint32_t id ) const
{
	auto it = m_clips.find( id );
	return it != m_clips.end() ? it->second.name : noName;
}

uint32_t AnimationLibrary::findClip( uint32_t skeleton, const std::string& name ) const
{
	for( const auto& [id, entry] : m_clips )
		if( entry.skeleton == skeleton && entry.name == name )
			return id;
	return 0;
}

const std::vector<uint32_t>& AnimationLibrary::clips( uint32_t skeleton ) const
{
	auto it = m_skeletons.find( skeleton );
	return it != m_skeletons.end() ? it->second.clips : noClips;
}

const BlendSpace1D* AnimationLibrary::locomotion( uint32_t skeleton ) const
{
	auto it = m_skeletons.find( skeleton );
	return it != m_skeletons.end() && !it->second.locomotion.empty() ? &it->second.locomotion : nullptr;
}

}
