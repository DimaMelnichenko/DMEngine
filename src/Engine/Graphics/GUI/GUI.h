#pragma once
#include <Windows.h>
#include <functional>
#include <string>
#include <map>
#include <vector>
#include "DirectX.h"
#include "Camera\DMCamera.h"
#include "Properties/PropertyContainer.h"
#include "FrameStats.h"



class GUI
{
public:
	GUI();
	~GUI();

	void Initialize( HWND hwnd );
	// Окна кадра; stats — счётчики окна «Statistic»
	void Begin( const GS::FrameStats& stats );
	void End();
	void printCamera( DMCamera& camera );

	void addPropertyWatching( PropertyContainer* propertyContainer );
	// Окна свойств в порядке addPropertyWatching — для команд list / get / set удалённого управления
	const std::vector<PropertyContainer*>& propertyContainers() const { return m_properties; }
	// Кнопка над деревом свойств в окне «Scene Objects»: по нажатию — action
	void addAction( const std::string& label, std::function<void()> action );

private:
	void Frame( const GS::FrameStats& stats );
	void renderTextureLibrary();
	void renderSceneObject();

	void showPropertiesTree();
	void parsePropertiesTree( PropertyContainer* propertyContainer );

private:
	void parsePropertiesAndCreateControls( PropertyContainer* propertyContainer );

private:
	std::vector<PropertyContainer*> m_properties;	// окна свойств в порядке addPropertyWatching
	std::vector<std::pair<std::string, std::function<void()>>> m_actions;
	bool m_isInited = false;
	
};

