#pragma once

#include "DMResource.h"
#include <unordered_map>

namespace GS
{

class Material : public DMResource
{
public:
	Material( uint32_t, const std::string& );
	~Material();
	
	uint32_t m_shader;
	
	std::list<uint32_t> m_textureList;
};

}