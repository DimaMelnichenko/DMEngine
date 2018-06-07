#include "Pipeline.h"

namespace
{
static std::unique_ptr<GS::Pipeline> g_pipeline;
}

GS::Pipeline& pipeline()
{
	if( !g_pipeline )
		g_pipeline.reset( new GS::Pipeline() );

	return *g_pipeline;
}

void pipelineDestroy()
{
	g_pipeline.reset();
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