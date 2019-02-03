#pragma once
#include <Windows.h>
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
	void printCamera( DMCamera& camera );

	void addPropertyWatching( PropertyContainer* propertyContainer );

private:
	void Frame();
	void clearAfterRender();
	void renderTextureLibrary();
	void renderMaterialLibrary();
	void renderSceneObject();

	void showPropertiesTree();
	void parsePropertiesTree( PropertyContainer* propertyContainer );

private:
	void parsePropertiesAndCreateControls( PropertyContainer* propertyContainer );

private:
	std::vector<std::pair<std::string,float>> m_counterInfoList;
	std::map<std::string, PropertyContainer*> m_propertiesMap;
	bool m_isInited = false;
	
};

