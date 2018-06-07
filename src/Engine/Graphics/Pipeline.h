#pragma once

#include "Shaders\ConstantBuffers.h"

namespace GS
{

class Pipeline
{
public:
	Pipeline();
	~Pipeline();

	void init();

	ConstantBuffers& shaderConstant();

private:
	ConstantBuffers m_shaderConstant;
	bool m_init = false;
};

}

GS::Pipeline& pipeline();
void pipelineDestroy();