#pragma once

#include "DMResource.h"
#include <unordered_map>

class DMMaterial : public DMResource
{
public:
	DMMaterial( uint32_t, const std::string& );
	~DMMaterial();

	void addShader( uint32_t id, float range );

	uint32_t shader( float );

private:
	std::unordered_map<float, uint32_t> m_shaders;
};

