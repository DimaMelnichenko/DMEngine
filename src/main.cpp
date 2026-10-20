
#include "DMSystem.h"
#include "D3D\DMD3D.h"

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR pScmdline, int iCmdshow )
{
	// COM главного потока — до всего (WIC DirectXTex: загрузка PNG и JPG, снимки). Без этого главный поток пользовался
	// COM неявно — в MTA потока звука; WIC-фабрика, созданная при загрузке картинок (промах кэша текстур), к снимку
	// в том же запуске роняла процесс без записи об ошибке
	const HRESULT com = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
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
	if( SUCCEEDED( com ) )
		CoUninitialize();
	// Неудачная инициализация — код 1: скрипты (Tools/engine.py) отличают её от штатного выхода
	return result ? 0 : 1;
}