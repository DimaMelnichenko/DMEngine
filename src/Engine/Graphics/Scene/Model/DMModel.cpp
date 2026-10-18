#include "DMModel.h"
#include <fstream>

namespace GS
{

DMModel::DMModel( uint32_t id, const std::string& name ) : DMResource( id, name )
{
	m_properties.setName( name );
}

DMModel::~DMModel()
{

}

DMModel::DMModel( DMModel&& other ) : DMResource( std::move(other) )
{
	*this = std::move( other );
}

DMModel& DMModel::operator=( DMModel&& other )
{
	if( this != &other )
	{
		std::swap( m_lods, other.m_lods );
		m_collision = other.m_collision;
		m_collisionRadius = other.m_collisionRadius;
		m_skeleton = other.m_skeleton;
		m_animatedBounds = other.m_animatedBounds;
	}
	return *this;
}

void DMModel::addLod( float range, std::unique_ptr<LodBlock>&& lod )
{
	m_lods.emplace_back( std::move( std::make_pair( range , std::move( lod ) ) ) );
}

const DMModel::LodBlock* DMModel::getLod( float distance ) const
{
	const int index = lodIndex( distance );
	return index >= 0 ? m_lods[index].second.get() : nullptr;
}

int DMModel::lodIndex( float distance ) const
{
	for( size_t i = 0; i < m_lods.size(); ++i )
	{
		if( distance <= m_lods[i].first )
			return static_cast<int>( i );
	}
	return -1;
}

DMModel::LodBlock* DMModel::getLodById( uint16_t index )
{
	if( index < m_lods.size() )
		return m_lods[index].second.get();
	return nullptr;
}

const DMModel::LodBlock* DMModel::getLodById( uint16_t index ) const
{
	if( index < m_lods.size() )
		return m_lods[index].second.get();
	return nullptr;
}

float DMModel::lodRange( uint16_t index ) const
{
	return index < m_lods.size() ? m_lods[index].first : 0.0f;
}

bool DMModel::parseCollision( const std::string& name, Collision& collision )
{
	static const std::pair<const char*, Collision> names[] = { { "hull", Collision::hull }, { "box", Collision::box },
															   { "mesh", Collision::mesh }, { "capsule", Collision::capsule },
															   { "none", Collision::none } };
	for( const auto& [text, value] : names )
	{
		if( name == text )
		{
			collision = value;
			return true;
		}
	}
	return false;
}

uint16_t DMModel::lodCount() const
{
	return m_lods.size();
}

float DMModel::shadowLodScale() const
{
	return m_properties.exists( "Shadow LOD scale" ) ? m_properties["Shadow LOD scale"].data<float>() : 1.0f;
}

void DMModel::setShadowLodScale( float scale )
{
	Property* property = m_properties.insert( "Shadow LOD scale", scale );
	property->setData( scale );
	property->setLow( 0.1f );
	property->setHigh( 1.0f );
	property->setTooltip( "LOD distances in shadow views are multiplied by it: < 1 - a coarser LOD casts the shadow closer "
						  "(Models.shadow_lod_scale, not saved with the level)" );
}

uint32_t DMModel::shadowMinLod() const
{
	return m_properties.exists( "Shadow min LOD" ) ? m_properties["Shadow min LOD"].data<uint32_t>() : 0;
}

void DMModel::setShadowMinLod( uint32_t lod )
{
	Property* property = m_properties.insert( "Shadow min LOD", lod );
	property->setData( lod );
	property->setLow( 0.0f );
	property->setHigh( static_cast<float>( m_lods.empty() ? 0 : m_lods.size() - 1 ) );
	property->setTooltip( "Shadow views draw at most this detailed LOD: 2 - the shadow of grass is its coarsest LOD even "
						  "next to the camera (Models.shadow_min_lod, not saved with the level)" );
}

PropertyContainer* DMModel::properties()
{
	return &m_properties;
}

}