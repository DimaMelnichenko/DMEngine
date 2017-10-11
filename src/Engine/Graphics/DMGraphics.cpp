#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>

DMGraphics::DMGraphics(  )
{
	m_showWireframe = false;
}

DMGraphics::~DMGraphics()
{
	DMD3D::close();
	DMTexturePool::close();
}

bool DMGraphics::Initialize( HINSTANCE hinstance, int screenWidth, int screenHeight, HWND hwnd )
{	
	bool result = true;
	char videoCard[128];
	int videoMemory;
	
	m_hwnd = hwnd;
	
	m_screenWidth = static_cast<float>( screenWidth );
	m_screenHeight = static_cast<float>( screenHeight );

	// Initialize the Direct3D object.
	result = DMD3D::instance().Initialize( screenWidth, screenHeight, VSYNC_ENABLED, hwnd, FULL_SCREEN, SCREEN_DEPTH, SCREEN_NEAR );
	if( !result )
	{
		MessageBox( hwnd, L"Could not initialize DirectX 11.", L"Error", MB_OK );
		return false;
	}

	if( !DMTexturePool::instance().initialize( L"textures\\no_image.dds" ) )
	{
		::MessageBox( 0, L"No found default texture: textures\\no_image.dds ", L"Error", MB_OK );
		return false;
	}

	result = m_Input.Initialize( hinstance, hwnd, screenWidth, screenHeight );
	if( !result )
	{
		MessageBox( hwnd, L"Could not initialize the input object.", L"Error", MB_OK );
		return false;
	}
	
	if( !InitCameras() )
	{
		MessageBox( hwnd, L"Could not initialize Cameras.", L"Error", MB_OK );
		return false;
	}

	// Initialize the timer object.
	result = m_Timer.Initialize();
	if( !result )
	{
		MessageBox( hwnd, L"Could not initialize the timer object.", L"Error", MB_OK );
		return false;
	}

	// Initialize the fps object.
	m_Fps.Initialize();

	// Initialize the cpu object.
	m_Cpu.Initialize();

	// Initialize the font shader object.
	result = m_FontShader.Initialize(  );
	if( !result )
	{
		MessageBox( hwnd, L"Could not initialize the font shader object.", L"Error", MB_OK );
		return false;
	}
	
	// Initialize the text object.
	result = m_Text.Initialize( screenWidth, screenHeight );
	if( !result )
	{
		MessageBox( hwnd, L"Could not initialize the text object.", L"Error", MB_OK );
		return false;
	}

	// Retrieve the video card information.
	DMD3D::instance().GetVideoCardInfo( videoCard, videoMemory );

	// Set the video card information in the text object.
	result = m_Text.SetVideoCardInfo( videoCard, videoMemory );
	if( !result )
	{
		MessageBox( hwnd, L"Could not set video card info in the text object.", L"Error", MB_OK );
		return false;
	}

	m_DebugWindow.Initialize( DMD3D::instance().GetDevice(), 10, 10, 500, 500 );


	/*
	for( size_t i = 0; i < 3; i++ )
	{
		DMRenderTexture* render_texture = std::unique_ptr< DMRenderTexture( m_dmd3d.get() );
		render_texture->Initialize( DXGI_FORMAT_R16G16B16A16_UNORM, 1024, 1024 );
		m_advection_render_texture.push_back( render_texture );
	}
	*/

	
	m_render_texture.Initialize( DXGI_FORMAT_R32G32_FLOAT, 1024, 1024 );

	m_render_texture2.Initialize( DXGI_FORMAT_R32G32_FLOAT, 1024, 1024 );

	/////////////////////////////////////////////////////////
	// shadow parameters

	for( size_t i = 0; i < 1; i++ )
	{
		DMRenderTexture rt_depth;
		rt_depth.Initialize( DXGI_FORMAT_R32_FLOAT, 512, 512 );

		m_shadow_depth.emplace_back( std::move( rt_depth ) );
	}

	m_depth_output.Initialize( screenWidth, screenHeight, 300, 300 );

	m_render_cube_texture.Initialize( DXGI_FORMAT_R32G32B32A32_FLOAT, 512, 512 );

	////////////////////////////////////////////////////////

	if( !m_sampler_state.initialize() )
		return false;

	/////////////////////////////////////////////////////////

	if( !InitLights() )
		return false;
	
	if( !InitShader() )
		return false;
	
	
	m_hdr.Initialize( 1024, 1024, 2.0f, 2.0f );

	//m_draw_container = std::unique_ptr<DMDrawContainer>( new DMDrawContainer( m_light_shader.get() ) );
	
	
	m_object_quad_tree.Initialize( 1024.0f, 1024.0f );	
	//m_quad_tree_object = std::unique_ptr< DMQuadTree( m_dmd3d.get() );
	//m_quad_tree_object->Initialize( 1024.0f, 32.0f );
		
	if( !InitModels() )
		return false;
	

	return true;
}

bool DMGraphics::InitCameras()
{
	// Create the main camera object.
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 1.0f, 10000.0f );
	float camera_x = 0.0f;
	float camera_y = 70.0f;
	float camera_z = -10.0f;
	m_cameraPool["main"].SetPosition( camera_x, camera_y, camera_z );

	// Set the initial position of the viewer to the same as the initial camera position.
	m_Position.SetPosition( camera_x, camera_y, camera_z );


	m_cameraPool["ortho"].Initialize( DMCamera::CT_ORTHO, 1024, 1024, 0.01f, 10.0f );
	m_cameraPool["ortho"].SetPosition( 0.0f, 0.0f, -1.0f );
	m_cameraPool["ortho"].Render();

	m_cameraPool["text"].Initialize( DMCamera::CT_ORTHO, m_screenWidth, m_screenHeight, 0.01f, 10.0f );
	m_cameraPool["text"].SetPosition( 0.0f, 0.0f, -5.0f );
	m_cameraPool["text"].Render();


	m_cameraPool["grass"].Initialize( DMCamera::CT_ORTHO, 1024, 1024, 1.0, 1600.0 );

	for( size_t i = 0; i < 8; i++ )
	{
		DMCamera camera;
		camera.Initialize( DMCamera::CT_ORTHO, 10.0f * ( i + 1.5f ), 10.0f * ( i + 1.5f ), 1.0f, 300.0f );
		m_cameras_shadow.push_back( std::move( camera ) );
	}

	return true;
}

bool DMGraphics::InitLights()
{	
	m_light_driver.Initialize();

	// Create the light object.
	DMLight light( DMLight::LightType::Dir );
	//m_Sun = light;
	light.setColor( 1.0f, 1.0f, 0.98f );
	light.attenuation = 100000.0f;
	light.setPosition( 100.00, 100.0, -100.00 );

	m_light_driver.addLight( std::move( light ) );

	light = DMLight( DMLight::Point );
	light.setColor( 5.0f, 4.8f, 4.0f );
	light.attenuation = 10400.0f;
	light.setPosition( 600.0f, 164.0f, 600.0f );
	//m_light_driver.addLight( std::move( light ) );
	
	/*for( int i = 0; i < 3; ++i )
		for( int j = 0; j < 3; ++j )
		{
			light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Point ) );
			light.setColor( 5.0f, 4.8f, 4.0f );
			light.attenuation = 400.0f;
			light.setPosition( i * 20.0f + 2.0f, 1.0f, j * 20.0f + 2.0f );
			m_light_driver.addLight( std::move( light ) );
		}

	*/


	/*
	light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Spot ) );
	light.setColor( 25.0f, 25.0f, 23.9f );
	light.attenuation = 300.0f;
	light.setPosition( 5.0f, 4.0f, 5.5f );
	light.setDirection( 0.0f, -1.0f, 0.00001f );
	light.setShadowParam( D3DX_PI / 1.5, 256 );
	light.setCastShadow( true );
	light.proj_texture = DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"textures\\flashlight.jpg" ) );
	m_light_driver.addLight( std::move( light ) );	

	light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Spot ) );
	light.setColor( 125.0f, 125.0f, 100.9f );
	light.attenuation = 300.0f;
	light.setPosition( 20.0, 4.0, 5.5 );
	light.setDirection( 0.0, -1.0, 0.00001 );
	light.setShadowParam( D3DX_PI / 1.5, 256 );
	light.setCastShadow( true );
	light.proj_texture = DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"textures\\flashlight.jpg" ) );
	m_light_driver.addLight( std::move( light ) );

	light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Spot ) );
	light.setColor( 125.0f, 125.0f, 100.9f );
	light.attenuation = 300.0f;
	light.setPosition( 35.0, 4.0, 5.5 );
	light.setDirection( 0.0, -1.0, 0.00001 );
	light.setShadowParam( D3DX_PI / 1.5, 256 );
	light.setCastShadow( true );
	light.proj_texture = DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"textures\\flashlight.jpg" ) );
	m_light_driver.addLight( std::move( light ) );

	light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Spot ) );
	light.setColor( 125.0f, 125.0f, 100.9f );
	light.attenuation = 300.0f;
	light.setPosition( 50.0, 4.0, 5.5 );
	light.setDirection( 0.0, -1.0, 0.00001 );
	light.setShadowParam( D3DX_PI / 1.5, 256 );
	light.setCastShadow( true );
	light.proj_texture = DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"textures\\flashlight.jpg" ) );
	m_light_driver.addLight( std::move( light ) );



	light = std::unique_ptr<DMLight>( new DMLight( m_dmd3d.get(), DMLight::Spot ) );
	light.setColor( 20.0f, 10.0f, 0.0f );
	light.attenuation = 100.0f;
	light.setPosition( 500.0, 10.0, 495.0 );
	*/

	//m_light_driver.addLight( std::move( light ) );
	
	for( size_t i = 0; i < m_shadow_depth.size(); i++ )
	{
		m_light_driver.addCSMLayer( m_cameras_shadow[i] );
	}

	return true;
}

bool DMGraphics::InitShader()
{
	// Create the texture shader object.
	if( !m_TextureShader.Initialize( L"Shaders\\textureshader.vs", L"Shaders\\textureshader.ps" ) )
		return false;
	m_TextureShader.createPhase( 0, -1, 0 );

	
	m_TextureInstanceShader.Initialize();
	if( !m_TextureInstanceShader.addShaderPass( SRVType::vs, "instance_main", L"Shaders\\textureshader.vs" ) )
		return false;
	if( !m_TextureInstanceShader.addShaderPass( SRVType::ps, "main", L"Shaders\\textureshader.ps" ) )
		return false;
	m_TextureInstanceShader.createPhase( 0, -1, 0 );

	
	if( !m_clipMapShader.Initialize( L"Shaders\\clipmap.vs" ) )
		return false;
	//if( !m_clipMapShader.addShaderPass( SRVType::ps, "main", L"Shaders\\clipmap.ps", "USE_SHADOW" ) )
	if( !m_clipMapShader.addShaderPass( SRVType::ps, "main", L"Shaders\\clipmap.ps" ) )
		return false;
	if( !m_clipMapShader.addShaderPass( SRVType::ps, "for_grass", L"Shaders\\clipmap.ps" ) )
		return false;
	if( !m_clipMapShader.addShaderPass( SRVType::ps, "for_shadow", L"Shaders\\clipmap.ps" ) )
		return false;
	m_clipMapShader.createPhase( 0, -1, 0 );
	m_clipMapShader.createPhase( 0, -1, 1 );
	m_clipMapShader.createPhase( 0, -1, -1 );

	
	if( !m_light_shader.Initialize() )
		return false;
	if( !m_light_shader.addShaderPass( SRVType::vs, "main", L"Shaders\\lightshader.vs" ) )
		return false;
	if( !m_light_shader.addShaderPass( SRVType::vs, "main", L"Shaders\\lightshader.vs", "INST_POS=1,INST_SCALE=1" ) )
		return false;
	if( !m_light_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\lightshader.ps" ) )
		return false;
	if( !m_light_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\lightshadersimple.ps" ) )
		return false;
	if( !m_light_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\PBR.ps" ) )
		return false;
	m_light_shader.createPhase( 0, -1, 0 );
	m_light_shader.createPhase( 0, -1, 1 );
	m_light_shader.createPhase( 1, -1, 0 );
	m_light_shader.createPhase( 0, -1, 2 );
	m_light_shader.createPhase( 1, -1, 2 );
	m_light_shader.setLights( &m_light_driver );

	
	m_render_queues[L"light"].inititlise( &m_light_shader, 0 );
	m_render_queues[L"light_instance"].inititlise( &m_light_shader, 4 );
	m_render_queues[L"light_PBR"].inititlise( &m_light_shader, 3 );


	
	if( !m_color_shader.Initialize( L"Shaders\\colorshader.vs", L"Shaders\\colorshader.ps" ) )
		return false;
	m_color_shader.createPhase( 0, -1, 0 );

	
	if( !m_grass_shader.Initialize() )
		return false;
	if( !m_grass_shader.addShaderPass( SRVType::vs, "generate", L"Shaders\\grass.vs" ) )
		return false;
	m_grass_shader.setStreamout( true );
	if( !m_grass_shader.addShaderPass( SRVType::gs, "generate", L"Shaders\\grass.gs" ) )
		return false;
	m_grass_shader.setStreamout( false );
	m_grass_shader.createPhase( 0, 0, -1 );
	if( !m_grass_shader.addShaderPass( SRVType::vs, "main", L"Shaders\\grass.vs" ) )
		return false;
	if( !m_grass_shader.addShaderPass( SRVType::gs, "main", L"Shaders\\grass.gs" ) )
		return false;
	if( !m_grass_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\grass.ps" ) )
		return false;
	m_grass_shader.createPhase( 1, 1, 0 );
	

	//m_advection_shader = std::unique_ptr< DMAdvectionShader( m_dmd3d.get() );
	//m_advection_shader->Initialize( L"Shaders\\textureshader.vs", L"Shaders\\advection.ps" );
	//m_advection_shader->addShaderPass( DMShader::ps, "init", L"Shaders\\advection.ps" );


	
	if( !m_full_screen_shader.Initialize( L"Shaders\\fullscreen.vs", L"Shaders\\fullscreen.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 0 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\noise.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 1 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "BrightPass", L"Shaders\\hdr.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 2 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "DownSample", L"Shaders\\hdr.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 3 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "HorizontalBlur", L"Shaders\\hdr.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 4 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "VerticalBlur", L"Shaders\\hdr.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 5 ); 
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "FinalPass", L"Shaders\\hdr.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 6 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\calcnormal.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 7 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\procedure_tex.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 8 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "main", L"Shaders\\fastwater.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 9 );
	if( !m_full_screen_shader.addShaderPass( SRVType::ps, "init", L"Shaders\\fastwater.ps" ) )
		return false;
	m_full_screen_shader.createPhase( 0, -1, 10 );

	//m_grass_cs = std::unique_ptr< DMComputeShader( m_dmd3d.get() );
	//m_grass_cs->Initialize( L"Shaders\\grass.cs", "main" );

	
	if( !m_depth_shader.Initialize( L"Shaders\\depth.vs", L"Shaders\\depth.ps" ) )
		return false;
	m_depth_shader.createPhase( 0, -1, -1 );	
	m_render_queues[L"shadow"].inititlise( &m_depth_shader, 0 );

	//Shaders\\font.vs", L"Shaders\\font.ps

	return true;
}

bool DMGraphics::InitModels()
{
	wchar_t out[500];
	memset( out, 0, sizeof( wchar_t ) * 500 );
	DWORD size;

	std::wstring level_file = L"data\\level01.ini";

	GetPrivateProfileSection( L"Models", out, 500, level_file.data() );
	//GetPrivateProfileSectionNames( out, 500, L"D:\\VS Projects\\DMEngine\\DMEngine\\data\\level01.ini" );

	
	size_t offset_start = 0;

	

	// загружаем все модели
	while( wcslen( &out[offset_start] ) > 0 )
	{
		std::wstring model_name = ( &out[offset_start] );

		DMModel model( model_name );

		int lods = GetPrivateProfileInt( model_name.data(), L"LOD", 0, level_file.data() );

		// загружаем все лоды модели
		for( size_t i = 0; i < lods; i++ )
		{
			std::wstring model_lod = L"model_lod" + std::to_wstring( i );
			wchar_t model_lod_path[1024];			
			GetPrivateProfileString( model_name.data(), model_lod.data(), L"", model_lod_path, 1024, level_file.data() );
			int dist = 0;
			model_lod = L"dist_lod" + std::to_wstring( i );
			dist = GetPrivateProfileInt( model_name.data(), model_lod.data(), 0, level_file.data() );
			if( wcslen( model_lod_path ) && dist > 0 )
			{
				if( model.countOfLOD() == 0 )
				{
					model.Initialize( dist, DMMesh::VertexCombination::V_PTNTB, model_lod_path );
				}
				else
				{
					model.addLODModel( dist, DMMesh::VertexCombination::V_PTNTB, model_lod_path );
				}
			}
		}

		if( lods && model.countOfLOD() )
		{
			wchar_t albedo[1024];
			wchar_t normal[1024];
			wchar_t height[1024];
			wchar_t gim[1024];
			GetPrivateProfileString( model_name.data(), L"albedo", L"", albedo, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"normal", L"", normal, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"height", L"", height, 1024, level_file.data() );
			GetPrivateProfileString( model_name.data(), L"gim",	   L"", gim, 1024, level_file.data() );
			if( wcslen( albedo ) > 0 )
				model.setTexure( DMModel::albedo, DMTexturePool::instance().load_texture( albedo ) );
			if( wcslen( normal ) > 0 )
				model.setTexure( DMModel::TextureType::normal, DMTexturePool::instance().load_texture( normal ) );
			if( wcslen( height ) > 0 )
				model.setTexure( DMModel::TextureType::height, DMTexturePool::instance().load_texture( height ) );
			if( wcslen( gim ) > 0 )
				model.setTexure( DMModel::TextureType::gim, DMTexturePool::instance().load_texture( gim ) );

			int scale = GetPrivateProfileInt( model_name.data(), L"scale", 0, level_file.data() );

			if( scale )
			{
				model.setScale( (float)scale / 100.0f );
			}

			
			int x = GetPrivateProfileInt( model_name.data(), L"pos_x", 0, level_file.data() );
			int y = GetPrivateProfileInt( model_name.data(), L"pos_y", 0, level_file.data() );
			int z = GetPrivateProfileInt( model_name.data(), L"pos_z", 0, level_file.data() );

			model.setPos( (float)x / 100.0f, (float)y / 100.0f, (float)z / 100.0f );

			wchar_t shader[200];
			GetPrivateProfileString( model_name.data(), L"shader", L"", shader, 200, level_file.data() );
			if( wcslen( shader ) )
			{
				//m_render_queues[shader]->append( model.get() );
				model.addRenderQueue( &m_render_queues[shader] );
			}

			model.addRenderQueue( &m_render_queues[L"shadow"] );
			
			m_models[model_name] = std::move( model );

			
		}

		offset_start += wcslen( &out[offset_start] );
		offset_start += 1;
	}

	

	//////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////			TERRAIN			//////////////////////////////////////////////

	
	m_terrain.Initialize( &m_clipMapShader );
	//m_terrain.loadHeightMap( L"Textures\\TerrainHM.png", 1.0, 150.0f );
	m_terrain.loadHeightMap( L"Textures\\terrain\\heightmap.dds", 1.0, 150.0f );
	m_terrain.addBaseTextures( L"Textures\\terrain\\NormalMap.png", L"Textures\\materials\\blocksrough_basecolor_2048.png" );

	DMTerrain::TextureSet texture_set;
	texture_set.blend = L"Textures\\terrain\\Layer.dds";
	
	texture_set.albedo[0] = L"Textures\\materials\\rock_sliced_Base_Color_256.png";
	texture_set.normal[0] = L"Textures\\materials\\rock_sliced_Normal_256.png";
	texture_set.albedo[1] = L"Textures\\materials\\blocksrough_basecolor_256.png";
	texture_set.normal[1] = L"Textures\\materials\\blocksrough_normal_alt.png";
	texture_set.albedo[2] = L"Textures\\materials\\grass1-albedo_256.png";
	texture_set.normal[2] = L"Textures\\materials\\grass1-normal2_256.png";
	texture_set.albedo[3] = L"Textures\\materials\\rock_sliced_Base_Color_256.png";
	texture_set.normal[3] = L"Textures\\materials\\rock_sliced_Normal_256.png";
	texture_set.albedo[4] = L"Textures\\materials\\rock_sliced_Base_Color_256.png";
	texture_set.normal[4] = L"Textures\\materials\\rock_sliced_Normal_256.png";
	m_terrain.setTexturesFirstBlock( &texture_set );

	m_terrain.setTile( 0, 800.0f );
	m_terrain.setTile( 1, 800.0f );
	m_terrain.setTile( 2, 800.0f );
	m_terrain.setTile( 3, 800.0f );
	m_terrain.setTile( 4, 800.0f );

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	
	//m_grass = std::unique_ptr<DMGrass>( new DMGrass( m_dmd3d.get() ) );
	//m_grass->Initialize( 256 );

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	//m_geoClipMapWater = std::unique_ptr< DMGeoClipMap ;
	//m_geoClipMapWater->Initialize( m_dmd3d.get()->GetDevice(), 63, 10, 1024.0f * 1.0, 50.0f );

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////		PARTICLE_SYSTEM         ////////////////////////////////////////////////////////////////////////////////////////////////

	//m_particle_system = std::unique_ptr<DMParticleSystem>( new DMParticleSystem( m_dmd3d.get() ) );
	//m_particle_system->Initialize( 3, 179 );

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	
	
	//m_grass_model = std::unique_ptr<DMModel>( new DMModel( m_dmd3d.get() ) );
	//if( m_grass_model.Initialize( 1000.0f, DMMesh::V_PTN, L"Models\\Box.bin" ) )
	//{
	//	m_grass_model.setTexure( DMModel::albedo, L"Textures\\dirt.dds" );
	//	//m_grass_model.setBasePosition( 2.0f, 0.0f, 2.0f );		
	//	//m_grass_model.setScale( 0.05f );
	//}
	

	// трава
	std::mt19937 gen( 10 );
	std::uniform_real_distribution<float> uif( 2.0f, 1024.0f );
	std::uniform_real_distribution<float> uif2( 0.9f, 1.1f );

	/*
	D3DXVECTOR3 inst_pos;
	D3DXVECTOR3 inst_size = D3DXVECTOR3( m_quad_tree_grass->minLength(), 0.5f, m_quad_tree_grass->minLength() );
	
	for( float i = 0; i < m_quad_tree_grass->sideLength(); i += m_quad_tree_grass->minLength() )
	{
		for( float j = 0; j < m_quad_tree_grass->sideLength(); j += m_quad_tree_grass->minLength() )
		{
			DMIntancePatch<D3DXVECTOR4>* instance_path = new DMIntancePatch<D3DXVECTOR4>( m_dmd3d.get() );
			inst_pos = D3DXVECTOR3( i + m_quad_tree_grass->minLength() / 2.0f, 0.5f, j + m_quad_tree_grass->minLength() / 2.0f );			
			instance_path->m_aabb.CreateAABB( inst_pos, inst_size );

			std::uniform_real_distribution<float> uif_instances_x( i, i + m_quad_tree_grass->minLength() );
			std::uniform_real_distribution<float> uif_instances_y( j, j + m_quad_tree_grass->minLength() );

			for( size_t i = 0; i < 200; i++ )
			{	
				D3DXVECTOR4 grass_pos;
				grass_pos.x = uif_instances_x( gen );
				grass_pos.z = uif_instances_y( gen );
				grass_pos.y = m_heightmap->height( grass_pos.x, grass_pos.z ) * m_geoClipMap->heightMultipler();
				grass_pos.w = 0.5f;
				instance_path->addInstance( grass_pos );
			}
			m_quad_tree_grass->addObject( instance_path );
		}
	}
	*/
	
	
	
	m_sky_sphere.Initialize( 1000.0f, DMMesh::V_PTN, L"Models\\SkySphere.bin" );
	//m_sky_sphere.setTexure( DMModel::albedo, DMTexturePool::instance().load_texture( L"Textures\\SkiesLL_4.dds" ) );
	m_sky_sphere.setTexure( DMModel::albedo, DMTexturePool::instance().load_texture( L"Textures\\SkiesLL_4.dds" ) );

	m_model_buffer["sky_sphere"] = &m_sky_sphere;

	m_render_filter.Initialize( &m_full_screen, &m_full_screen_shader );
	
	
	
	if( !m_box_instance.initialize( m_models.at( L"Sphere" ), INSTANCE_TYPE::INST_POS, 7 ) )
		return false;
	
	m_models.erase( L"Sphere" );
	
	std::vector<DMInstance::VSInstanceDataType> instance_data;
	
	std::uniform_real_distribution<float> scale_uif( 0.5f, 3.1f );
	std::uniform_real_distribution<float> xyz_uif( -5.0f, 5.0f );
	
	

	for( size_t x = 0; x < 7; x++ )
	{
		DMInstance::VSInstanceDataType instance;
		instance.pos.x = x * 2.3 + 2.0;
		instance.pos.y = 0.5f;
		instance.pos.z = 2.0f;
		instance.scale = 1.0f;
		instance_data.push_back( instance );
	}
	
	m_box_instance.set_data( instance_data );


	//m_sphere_instance = std::unique_ptr<DMInstance>( new DMInstance( m_dmd3d.get() ) );
	//if( !m_sphere_instance->initialize( m_models[L"Sphere"], DMInstance::INST_POS, 10000 ) )
	//	return false;
	//
	//m_models.erase( L"Sphere" );
	//
	//instance_data.clear();
	//
	//for( size_t x = 0; x < 10; x++ )
	//{
	//	for( size_t y = 0; y < 10; y++ )
	//	{
	//		for( size_t z = 0; z < 10; z++ )
	//		{
	//			DMInstance::InstanceDataType instance;
	//			instance.pos.x = x * -10.0 + 10.0 + xyz_uif( gen );
	//			instance.pos.y = y * 10.0 + 10.0 + xyz_uif( gen );
	//			instance.pos.z = z * 10.0 + 10.0 + xyz_uif( gen );
	//			instance.scale = scale_uif( gen );
	//			instance_data.push_back( instance );
	//		}
	//	}
	//}
	//
	//m_sphere_instance->set_data( instance_data );

	

	for( auto& pair : m_models )
	{
		m_object_quad_tree.addObject( &pair.second );
	}
	

	return true;
}

bool DMGraphics::Frame()
{
	bool result;
	D3DXVECTOR3 position;

	

	// Read the user input.
	result = m_Input.Frame( );
	if( !result )
	{
		return false;
	}

	// Check if the user pressed escape and wants to exit the application.
	if( m_Input.IsEscapePressed( ) == true )
	{
		return false;
	}
	
	// Update the system stats.
	m_Timer.Frame( );
	m_Fps.Frame( );
	m_Cpu.Frame( );

	// Update the FPS value in the text object.
	result = m_Text.SetFps( m_Fps.GetFps( ) );
	if( !result )
	{
		return false;
	}

	// Update the CPU usage value in the text object.
	result = m_Text.SetCpu( m_Cpu.GetCpuPercentage( ));
	if( !result )
	{
		return false;
	}

	// Do the frame input processing.
	result = HandleInput( (float)m_Timer.GetTime( ) );
	if( !result )
	{
		return false;
	}

//	m_fpsCamera.FrameMove( m_Timer.GetTime( ) );

	for( auto model : m_model_buffer )
	{
		model.second->update( (float)m_Timer.GetTime() );
	}

	//m_draw_container->update( m_Timer.GetTime() );

	Render();
	
	return true;
}

LRESULT DMGraphics::HandleMsg( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
	//m_fpsCamera.HandleMessages( hWnd, uMsg, wParam, lParam );

	return 0;
}

bool DMGraphics::HandleInput( float frameTime )
{
	bool keyDown;
	float posX, posY, posZ, rotX, rotY, rotZ;

	// Set the frame time for calculating the updated position.
	m_Position.SetFrameTime( frameTime );

	if( m_Input.isKeyPressed( DIK_Q ) )
		m_showWireframe = !m_showWireframe;

	// Handle the input.
	keyDown = m_Input.IsForwarPressed( );
	m_Position.MoveForward( keyDown );

	keyDown = m_Input.IsBackwardPressed( );
	m_Position.MoveBackward( keyDown );

	keyDown = m_Input.IsUpMove( );
	m_Position.MoveUpward( keyDown );

	keyDown = m_Input.IsDownMove( );
	m_Position.MoveDownward( keyDown );

	keyDown = m_Input.IsLeftStride( );
	m_Position.MoveLeft( keyDown );

	keyDown = m_Input.IsRightStride( );
	m_Position.MoveRight( keyDown );

	/*
	keyDown = m_Input.IsLookUpPressed( );
	m_Position.LookUpward( keyDown );

	keyDown = m_Input.IsLookDownPressed( );
	m_Position.LookDownward( keyDown );

	keyDown = m_Input.IsLookLeftPressed( );
	m_Position.TurnLeft( keyDown );

	keyDown = m_Input.IsLookRightPressed( );
	m_Position.TurnRight( keyDown );
	*/
	double x, y;
	m_Input.GetMouseLocation( x, y );

	double speed_factor = 0.1;

	x *= speed_factor;
	y *= speed_factor;

	m_Position.SetRotation( (float)y, (float)x, 0.0f );

	// Get the view point position/rotation.
	m_Position.GetPosition( posX, posY, posZ );
	m_Position.GetRotation( rotX, rotY, rotZ );

	// Set the position of the camera.
	m_cameraPool["main"].SetPosition( posX, posY, posZ );
	m_cameraPool["main"].SetRotation( rotX, rotY, rotZ );
	
	// Update the position values in the text object.
	if( !m_Text.SetCameraPosition( posX, posY, posZ ) )
	{
		return false;
	}

	// Update the rotation values in the text object.
	if( !m_Text.SetCameraRotation( rotX, rotY, rotZ ) )
	{
		return false;
	}

	// Update the location of the camera on the mini map.
	//m_MiniMap->PositionUpdate( posX, posZ );

	


	return true;
}

const bool use_hdr = true;

bool DMGraphics::Render()
{
	DMModel* model = nullptr;

	static D3DXMATRIX worldMatrix;
	
	static ID3D11SamplerState* samplers_sates[] = { m_sampler_state.sampler( DMSamplerState::st_point_clamp ),
											m_sampler_state.sampler( DMSamplerState::st_point_wrap ),
											m_sampler_state.sampler( DMSamplerState::st_point_border ),
											m_sampler_state.sampler( DMSamplerState::st_linear_clamp ),
											m_sampler_state.sampler( DMSamplerState::st_linear_wrap ),
											m_sampler_state.sampler( DMSamplerState::st_anisotrop_clamp ),
											m_sampler_state.sampler( DMSamplerState::st_anisotrop_wrap ),
											m_sampler_state.sampler( DMSamplerState::st_cmp_less ) };
	DMD3D::instance().GetDeviceContext()->VSSetSamplers( 0, 8, samplers_sates );
	DMD3D::instance().GetDeviceContext()->GSSetSamplers( 0, 8, samplers_sates );
	DMD3D::instance().GetDeviceContext()->PSSetSamplers( 0, 8, samplers_sates );

	D3DXVECTOR3 cam_pos = m_cameraPool["main"].position();
	
	//cam_pos.x = max( cam_pos.x, 5.0f );
	//cam_pos.z = max( cam_pos.z, 5.0f );
	//cam_pos.x = min( cam_pos.x, m_terrain.heightmap()->size().x - 5.0f );
	//cam_pos.z = min( cam_pos.z, m_terrain.heightmap()->size().y - 5.0f );
	//
	//float terrain_height = m_terrain.heightmap()->height( cam_pos.x, cam_pos.z ) * 300 + 2;
	//m_cameraPool["main"].SetPosition( cam_pos.x, terrain_height, cam_pos.z );
	//m_Position.SetPosition( cam_pos.x, terrain_height, cam_pos.z );

	m_cameraPool["main"].Render();

	for( auto& queue : m_render_queues )
	{
		queue.second.clear();
	}	
	
	/////////////////////////////////////////////////////////////
	//RenderEnvironment();
	static bool first_time = true;
	if( first_time )
	{
		//RenderToTexture();
		//first_time = false;
	}
	//RenderShadowSun();
	RenderShadowLights();
	/////////////////////////////////////////////////////////////
	
	m_light_driver.setShaderBuffer();

	static float app_time = 0;
	app_time += (float)m_Timer.GetTime( );

	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////	
	//
	if( use_hdr )
	{
		m_hdr.begin();
	}
	else
	{
		DMD3D::instance().BeginScene( 0.4f, 0.4f, 0.4f, 1.0f );
	}
	//
	//DMD3D::instance().TurnOnWireframe();
	RenderBackground( m_cameraPool["main"] );
	//DMD3D::instance().TurnOffWireframe();
	//
	///////////////////////////////////////////////////////////////
	//
	int model_draw_count = 0;
	//
	//float elapsed_time = (float)m_Timer.GetTime();
	//
	//m_light_driver.update( elapsed_time );
	//
	//DMLightShader::PSParamBuffer light_ps_buffer;
	//memset( &light_ps_buffer, 0, sizeof( DMLightShader::PSParamBuffer ) );
	//
	////std::swap( m_Camera, m_cameras_shadow[3] );
	//
	m_Frustum.ConstructFrustum( m_cameraPool["main"], 1540.0f );
	
	m_object_quad_tree.checkObjects( m_Frustum, m_cameraPool["main"] );


	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////   GRASS SYSTEM ////////////////////////////////////////////////////////////////////////////////

	
	// grass
	//m_grass_shader.Prepare( m_Camera, 0 );
	//m_grass_shader.setDrawType( DMShader::by_vertex );
	//m_grass_shader.setTextureDistribution( m_TerrainTexturesArray->GetTextureArray()[12] );
	//m_grass_shader.setTextureNoise( m_TerrainTexturesArray->GetTextureArray()[13] );
	//m_grass_shader.setTextureColor( m_TerrainTexturesArray->GetTextureArray()[11] );
	//m_grass_shader.setTextureHeight( m_TerrainTexturesArray->GetTextureArray()[0] );	
	//m_grass_shader.setTerrainColor( m_render_texture.GetShaderResourceView() );
	//m_grass_shader.setMapSize( m_grass->grassMapSize() );
	//m_grass->Generate();
	//
	//DMD3D::instance().TurnZBufferOff();
	//m_grass_shader.Render( m_grass->grassCount(), nullptr );
	//DMD3D::instance().TurnZBufferOn();
	//m_grass->EndGenerate();
	//
	//
	//m_grass->Render();	
	//m_grass_shader.Prepare( m_Camera, 1 );
	//m_grass_shader.setDrawType( DMShader::by_auto );	
	//DMD3D::instance().TurnOnTransparancy();
	//m_grass_shader.Render( m_grass->grassCount(), nullptr );
	//DMD3D::instance().TurnOffTransparancy();
	

	
	///////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////   PARTICLE SYSTEM ////////////////////////////////////////////////////////////////////////////////
		

	//m_particle_system->update( m_grass_cs, elapsed_time );
	//
	//DMD3D::instance().TurnOnTransparancy();
	////
	//m_particle_system->Render();
	//m_particle_shader.Prepare( m_Camera, 0 );
	//m_particle_shader.setTexture( m_TerrainTexturesArray->GetTextureArray()[11] );
	//m_particle_shader.setTextureHeight( m_TerrainTexturesArray->GetTextureArray()[0] );
	//m_particle_shader.setTextureDistributon( m_TerrainTexturesArray->GetTextureArray()[12] );
	//m_particle_shader.Render( m_particle_system->particleCount(), m_particle_system->resultMatrix() );
	////
	//DMD3D::instance().TurnOffTransparancy();
	///////////////////////////////////////////////////////////////////////////////////////////////////////////



	/////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////
	///////////////////		RENDER QUEUES					/////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////
	
	
	
	ID3D11ShaderResourceView* cube_texture = DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"Textures\\uffizi_cross_PBR.dds" ) );
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 5, 1, &cube_texture );
	/*

	DMRenderQueue* current_queue = m_render_queues[L"light"].get();
	current_queue->shader()->Prepare( m_cameraPool["main"], current_queue->phase() );
	for( auto model : current_queue->queue() )
	{
		model.Render( m_cameraPool["main"] );
		model.resultMatrix( &worldMatrix );
		model.setTexures();
		current_queue->shader()->setDrawType( DMShader::by_index );
		current_queue->shader()->Render( model.GetIndexCount(), &worldMatrix );
		model_draw_count += 1;
	}

	current_queue = m_render_queues[L"light_PBR"].get();
	current_queue->shader()->Prepare( m_cameraPool["main"], current_queue->phase() );
	for( auto model : current_queue->queue() )
	{
		model.Render( m_cameraPool["main"] );
		model.resultMatrix( &worldMatrix );
		model.setTexures();
		current_queue->shader()->setDrawType( DMShader::by_index );
		current_queue->shader()->Render( model.GetIndexCount(), &worldMatrix );
		model_draw_count += 1;
	}
	
	current_queue = m_render_queues[L"light_instance"].get();
	current_queue->shader()->Prepare( m_cameraPool["main"], current_queue->phase() );
	
	model = m_box_instance.model();
	if( model )
	{
		model.Render( m_cameraPool["main"] );
		model.resultMatrix( &worldMatrix );
		model.setTexures();
		current_queue->shader()->setDrawType( DMShader::by_index );
		m_box_instance.set_to_draw();
		current_queue->shader()->RenderInstanced( model.GetIndexCount(), m_box_instance.count(), &worldMatrix );
		model_draw_count += m_box_instance.count();
	}*/
	//
	//
	//model = m_sphere_instance->model();
	//model.Render( m_cameraPool["main"] );
	//model.resultMatrix( &worldMatrix );
	//model.setTexures();
	//ID3D11ShaderResourceView* srv = m_render_texture2.GetShaderResourceView();
	//DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 1, 1, &srv );
	//srv = m_render_texture.GetShaderResourceView();
	//DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 2, 1, &srv );
	//current_queue->shader()->setDrawType( DMShader::by_index );
	//m_sphere_instance->set_to_draw();
	//current_queue->shader()->RenderInstanced( model.GetIndexCount(), m_sphere_instance->count(), &worldMatrix );
	//model_draw_count += m_sphere_instance->count();
		

	//DMD3D::instance().TurnOnWireframe();
	
	m_clipMapShader.selectPhase( 0 );
	m_Frustum.ConstructFrustum( m_cameraPool["main"], 10000 );
	//m_terrain_low_res->Render( m_cameraPool["main"], m_Frustum.get() );
	m_terrain.Render( m_cameraPool["main"], m_Frustum );

	//DMD3D::instance().TurnOffWireframe();
	
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////
	
	m_Text.SetRenderCount( model_draw_count );
	//
	if( use_hdr )
	{
		m_hdr.postprocess_and_end( m_cameraPool["ortho"], &m_render_filter );
	
		DMD3D::instance().BeginScene( 0.3f, 0.3f, 0.3f, 1.0f );

		//hdr final pass
		m_render_filter.setSRV( 0, m_hdr.mainTexture() );
		m_render_filter.setSRV( 1, m_hdr.brightTexture() );
		m_render_filter.selectPass( 6 );
	}
		
	//m_render_filter.setSRV( 0, m_light_driver.lights()[0]->shadow_rt()->depthShaderResourceView() );
	//m_render_filter.setSRV( 0, m_hdr.debugTexture() );
	//m_render_filter.selectPass( 0 );
	m_render_filter.Render( m_cameraPool["ortho"], nullptr );

	//m_depth_output.Render( 50, 700 );
	
	RenderGUI();

	// Present the rendered scene to the screen.
	DMD3D::instance().EndScene( );

	//std::swap( m_Camera, m_cameras_shadow[3] );

	return true;
}

void DMGraphics::RenderToTexture()
{
	D3DXMATRIX worldMatrix;

	DMD3D::instance().TurnZBufferOff();
	
	m_render_texture.SetRenderTarget();
	m_render_texture.ClearRenderTarget( 0.0f, 0.0f, 0.0f, 1.0f );
	
	//////////////////////////////////////////////////////////////////////////////////////////////////
	
	m_render_filter.clearSRV();
	m_render_filter.setSRV( 0, m_render_texture2.GetShaderResourceView() );
	m_render_filter.setSRV( 1, DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"Textures\\TerrainHM.png" ) ) );
	m_render_filter.setSRV( 2, DMTexturePool::instance().texture( DMTexturePool::instance().load_texture( L"Textures\\TerrainNRM.dds" ) ) );
	static bool init = true;
	if( init )
	{
		m_render_filter.selectPass( 10 );
		init = false;
	}
	else
	{
		m_render_filter.selectPass( 9 );
	}
	
	m_render_filter.Render( m_cameraPool["ortho"], nullptr );
	
	//////////////////////////////////////////////////////////////////////////////////////////////////

	m_render_texture2.SetRenderTarget();
	m_render_texture2.ClearRenderTarget( 0.0f, 0.0f, 0.0f, 1.0f );

	DMD3D::instance().TurnZBufferOn();

	DMD3D::instance().SetBackBufferRenderTarget();
	DMD3D::instance().ResetViewport();

	std::swap( m_render_texture, m_render_texture2 );

	//ID3D11Resource* tempResource;

	//m_RenderTexture->GetShaderResourceView( )->GetResource( &tempResource );

	//D3DX11SaveTextureToFile( DMD3D::instance().GetDeviceContext( ), tempResource, D3DX11_IFF_DDS, L"normal.dds" );
}

void DMGraphics::RenderGUI( )
{
	D3DXMATRIX worldMatrix;
	// Reset the world matrix.
	D3DXMatrixIdentity( &worldMatrix );

	DMD3D::instance().TurnZBufferOff( );

	// Turn on the alpha blending before rendering the text.
	DMD3D::instance().TurnOnAlphaBlending( );

	DMD3D::instance().TurnCullingNoneRS();

	//m_CameraOrthoText->SetPosition( 0.0f, 0.0f, -0.1f );
	m_cameraPool["text"].Render();

	// Render the text user interface elements.
	m_Text.Render( worldMatrix, m_cameraPool["text"] );

	// Turn off alpha blending after rendering the text.
	DMD3D::instance().TurnOffAlphaBlending( );

	// Turn the Z buffer back on now that all 2D rendering has completed.
	DMD3D::instance().TurnZBufferOn( );
}

void DMGraphics::RenderBackground( const DMCamera& camera )
{
	DMD3D::instance().TurnZBufferOff( );
	DMD3D::instance().TurnCullingNoneRS();

	m_TextureShader.Prepare( camera, 0 );
	{
		if( m_model_buffer.find( "sky_sphere" ) != m_model_buffer.end() )
		{	
			m_sky_sphere.Render( 0.0f );

			D3DXMATRIX worldMatrix;
			//D3DXMatrixIdentity(&worldMatrix );

			D3DXMatrixTranslation( &worldMatrix, camera.position().x, camera.position().y, camera.position().z );

			DMTextureShader::PSParamBuffer param;
			memset( &param, 0, sizeof( DMTextureShader::PSParamBuffer ) );
			param.tex_tiled.x = -1.0f;
			param.tex_tiled.y = 1.0f;

			m_TextureShader.setParameters( &param );
			m_TextureShader.setTexure( DMTexturePool::instance().texture( m_sky_sphere.texture( DMModel::albedo ) ) );
			m_TextureShader.setDrawType( DMShader::by_index );
			m_TextureShader.Render( m_sky_sphere.GetIndexCount(), &worldMatrix );
		}
	}

	DMD3D::instance().TurnDefaultRS();
	DMD3D::instance().TurnZBufferOn( );
}

void DMGraphics::RenderShadowSun()
{
	DMD3D::instance().TurnShadowRS();

	static double app_time = 0;
	app_time += (double)m_Timer.GetTime();

	D3DXMATRIX worldMatrix;

	D3DXVECTOR3 sun_position;
	m_Sun->position( &sun_position );

	D3DXVec3Normalize( &sun_position, &sun_position );

	D3DXVECTOR3 vec_view;
	vec_view = sun_position;
	vec_view *= -1.0f;
	sun_position *= 50.0f;
	sun_position += m_cameraPool["main"].position();

	D3DXVECTOR3 offset_direction;
	m_cameraPool["main"].viewDirection( &offset_direction );

	if( !m_render_queues.count( L"shadow" ) )
		return;

	//DMRenderQueue* queue = m_render_queues[L"shadow"].get();

	for( size_t i = 0; i < m_shadow_depth.size(); i++ )
	{
		m_shadow_depth[i].SetRenderTarget( true );
		m_shadow_depth[i].ClearRenderTarget( 0.0, 0.0, 0.0, 0.0 );

		// calc camera from light		
		m_cameras_shadow[i].SetDirection( vec_view );

		D3DXVECTOR3 shadow_camera_look_at;

		shadow_camera_look_at = sun_position;

		shadow_camera_look_at.x += offset_direction.x * ( 10.0f / 3.0f * ( i + 1.0f ) );
		shadow_camera_look_at.z += offset_direction.z * ( 10.0f / 3.0f * ( i + 1.0f ) );

		m_cameras_shadow[i].SetPosition( shadow_camera_look_at );

		m_cameras_shadow[i].Render();

		//////////////////////////////////////////////////////////////////////////////////////////////////

		m_Frustum.ConstructFrustum( m_cameras_shadow[i], 450.0f );
		m_object_quad_tree.checkObjects( m_Frustum, m_cameras_shadow[i] );

		//m_Frustum.ConstructFrustum( m_cameraPool["main"], 450.0f );
		//m_object_quad_tree.checkObjects( m_Frustum.get(), m_cameraPool["main"] );

		m_depth_shader.Prepare( m_cameras_shadow[i], 0 );
		m_depth_shader.setDrawType( DMShader::by_index );

		for( auto model : m_render_queues[L"shadow"].queue() )
		{
			model->Render( m_cameras_shadow[i] );
			model->resultMatrix( &worldMatrix );
			m_depth_shader.Render( model->GetIndexCount(), &worldMatrix );
		}


		//m_clipMapShader.selectPhase( 2 );
		//m_terrain.Render( m_cameras_shadow[i].get() );

	}

	//////////////////////////////////////////////////////////////////////////////////////////////////

	
	DMD3D::instance().TurnDefaultRS();

	DMD3D::instance().SetBackBufferRenderTarget();
	DMD3D::instance().ResetViewport();
}

void DMGraphics::RenderShadowLights()
{
	DMD3D::instance().TurnShadowRS();

	static double app_time = 0;
	app_time += (double)m_Timer.GetTime();

	D3DXMATRIX worldMatrix;

	if( !m_render_queues.count( L"shadow" ) )
		return;

	

	ID3D11ShaderResourceView* shadow_srv[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 38, 6, shadow_srv );


 	for( auto& light : m_light_driver.lights() )
	{
		if( !light.castShadow() )
			continue;
		
		m_render_queues[L"shadow"].queue().clear();

		const DMCamera& shadow_camera = light.shadowCamera();
		m_Frustum.ConstructFrustum( shadow_camera, light.attenuation );

		m_object_quad_tree.checkObjects( m_Frustum, shadow_camera );
		
		light.shadow_rt().SetRenderTarget( true );
		light.shadow_rt().ClearRenderTarget( 0.0, 0.0, 0.0, 0.0 );
			
		//////////////////////////////////////////////////////////////////////////////////////////////////

		m_depth_shader.Prepare( shadow_camera, 0 );
		m_depth_shader.setDrawType( DMShader::by_index );

		for( auto model : m_render_queues[L"shadow"].queue() )
		{
			model->Render( shadow_camera );
			model->resultMatrix( &worldMatrix );
			m_depth_shader.Render( model->GetIndexCount(), &worldMatrix );
		}	

		//////////////////////////////////////////////////////////////////////////////////////////////////
	}

	DMD3D::instance().TurnDefaultRS();

	DMD3D::instance().SetBackBufferRenderTarget();
	DMD3D::instance().ResetViewport();
}

void DMGraphics::RenderEnvironment()
{
	m_render_cube_texture.initLoop( 509.0f, 11.0f, 490.0f );

	m_light_shader.setLights( &m_light_driver );


	for( uint8_t camera_index = 0; camera_index < 5; ++camera_index )
	{
		RenderBackground( m_render_cube_texture.camera( camera_index ) );

		//m_clipMapShader.selectPhase( 1 );
		//m_terrain_low_res->Render( camera );
		/*
		m_Frustum.ConstructFrustum( camera, 140.0f );
		m_object_quad_tree.checkObjects( m_Frustum.get() );

		D3DXMATRIX worldMatrix;
		m_TextureShader.Prepare( camera, 0 );
		DMTextureShader::PSParamBuffer param;
		memset( &param, 0, sizeof( DMTextureShader::PSParamBuffer ) );
		param.tex_tiled.x = 1.0f;
		param.tex_tiled.y = 1.0f;
		m_TextureShader.setParameters( &param );
		
		m_TextureShader.setDrawType( DMShader::by_index );
		
		for( size_t i = 0; i < m_object_quad_tree.m_visible_objects.size(); i++ )
		{
			DMModel* model = dynamic_cast<DMModel*>( m_object_quad_tree.m_visible_objects[i] );
			if( m_Frustum.CheckAABB( &model.aabb() ) )
			{	
				model.Render( camera );
				model.resultMatrix( &worldMatrix );
				m_TextureShader.setTexure( model.GetTexture( DMModel::albedo ) );
				m_TextureShader.Render( model.GetIndexCount(), &worldMatrix );
			}
		}
		*/
	}

	DMD3D::instance().SetBackBufferRenderTarget();
	DMD3D::instance().ResetViewport();

	
	static bool one_time = true;

	if( one_time )
	{
		ID3D11Resource* tempResource;

		m_render_cube_texture.GetShaderResourceView()->GetResource( &tempResource );

		D3DX11SaveTextureToFile( DMD3D::instance().GetDeviceContext(), tempResource, D3DX11_IFF_DDS, L"SaveCube.dds" );

		tempResource->Release();
	}

}