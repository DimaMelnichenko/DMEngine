#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include "DirectX.h"
#include "Camera\DMCamera.h"



class GUI
{
public:
	GUI();
	~GUI();

	void Initialize( HWND hwnd );
	void Begin();
	void End();
	void addCounterInfo( const std::string&, float );
	void printCamera( const DMCamera& camera );

	void terrainTessFactor( std::vector<float>* tessFactor );

private:
	void Frame();
	void clearAfterRender();
	void renderTextureLibrary();
	void renderMaterialLibrary();
	void renderSceneObject();

	void terrainSettings();

private:
	void materialParameterKind();

private:
	std::vector<std::pair<std::string,float>> m_counterInfoList;
	bool m_isInited = false;
	std::vector<float>* m_terrainTessFactor;
};

