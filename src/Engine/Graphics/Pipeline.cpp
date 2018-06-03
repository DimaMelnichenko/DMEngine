#include "Pipeline.h"

GS::Pipeline& pipeline()
{
	static GS::Pipeline pipeline;

	return pipeline;
}

namespace GS
{

Pipeline::Pipeline()
{
	
}

Pipeline::~Pipeline()
{

}

void Pipeline::init()
{
	if( m_init )
		return;

	m_shaderConstant.initBuffers();
}

ConstantBuffers& Pipeline::shaderConstant()
{
	return m_shaderConstant;
}

}