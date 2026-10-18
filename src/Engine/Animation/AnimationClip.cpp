#include "AnimationClip.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "AnimationPose.h"

namespace GS
{

bool AnimationClip::create( uint32_t jointCount, float frameRate, std::vector<JointTransform> frames, bool looping,
							std::string& reason )
{
	if( jointCount == 0 || frames.empty() || frames.size() % jointCount != 0 || !( frameRate > 0.0f ) )
	{
		reason = "frames do not match the joint count";
		return false;
	}
	m_jointCount = jointCount;
	m_frameCount = static_cast<uint32_t>( frames.size() / jointCount );
	m_frameRate = frameRate;
	m_looping = looping;
	m_frames = std::move( frames );
	m_rootMotion.assign( m_frameCount, RootMotion() );
	return true;
}

bool AnimationClip::parse( const uint8_t* data, size_t size, std::string& reason )
{
	BinaryReader reader( data, size );
	char magic[4];
	reader.bytes( magic, sizeof( magic ) );
	const uint32_t version = reader.read<uint32_t>();
	if( !reader.ok() || std::memcmp( magic, "DMAN", 4 ) != 0 )
	{
		reason = "not an animation file (DMAN)";
		return false;
	}
	if( version != 1 )
	{
		reason = "animation file version " + std::to_string( version ) + ", expected 1";
		return false;
	}
	const uint32_t jointCount = reader.read<uint32_t>();
	const uint32_t frameCount = reader.read<uint32_t>();
	const float frameRate = reader.read<float>();
	const uint32_t flags = reader.read<uint32_t>();
	const float rootSpeed = reader.read<float>();
	const float rootTurn = reader.read<float>();
	const DirectX::XMFLOAT3 boundsMin = reader.read<DirectX::XMFLOAT3>();
	const DirectX::XMFLOAT3 boundsMax = reader.read<DirectX::XMFLOAT3>();
	const size_t count = static_cast<size_t>( jointCount ) * frameCount;
	if( !reader.ok() || jointCount == 0 || frameCount == 0 ||
		reader.remaining() < count * sizeof( JointTransform ) + frameCount * sizeof( RootMotion ) )
	{
		reason = "the file is cut short";
		return false;
	}
	std::vector<JointTransform> frames( count );
	reader.bytes( frames.data(), count * sizeof( JointTransform ) );
	std::vector<RootMotion> rootMotion( frameCount );
	reader.bytes( rootMotion.data(), frameCount * sizeof( RootMotion ) );
	if( !create( jointCount, frameRate, std::move( frames ), ( flags & 1u ) != 0, reason ) )
		return false;
	m_rootMotion = std::move( rootMotion );
	m_rootSpeed = rootSpeed;
	m_rootTurn = rootTurn;
	m_boundsMin = boundsMin;
	m_boundsMax = boundsMax;
	return true;
}

bool AnimationClip::load( const std::string& path, std::string& reason )
{
	std::vector<uint8_t> data;
	return readFileBytes( path, data, reason ) && parse( data.data(), data.size(), reason );
}

void AnimationClip::sample( double time, std::vector<JointTransform>& pose ) const
{
	pose.resize( m_jointCount );
	if( m_frameCount == 1 )
	{
		std::copy( m_frames.begin(), m_frames.end(), pose.begin() );
		return;
	}
	const double length = duration();
	double t = time;
	if( m_looping )
	{
		t = std::fmod( t, length );
		if( t < 0.0 )
			t += length;
	}
	else
		t = std::clamp( t, 0.0, length );
	// Кадр и доля до следующего: последний отрезок — между предпоследним и последним кадром
	const double position = t * m_frameRate;
	const uint32_t frame = std::min( static_cast<uint32_t>( position ), m_frameCount - 2 );
	const float weight = static_cast<float>( std::clamp( position - frame, 0.0, 1.0 ) );
	const JointTransform* a = &m_frames[static_cast<size_t>( frame ) * m_jointCount];
	const JointTransform* b = a + m_jointCount;
	for( uint32_t i = 0; i < m_jointCount; ++i )
		pose[i] = blendJoint( a[i], b[i], weight );
}

}
