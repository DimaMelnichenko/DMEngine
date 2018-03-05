#include "DMResource.h"



DMResource::DMResource( uint32_t id, const std::string& name ) : m_id(id), m_name(name)
{
}


DMResource::~DMResource()
{
}


const std::string& DMResource::name()
{
	return m_name;
}

uint32_t DMResource::id()
{
	return m_id;
}