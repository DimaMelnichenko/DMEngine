
#include "DMSystem.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pScmdline, int iCmdshow )
{
	// Create the system object.	test 2
	DMSystem system;
	// Initialize and run the system object.
	bool result = system.Initialize( pScmdline );
	if( result )
	{
		system.Run( );
	}
	// Неудачная инициализация — код 1: скрипты (Tools/engine.py) отличают её от штатного выхода
	return result ? 0 : 1;
}