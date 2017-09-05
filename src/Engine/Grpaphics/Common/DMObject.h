#pragma once

#include <list>
#include <memory>


class DMObject
{
public:
	DMObject();
	virtual ~DMObject();

	//void setParent(  );
	//void destroy();
	//void addChild(  );
	//void removeChild(  );
	//virtual void shutdown() = 0;

private:
	//std::list<std::unique_ptr<DMObject>> m_children;
	// m_parent = std::unique_ptr<DMObject>( nullptr );
	//bool m_finally_destroy;	
};

