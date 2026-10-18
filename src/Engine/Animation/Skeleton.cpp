#include "Skeleton.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include "AnimationPose.h"

using namespace DirectX;

namespace GS
{

void BinaryReader::bytes( void* out, size_t count )
{
	if( !m_ok || count > m_size - m_offset )
	{
		m_ok = false;
		std::memset( out, 0, count );
		return;
	}
	std::memcpy( out, m_data + m_offset, count );
	m_offset += count;
}

bool readFileBytes( const std::string& path, std::vector<uint8_t>& data, std::string& reason )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
	{
		reason = "file is not found";
		return false;
	}
	data.assign( std::istreambuf_iterator<char>( file ), std::istreambuf_iterator<char>() );
	return true;
}

bool Skeleton::create( std::vector<Joint> joints, std::string& reason )
{
	if( joints.empty() || joints.size() > maxJoints )
	{
		reason = "joint count " + std::to_string( joints.size() ) + " is not in 1.." + std::to_string( maxJoints );
		return false;
	}
	for( size_t i = 0; i < joints.size(); ++i )
		if( joints[i].parent >= static_cast<int32_t>( i ) || joints[i].parent < -1 )
		{
			reason = "joint " + joints[i].name + ": the parent must come before the child";
			return false;
		}
	m_joints = std::move( joints );

	// Места костей в позе покоя в пространстве модели и обратные к ним
	std::vector<JointTransform> rest( m_joints.size() );
	for( size_t i = 0; i < m_joints.size(); ++i )
		rest[i] = m_joints[i].rest;
	std::vector<XMFLOAT4X4> model;
	modelSpace( *this, rest, model );
	m_inverseBind.resize( m_joints.size() );
	for( size_t i = 0; i < m_joints.size(); ++i )
		XMStoreFloat4x4( &m_inverseBind[i], XMMatrixInverse( nullptr, XMLoadFloat4x4( &model[i] ) ) );
	return true;
}

bool Skeleton::parse( const uint8_t* data, size_t size, std::string& reason )
{
	BinaryReader reader( data, size );
	char magic[4];
	reader.bytes( magic, sizeof( magic ) );
	const uint32_t version = reader.read<uint32_t>();
	const uint32_t count = reader.read<uint32_t>();
	if( !reader.ok() || std::memcmp( magic, "DMSK", 4 ) != 0 )
	{
		reason = "not a skeleton file (DMSK)";
		return false;
	}
	if( version != 1 )
	{
		reason = "skeleton file version " + std::to_string( version ) + ", expected 1";
		return false;
	}
	if( count == 0 || count > maxJoints )
	{
		reason = "joint count " + std::to_string( count );
		return false;
	}
	std::vector<Joint> joints( count );
	for( Joint& joint : joints )
	{
		const uint32_t length = reader.read<uint32_t>();
		if( length > reader.remaining() )
		{
			reason = "the file is cut short";
			return false;
		}
		joint.name.resize( length );
		reader.bytes( joint.name.data(), length );
		joint.parent = reader.read<int32_t>();
		joint.rest = reader.read<JointTransform>();
	}
	if( !reader.ok() )
	{
		reason = "the file is cut short";
		return false;
	}
	return create( std::move( joints ), reason );
}

bool Skeleton::load( const std::string& path, std::string& reason )
{
	std::vector<uint8_t> data;
	return readFileBytes( path, data, reason ) && parse( data.data(), data.size(), reason );
}

int32_t Skeleton::find( const std::string& name ) const
{
	for( size_t i = 0; i < m_joints.size(); ++i )
		if( m_joints[i].name == name )
			return static_cast<int32_t>( i );
	return -1;
}

}
