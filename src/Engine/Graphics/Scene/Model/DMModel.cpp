#include "DMModel.h"
#include <fstream>

namespace GS
{

DMModel::DMModel( uint32_t id, const std::string& name ) : DMResource( id, name )
{
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

void DMModel::addLod( float range, uint32_t meshId, uint32_t materialId )
{
	m_lods.insert( { range , { meshId, materialId } } );
}

bool DMModel::getLod( float range, uint32_t& meshId, uint32_t& materialId )
{
	for( auto& it : m_lods )
	{
		if( range <= it.first )
		{
			meshId = it.second.mesh;
			materialId = it.second.material;
			return true;
		}
	}
	return false;
}

const DMTransformBuffer& DMModel::transformBuffer() const
{
	return m_transformBuffer;
}

DMTransformBuffer& DMModel::transformBuffer()
{
	return m_transformBuffer;
}


void DMModel::copyTo( DMModel& other )
{
	other.m_lods = m_lods;
	other.m_transformBuffer = m_transformBuffer;
}

}