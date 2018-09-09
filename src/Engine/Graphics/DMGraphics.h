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
#include "Light\DMLightDriver.h"
#include "D3D\DMSamplerState.h"
#include "Camera\CameraController.h"
#include "Terrain\DMTerrain.h"
#include "ObjectLibrary\LibraryLoader.h"
#include "GUI\GUI.h"


#include "Particle\DMParticleSystem.h"
#include "Scene\Model\Grass\DMGrass.h"

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
	void ComputePass();
	void preparePipeline();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	std::unordered_map<std::string, DMCamera> m_cameraPool;
	VertexPool m_vertexPool;
	
	DMTimer m_timer;
	Config m_config;
	DMLightDriver m_lightDriver;
	DMSamplerState m_samplerState;

	using RenderQueue = std::vector<const DMModel::LodBlock*>;
	std::unordered_map<uint32_t, RenderQueue> m_renderQueues;

	CameraController m_cameraController;
	DMTerrain m_terrain;
	DMTerrain m_water;
	std::unordered_map<std::string, bool> m_visible;
	LibraryLoader m_library;

private:
	DMParticleSystem m_particleSystem;
	DMGrass m_grass;
	GUI m_GUI;
	bool m_cursorMode = false;
};

}