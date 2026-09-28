#include "KeyEventNotifier.h"

KeyEventNotifier::KeyEventNotifier()
{

}


KeyEventNotifier::~KeyEventNotifier()
{

}

void KeyEventNotifier::process( uint8_t* keyboardState )
{
	for( auto& statePair : m_processorMap )
	{
		State& state = statePair.second;
		if( ( keyboardState[statePair.first] & 0x80 ) && state.keyUp )
		{
			state.keyUp = false;
			state.triggered = !state.triggered;

			for( auto& func : state.delegateList )
			{
				func( state.triggered );
			}
		}

		if( !( keyboardState[statePair.first] & 0x80 ) && !state.keyUp )
		{
			state.keyUp = true;
		}
	}
}

bool KeyEventNotifier::press( uint8_t key )
{
	auto it = m_processorMap.find( key );
	if( it == m_processorMap.end() )
		return false;
	State& state = it->second;
	state.triggered = !state.triggered;
	for( auto& func : state.delegateList )
		func( state.triggered );
	return true;
}

void KeyEventNotifier::registerTrigger( uint8_t key, std::function<void( bool )> func )
{
	if( !m_processorMap.count( key ) )
	{
		m_processorMap[key].keyUp = false;
		m_processorMap[key].triggered = false;
	}

	m_processorMap[key].delegateList.push_back( func );
}