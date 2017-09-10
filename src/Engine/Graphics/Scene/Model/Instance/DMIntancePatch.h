#pragma once
#include "DMObject.h"

template<class T>
class DMIntancePatch : public DMObject
{
public:
	DMIntancePatch(  )
	{
	};

	~DMIntancePatch()
	{
		
	};
	
	void addInstance( T& obj )
	{
		m_instaces.push_back( obj );
	};

	T* intances()
	{
		return &m_instaces[0];
	}

	int size()
	{
		return m_instaces.size();
	}

private:
	std::vector<T> m_instaces;
};

