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

AnimationClip::RootMotion AnimationClip::rootMotionAt( double time ) const
{
	if( m_rootMotion.empty() )
		return RootMotion();
	if( m_frameCount == 1 )
		return m_rootMotion.front();
	const double position = std::clamp( time, 0.0, static_cast<double>( duration() ) ) * m_frameRate;
	const uint32_t frame = std::min( static_cast<uint32_t>( position ), m_frameCount - 2 );
	const float weight = static_cast<float>( std::clamp( position - frame, 0.0, 1.0 ) );
	const RootMotion& a = m_rootMotion[frame];
	const RootMotion& b = m_rootMotion[frame + 1];
	return RootMotion{ a.x + ( b.x - a.x ) * weight, a.z + ( b.z - a.z ) * weight, a.yaw + ( b.yaw - a.yaw ) * weight };
}

namespace
{

// Вектор (x, z) повёрнутый вокруг вертикали на angle (+Z к +X)
void rotateYaw( float x, float z, float angle, float& outX, float& outZ )
{
	const float c = std::cos( angle ), s = std::sin( angle );
	outX = x * c + z * s;
	outZ = -x * s + z * c;
}

}

AnimationClip::RootMotion AnimationClip::rootMotionDelta( double from, double to ) const
{
	RootMotion total;
	const double length = duration();
	if( m_rootMotion.empty() || !( length > 0.0 ) || to <= from )
		return total;
	// Отрезок в пределах одного прохода клипа: сдвиг по кривой — в осях корня в его начале
	auto segment = [this]( double a, double b )
	{
		const RootMotion start = rootMotionAt( a );
		const RootMotion end = rootMotionAt( b );
		RootMotion delta;
		rotateYaw( end.x - start.x, end.z - start.z, -start.yaw, delta.x, delta.z );
		delta.yaw = end.yaw - start.yaw;
		return delta;
	};
	if( !m_looping )
		return segment( std::clamp( from, 0.0, length ), std::clamp( to, 0.0, length ) );
	// Зацикленный — по проходам: каждый следующий отрезок — в осях корня после предыдущих
	double t = from;
	for( int guard = 0; t < to && guard < 1000; ++guard )
	{
		// Запас на округление: t ровно на стыке — уже следующий проход
		const double pass = std::floor( t / length + 1e-9 );
		const double end = std::min( to, ( pass + 1.0 ) * length );
		if( end <= t )
			break;
		const double a = t - pass * length;
		const double b = end >= ( pass + 1.0 ) * length ? length : end - pass * length;
		const RootMotion delta = segment( a, b );
		float x, z;
		rotateYaw( delta.x, delta.z, total.yaw, x, z );
		total.x += x;
		total.z += z;
		total.yaw += delta.yaw;
		t = end;
	}
	return total;
}

}
