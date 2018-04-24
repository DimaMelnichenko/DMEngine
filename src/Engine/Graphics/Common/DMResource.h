#pragma once

#include <stdint.h>
#include <string>

namespace GS
{

class DMResource
{
public:
	DMResource( uint32_t id, const std::string& name = "" );
	DMResource( DMResource&& );
	DMResource& operator=( DMResource&& );
	virtual ~DMResource();


	DMResource( const DMResource& ) = delete;
	DMResource& operator=( const DMResource& ) = delete;

	const std::string& name();
	uint32_t id();

private:
	uint32_t m_id = 0;
	std::string m_name = "";
};

}