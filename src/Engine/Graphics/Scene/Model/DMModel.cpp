#include "DMModel.h"
#include <fstream>

namespace GS
{

DMModel::DMModel( uint32_t id, const std::wstring& name ) : DMResource( id )
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
		m_name = other.m_name;
		m_lods = std::move( other.m_lods );
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

}