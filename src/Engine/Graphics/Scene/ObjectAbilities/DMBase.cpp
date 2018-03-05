#include "DMBase.h"


DMBase::DMBase()
{
}


DMBase::~DMBase()
{
}

const DMAbility* DMBase::ability( unsigned int id )
{
	if( m_abilies.count( id ) )
		return m_abilies.at( id ).get();

	return nullptr;
}