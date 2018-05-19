#pragma once
#include <functional>
#include <unordered_map>

#include <dinput.h>

class KeyEventNotifier
{
public:
	KeyEventNotifier();
	~KeyEventNotifier();

	void process( uint8_t* );

	void registerTrigger( uint8_t key, std::function<void(bool)> func );

private:
	struct State
	{
		bool keyUp;
		bool triggered;
		std::list<std::function<void( bool )>> delegateList;
	};

	std::unordered_map<uint8_t, State> m_processorMap;
};