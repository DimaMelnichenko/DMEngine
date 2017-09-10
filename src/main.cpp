
#include "DMSystem.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pScmdline, int iCmdshow )
{
	// Create the system object.	test 2
	DMSystem system;
	// Initialize and run the system object.
	bool result = system.Initialize();
	if( result )
	{
		system.Run( );
	}
	return 0;
}