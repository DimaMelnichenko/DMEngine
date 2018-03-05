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

#include "..\..\Utils\DMTimer.h"
#include "..\..\Utils\dmcpu.h"
#include "..\..\Utils\dmfps.h"
#include "..\Input\DMInput.h"

#include "D3D\DMD3D.h"
#include "D3D\DMSamplerState.h"
#include "D3D\DMDebugWindow.h"
#include "D3D\DMStructuredBuffer.h"

#include "Scene\DMQuadTree.h"

#include "Scene\Camera\DMCamera.h"
#include "Scene\Camera\DMPosition.h"
#include "Scene\Camera\DMFrustum.h"

#include "Text\DMFontShader.h"
#include "Text\DMText.h"

#include "Scene\Terrain\DMTerrain.h"
#include "Scene\Terrain\DMTerrainShader.h"
#include "Scene\Terrain\TerrainClass.h"
#include "Scene\Terrain\DMGeoClipMap.h"
#include "Scene\Terrain\DMClipMapShader.h"
#include "Scene\Terrain\DMHeightMap.h"
#include "Scene\Terrain\Map\DMMiniMap.h"

#include "Scene\Light\DMLight.h"
#include "Scene\Light\DMLightShader.h"
#include "Scene\Light\DMLightDriver.h"

#include "Scene\Shaders\DMColorShader.h"
#include "Scene\Shaders\DMTextureShader.h"
#include "Scene\Shaders\DMDepthShader.h"
#include "Scene\Shaders\DMReflectionShader.h"
#include "Scene\Shaders\DMNormalmapShader.h"
#include "Scene\Shaders\DMComputeShader.h"
#include "Scene\Shaders\DMHDR.h"

#include "Scene\Sky\DMSkyDomeShader.h"
#include "Scene\Sky\DMSkyPlane.h"
#include "Scene\Sky\DMSkyPlaneShader.h"

#include "Scene\Model\DMModel.h"

#include "Scene\TextureObjects\DMRenderTexture.h"
#include "Scene\TextureObjects\DMFullScreen.h"
#include "Scene\TextureObjects\DMFullScreenShader.h"
#include "Scene\TextureObjects\DMRenderFilter.h"
#include "Scene\TextureObjects\DMRenderCubeTexture.h"

#include "Scene\Water\DMWater.h"
#include "Scene\Water\DMWaterShader.h"
#include "Scene\Water\DMOcean.h"
#include "Scene\Water\DMOceanShader.h"
#include "Scene\Water\DMAdvectionShader.h"

#include "Scene\Model\DMRenderQueue.h"
#include "Scene\Model\DMDrawContainer.h"
#include "Scene\Model\Grass\DMGrass.h"
#include "Scene\Model\Grass\DMGrassShader.h"
#include "Scene\Model\Instance\DMInstance.h"

#include "Scene\Particle\DMParticleSystem.h"
#include "Scene\Particle\DMParticleShader.h"


/////////////
// GLOBALS //
/////////////
const bool FULL_SCREEN = false;
const bool VSYNC_ENABLED = false;
const float SCREEN_DEPTH = 2000.0f;
const float SCREEN_NEAR = 1.0f;

class DMGraphics
{
public:
	DMGraphics( );
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND );

	bool Frame();
	

	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:	
	bool Render();

private:
	float m_screenWidth;
	float m_screenHeight;
	HWND m_hwnd;

	DMInput m_Input;
	std::unordered_map<std::string, DMCamera> m_cameraPool;
};


