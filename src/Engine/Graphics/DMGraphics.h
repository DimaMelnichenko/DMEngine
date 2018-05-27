#pragma once


//////////////
// INCLUDES //
//////////////
#include <windows.h>
#include <map>
#include <unordered_map>
#include <memory>

///////////////////////
// MY  INCLUDES //
///////////////////////

#include "Utils\DMTimer.h"
#include "Utils\dmcpu.h"
#include "Utils\dmfps.h"

#include "System.h"

#include "Scene\VertexPool.h"
#include "Config\Config.h"
#include "Shaders\ConstantBuffers.h"
#include "Light\DMLightDriver.h"
#include "D3D\DMSamplerState.h"
#include "Camera\CameraController.h"


namespace GS
{

/////////////
// GLOBALS //
/////////////

class DMGraphics
{
public:
	DMGraphics();
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND, Config );

	bool Frame();


	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:
	bool Render();

	bool renderSky();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	std::unordered_map<std::string, DMCamera> m_cameraPool;
	VertexPool m_vertexPool;
	
	DMTimer m_timer;
	Config m_config;
	ConstantBuffers m_shaderConstant;
	DMLightDriver m_lightDriver;
	DMSamplerState m_samplerState;

	using RenderQueue = std::vector<const DMModel::LodBlock*>;
	std::unordered_map<uint32_t, RenderQueue> m_renderQueues;

	CameraController m_cameraController;
};

}