#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Properties/PropertyContainer.h"



class GUI
{
public:
	GUI();
	~GUI();

	void Initialize( HWND hwnd );
	void Begin();
	void End();
	void addCounterInfo( const std::string&, float );
	// Кадр без интерфейса: собранные за кадр счётчики отбрасываются, как после отрисовки
	void skipFrame();
	void printCamera( DMCamera& camera );

	void addPropertyWatching( PropertyContainer* propertyContainer );
	// Кнопка над деревом свойств в окне «Scene Objects»: по нажатию — action
	void addAction( const std::string& label, std::function<void()> action );

private:
	void Frame();
	void clearAfterRender();
	void renderTextureLibrary();
	void renderSceneObject();

	void showPropertiesTree();
	void parsePropertiesTree( PropertyContainer* propertyContainer );

private:
	void parsePropertiesAndCreateControls( PropertyContainer* propertyContainer );

private:
	std::vector<std::pair<std::string,float>> m_counterInfoList;
	std::map<std::string, PropertyContainer*> m_propertiesMap;
	std::vector<std::pair<std::string, std::function<void()>>> m_actions;
	bool m_isInited = false;
	
};

