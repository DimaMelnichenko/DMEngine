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

	void addShader( uint32_t id, float range );

	uint32_t shader( float );

private:
	std::unordered_map<float, uint32_t> m_shaders;
};

}