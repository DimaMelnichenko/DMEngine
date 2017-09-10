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

#include "Scene\DMQuadTree.h"
#include "Scene\QuadTreeClass.h"

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
#include "Scene\Shaders\DMMultitextureShader.h"
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

class DMGraphics : public DMObject
{
public:
	DMGraphics( );
	~DMGraphics();

	bool Initialize( HINSTANCE hinstance, int, int, HWND );
	void Shutdown( );

	bool Frame();
	

	LRESULT HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam );

private:	
	bool HandleInput( float frameTime );
	void RenderToTexture();
	void RenderShadowSun();
	void RenderShadowLights();
	void RenderGUI();
	void RenderBackground( DMCamera* );
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

	std::unique_ptr<DMInput> m_Input;
	std::unique_ptr<DMD3D> m_dmd3d;
	std::unique_ptr<DMCamera> m_Camera;
	std::unique_ptr<DMCamera> m_CameraOrtho;
	std::unique_ptr<DMCamera> m_CameraOrthoText;
	std::unique_ptr<DMCamera> m_CameraGrass;	
	std::vector<std::unique_ptr<DMCamera>> m_cameras_shadow;
	std::vector<std::unique_ptr<DMRenderTexture>> m_shadow_depth;
	
	std::unique_ptr<DMTimer> m_Timer;
	std::unique_ptr<DMPosition> m_Position;
	std::unique_ptr<DMFps> m_Fps;
	std::unique_ptr<DMCpu> m_Cpu;
	std::unique_ptr<DMFontShader> m_FontShader;
	std::unique_ptr<DMText> m_Text;

	std::unique_ptr<DMTextureShader> m_TextureShader;
	std::unique_ptr<DMTextureShader> m_TextureInstanceShader;
	std::unique_ptr<DMMiniMap> m_MiniMap;

	std::unique_ptr<DMBitmap> m_BlendingBitmap;
	std::unique_ptr<DMTexture> m_FirstBlendTexture;
	std::unique_ptr<DMTexture> m_SecondBlendTexture;
	std::unique_ptr<DMTexture> m_AlphaBlendTexture;
	std::unique_ptr<DMMultitextureShader> m_MultitextureShader;


	std::unique_ptr<DMDebugWindow> m_DebugWindow;

	
	std::unique_ptr<DMTextureArray> m_TerrainTexturesArray;
	std::unique_ptr<DMTextureArray> m_OceanTexturesArray;
	std::unique_ptr<DMFrustum> m_Frustum;
	std::unique_ptr<DMQuadTree> m_object_quad_tree;

	std::map<std::string, std::unique_ptr<DMShader>> m_shader_list;


	std::unique_ptr<QuadTreeClass> m_QuadTreeClass;
	std::unique_ptr<TerrainClass> m_TerrainClass;
	//std::unique_ptr<TerrainShaderClass> m_TerrainShaderClass;

	std::unique_ptr<DMTexture> m_TextureHeight;	
	std::unique_ptr<DMBitmap> m_HeightTextureDebug;
	std::unique_ptr<DMTextureShader> m_HeightToNormalShader;

	float m_time_counter;
	
	std::unique_ptr<DMGeoClipMap> m_geoClipMapWater;
	

	std::unique_ptr<DMOcean> m_Ocean;
	std::unique_ptr<DMOceanShader> m_oceanShader;	
	
	std::unique_ptr<DMTextureShader> m_OceanSurface;
	
	std::unique_ptr<DMNormalmapShader> m_normalmap_shader;
	std::unique_ptr<DMTextureArray> m_normalmap_textures;
	
	std::vector<std::unique_ptr<DMSceneObject>> m_grass_buffer;
	
	std::vector<D3DXVECTOR4> m_grass_buffer_visible;

	std::unique_ptr<DMGrassShader> m_grass_shader;

	std::unordered_map<std::string, DMModel* > m_model_buffer;

	std::unique_ptr<DMLightDriver> m_light_driver;

	std::unique_ptr<DMLightShader> m_light_shader;

	std::unique_ptr<DMDrawContainer> m_draw_container;

	std::unique_ptr<DMColorShader> m_color_shader;

	std::unique_ptr<DMModel> m_grass_model;
	//DMQuadTree> m_quad_tree_object;

	std::unique_ptr<DMAdvectionShader> m_advection_shader;
	std::vector<std::unique_ptr<DMRenderTexture>> m_advection_render_texture;

	std::unique_ptr<DMParticleSystem> m_particle_system;
	std::unique_ptr<DMParticleShader> m_particle_shader;

	std::unique_ptr<DMFullScreen> m_full_screen;
	std::unique_ptr<DMFullScreenShader> m_full_screen_shader;

	std::unique_ptr<DMTerrain> m_terrain;
	std::unique_ptr<DMTerrain> m_terrain_low_res;
	std::unique_ptr<DMClipMapShader> m_clipMapShader;

	std::unique_ptr<DMComputeShader> m_grass_cs;

	std::unique_ptr<DMGrass> m_grass;

	DMLight* m_Sun;

	std::unique_ptr<DMModel> m_sky_sphere;

	std::unique_ptr<DMRenderTexture>	m_render_texture;
	std::unique_ptr<DMRenderTexture>	m_render_texture2;
	std::unique_ptr<DMDepthShader>		m_depth_shader;
	std::unique_ptr<DMBitmap>			m_depth_output;
	//std::unique_ptr<DMInstance<D3DXVECTOR3>> m_box_instance;
	std::unique_ptr<DMRenderFilter>		m_render_filter;
	std::unique_ptr<DMRenderCubeTexture> m_render_cube_texture;
	std::unique_ptr<DMHDR> m_hdr;
	std::unordered_map<std::wstring,std::unique_ptr<DMRenderQueue>> m_render_queues;
	std::unique_ptr<DMSamplerState> m_sampler_state;
	std::unique_ptr<DMTexturePool> m_texture_pool;
	std::unique_ptr<DMInstance> m_box_instance;
	std::unique_ptr<DMInstance> m_sphere_instance;
	std::unordered_map<std::wstring, std::unique_ptr<DMModel>> m_models;

};


