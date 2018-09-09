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

void DMModel::addLod( float range, const DMModel::LodBlock& block )
{
	m_lods.push_back( { range , block } );
}

const DMModel::LodBlock* DMModel::getLod( float range )
{
	for( auto& it : m_lods )
	{
		if( range <= it.first )
		{
			it.second.resultMatrix = m_transformBuffer.resultMatrixPtr();
			return &it.second;
		}
	}
	return nullptr;
}

DMModel::LodBlock* DMModel::getLodById( uint16_t index )
{
	if( index < m_lods.size() )
	{
		auto& it = m_lods[index];		
		it.second.resultMatrix = m_transformBuffer.resultMatrixPtr();
		return &it.second;
	}
	return nullptr;
}

uint16_t DMModel::lodCount()
{
	return m_lods.size();
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