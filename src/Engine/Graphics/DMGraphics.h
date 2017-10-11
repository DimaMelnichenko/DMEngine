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

const int SHADOWMAP_WIDTH = 1024;
const int SHADOWMAP_HEIGHT = 1024;

class DMGraphics
{
public:
	DMGraphics( );
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND );

	bool Frame();
	

	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:	
	bool HandleInput( float frameTime );
	void RenderToTexture();
	void RenderShadowSun();
	void RenderShadowLights();
	void RenderGUI();
	void RenderBackground( const DMCamera& );
	void RenderEnvironment();
	bool InitShader();
	bool InitModels();
	bool InitLights();
	bool InitCameras();
	bool Render();

private:
	bool m_showWireframe;
	HWND m_hwnd;
	float m_screenWidth;
	float m_screenHeight;

	DMInput m_Input;
	std::unordered_map<std::string, DMCamera> m_cameraPool;
	std::vector<DMCamera> m_cameras_shadow;
	std::vector<DMRenderTexture> m_shadow_depth;
	
	DMTimer m_Timer;
	DMPosition m_Position;
	DMFps m_Fps;
	DMCpu m_Cpu;
	DMFontShader m_FontShader;
	DMText m_Text;

	DMTextureShader m_TextureShader;
	DMTextureShader m_TextureInstanceShader;
	DMMiniMap m_MiniMap;

	
	DMDebugWindow m_DebugWindow;
	
	
	DMFrustum m_Frustum;
	DMQuadTree m_object_quad_tree;

	std::map<std::string, DMShader*> m_shader_list;

	TerrainClass m_TerrainClass;
	//std::unique_ptr<TerrainShaderClass> m_TerrainShaderClass;

	DMTexture m_TextureHeight;	
	DMBitmap m_HeightTextureDebug;
	DMTextureShader m_HeightToNormalShader;

	float m_time_counter;

	//DMNormalmapShader m_normalmap_shader;
	DMTextureArray m_normalmap_textures;
	
	std::vector<DMSceneObject> m_grass_buffer;
	std::vector<D3DXVECTOR4> m_grass_buffer_visible;
	DMGrassShader m_grass_shader;

	std::unordered_map<std::string, DMModel* > m_model_buffer;

	DMLightDriver m_light_driver;
	DMLightShader m_light_shader;
	DMDrawContainer m_draw_container;
	DMColorShader m_color_shader;
	DMModel m_grass_model;
	//DMQuadTree> m_quad_tree_object;

	//DMAdvectionShader m_advection_shader;
	//std::vector<DMRenderTexture> m_advection_render_texture;

	//DMParticleSystem m_particle_system;
	//DMParticleShader m_particle_shader;

	DMFullScreen m_full_screen;
	DMFullScreenShader m_full_screen_shader;

	DMTerrain m_terrain;
	DMTerrain m_terrain_low_res;
	DMClipMapShader m_clipMapShader;

	DMComputeShader m_grass_cs;

	DMGrass m_grass;

	DMLight* m_Sun;

	DMModel m_sky_sphere;

	DMRenderTexture	m_render_texture;
	DMRenderTexture	m_render_texture2;
	DMDepthShader		m_depth_shader;
	DMBitmap			m_depth_output;
	//std::unique_ptr<DMInstance<D3DXVECTOR3>> m_box_instance;
	DMRenderFilter		m_render_filter;
	DMRenderCubeTexture m_render_cube_texture;
	DMHDR m_hdr;
	std::unordered_map<std::wstring, DMRenderQueue> m_render_queues;
	DMSamplerState m_sampler_state;
	DMInstance m_box_instance;
	DMInstance m_sphere_instance;
	std::unordered_map<std::wstring, DMModel> m_models;
};


