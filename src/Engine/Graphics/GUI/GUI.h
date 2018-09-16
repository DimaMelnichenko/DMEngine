#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "DirectX.h"



class GUI
{
public:
	GUI();
	~GUI();

	void Initialize( HWND hwnd );
	void Frame();
	void Render();
	void addCounterInfo( const std::string&, float );

private:
	void clearAfterRender();
	void renderTextureLibrary();
	void renderMaterialLibrary();
	void renderSceneObject();

private:
	std::vector<std::pair<std::string,float>> m_counterInfoList;
	bool m_isInited = false;
	
};

