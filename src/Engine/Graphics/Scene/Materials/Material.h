#pragma once

#include "Storage\DMResource.h"
#include <unordered_map>
#include "Shaders/DMShader.h"


namespace GS
{

class Material : public DMResource
{
public:
	Material( uint32_t id, const std::string& name );
	Material& operator=( Material&& );
	Material( Material&& );
	~Material();
	std::unique_ptr<DMShader> m_shader;
	PropertyContainer m_parameters;
};

}