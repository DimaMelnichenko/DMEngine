#include "SlowTask.h"
#include <algorithm>

std::vector<SlowTask*> SlowTask::s_stack;
SlowTask::Handler SlowTask::s_handler;

void SlowTask::setHandler( Handler handler )
{
	s_handler = std::move( handler );
}

SlowTask::SlowTask( float totalWork, std::string message ) :
	m_totalWork( std::max( totalWork, 1e-6f ) ),
	m_message( std::move( message ) )
{
	// Без показа: кадр загрузки — только в enterProgressFrame, на границе этапа, которую код отметил сам
	s_stack.push_back( this );
}

SlowTask::~SlowTask()
{
	// Задачи — в стеке по вложенности: снимается своя (и вложенные, если их забыли закрыть)
	auto found = std::find( s_stack.begin(), s_stack.end(), this );
	if( found != s_stack.end() )
		s_stack.erase( found, s_stack.end() );
}

void SlowTask::enterProgressFrame( float amount, std::string message )
{
	m_completedWork = std::min( m_completedWork + m_frameWork, m_totalWork );
	m_frameWork = std::max( amount, 0.0f );
	m_frameMessage = std::move( message );
	report();
}

void SlowTask::report()
{
	if( !s_handler )
		return;
	// Доля всей работы: сделанное каждой задачи в масштабе текущей части внешней
	float progress = 0.0f;
	float scale = 1.0f;
	std::string message;
	for( const SlowTask* task : s_stack )
	{
		progress += scale * task->m_completedWork / task->m_totalWork;
		scale *= std::min( task->m_frameWork, task->m_totalWork - task->m_completedWork ) / task->m_totalWork;
		const std::string& text = task->m_frameMessage.empty() ? task->m_message : task->m_frameMessage;
		if( text.empty() || ( !message.empty() && message.size() >= text.size() &&
							  message.compare( message.size() - text.size(), text.size(), text ) == 0 ) )
			continue;
		message += ( message.empty() ? "" : " — " ) + text;
	}
	s_handler( message, std::clamp( progress, 0.0f, 1.0f ) );
}
