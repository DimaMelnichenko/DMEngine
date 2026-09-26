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

float DMModel::lodRange( uint16_t index ) const
{
	return index < m_lods.size() ? m_lods[index].first : 0.0f;
}

uint16_t DMModel::lodCount()
{
	return m_lods.size();
}


PropertyContainer* DMModel::properties()
{
	return &m_properties;
}

}