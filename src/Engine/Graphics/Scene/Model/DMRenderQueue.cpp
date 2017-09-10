#include "DMRenderQueue.h"
#include "DMModel.h"


DMRenderQueue::DMRenderQueue()
{
}


DMRenderQueue::~DMRenderQueue()
{
}

void DMRenderQueue::append( DMModel* model )
{
	m_queue.push_back( model );
}

void DMRenderQueue::clear()
{
	m_queue.clear();
}

std::vector<DMModel*>& DMRenderQueue::queue()
{
	return m_queue;
}

bool DMRenderQueue::inititlise( DMShader* shader, int phase )
{

	m_shader = shader;
	m_phase = phase;

	return (shader != nullptr);
}

DMShader* DMRenderQueue::shader()
{
	return m_shader;
}

int DMRenderQueue::phase()
{
	return m_phase;
}