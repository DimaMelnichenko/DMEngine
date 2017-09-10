#include "DMObject.h"


DMObject::DMObject( )
{
	//m_parent = 0;
	//setParent( parent );
	//m_finally_destroy = false;
}


DMObject::~DMObject()
{
	//destroy();	
}
/*
void DMObject::setParent(  )
{
	if( !parent )
		return;

	if( m_parent )
	{
		m_parent->removeChild( std::unique_ptr<DMObject>  );
	}

	m_parent = parent;
	parent->addChild( std::unique_ptr<DMObject>  );
}

void DMObject::destroy( )
{
	m_finally_destroy = true;
	for(  obj: m_children )
	{
		//obj->shutdown();
		obj->destroy();
		//delete obj;
	}
	m_finally_destroy = false;

	m_children.clear();

	
	if( m_parent )
	{
		m_parent->removeChild( std::unique_ptr<DMObject>  );
	}
}

void DMObject::addChild(  child )
{
	m_children.push_back( child );
}

void DMObject::removeChild(  child )
{
	if( !m_finally_destroy )
		m_children.remove( child );
}

*/