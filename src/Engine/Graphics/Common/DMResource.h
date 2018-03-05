#pragma once

#include <stdint.h>
#include <string>

class DMResource
{
public:
	DMResource(uint32_t id, const std::string& name);
	~DMResource();

	const std::string& name();
	uint32_t id();

private:
	uint32_t m_id;
	std::string m_name;
};

