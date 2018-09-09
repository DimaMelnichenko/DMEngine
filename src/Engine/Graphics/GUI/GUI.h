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

	XMFLOAT4 colorGrass;
	XMFLOAT4 ambientGrass;

private:
	void clearAfterRender();
	void grassParam();

private:
	std::vector<std::pair<std::string,float>> m_counterInfoList;
	
};

