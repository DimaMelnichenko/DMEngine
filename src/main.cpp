
#include "DMSystem.h"
#include "D3D\DMD3D.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pScmdline, int iCmdshow )
{
	bool result = false;
	{
		DMSystem system;
		result = system.Initialize( pScmdline );
		if( result )
			system.Run();
	}
	// Устройство D3D12 — после всех объектов с ресурсами GPU (сцена, рендерер, хранилища): память отдаёт аллокатор,
	// который живёт в DMD3D
	DMD3D::destroy();
	// Неудачная инициализация — код 1: скрипты (Tools/engine.py) отличают её от штатного выхода
	return result ? 0 : 1;
}