#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>
#include "Shaders\Layout.h"
#include "../Input/Input.h"
#include "Pipeline.h"
#include <chrono>

#define TIME_POINT() std::chrono::high_resolution_clock::now()

#define TIME_DIFF( start, end ) std::chrono::duration_cast<std::chrono::microseconds>( end - start ).count()

#define TIME_CHECK( CHECKED_FUNC, INFO_TEXT ) { \
	std::chrono::high_resolution_clock::time_point start##__LINE__ = std::chrono::high_resolution_clock::now(); \
	CHECKED_FUNC; \
	std::chrono::high_resolution_clock::time_point end##__LINE__ = std::chrono::high_resolution_clock::now(); \
	m_GUI.addCounterInfo( (INFO_TEXT), std::chrono::duration_cast<std::chrono::microseconds>( end##__LINE__ - start##__LINE__ ).count() / 1000.0f ); }


namespace GS
{

DMGraphics::DMGraphics()
{

}

DMGraphics::~DMGraphics()
{
	pipelineDestroy();
	System::destroy();
	DMD3D::destroy();
}

bool DMGraphics::Initialize( HINSTANCE hinstance, int screenWidth, int screenHeight, HWND hwnd, Config config )
{
	bool result = true;
	m_hwnd = hwnd;
	m_config = config;

	m_screenWidth = static_cast<float>( screenWidth );
	m_screenHeight = static_cast<float>( screenHeight );

	// Initialize the Direct3D object.
	result = DMD3D::instance().Initialize( m_config, hwnd );

	if( !result )
	{
		MessageBox( hwnd, "Could not initialize DirectX 11.", "Error", MB_OK );
		return false;
	}

	if( !m_library.init() )
		return false;

	
	for( uint16_t i = 7; i > 0; --i )
	{
		if( !m_library.loadModelWithLOD( i ) )
			return false;
	}

	for( uint16_t i = 29; i > 0; --i )
	{
		if( !m_library.loadTexture( i ) )
			return false;
	}

	for( uint16_t i = 10; i > 0; --i )
	{
		if( !m_library.loadMaterial( i ) )
			return false;
	}

	// —оздаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// —оздаем основную камеру
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 0.1f, 10000.0f );
	m_cameraPool["main"].SetPosition( 0.0, 0.0, -5.0 );
	//m_cameraPool["main"].SetDirection( 0.0, -0.0, 3.0 );

	m_timer.Initialize();

	Layout layout;
	layout.initLayouts();

	pipeline().init();

	m_lightDriver.Initialize();
	m_lightDriver.loadFromFile( "Scene\\Lights.ini" );

	m_samplerState.initialize();

	getInput().notifier().registerTrigger( DIK_Q, [](bool value)
	{
		if( value )
			DMD3D::instance().TurnOnWireframe();
		else
			DMD3D::instance().TurnOffWireframe();
	} );

	getInput().notifier().registerTrigger( DIK_P, []( bool value )
	{
		if( value )
		{
			DMD3D::instance().createScreenshot();
		}
			
	} );

	m_visible["terrain"] = true;
	m_visible["water"] = true;
	getInput().notifier().registerTrigger( DIK_1, [this]( bool value )
	{
		m_visible["terrain"] = value;
	} );

	getInput().notifier().registerTrigger( DIK_2, [this]( bool value )
	{
		m_visible["water"] = value;
	} );

	getInput().notifier().registerTrigger( DIK_3, [this]( bool value )
	{
		m_visible["grass calc"] = value;
	} );

	getInput().notifier().registerTrigger( DIK_4, [this]( bool value )
	{
		m_visible["grass render"] = value;
	} );

	getInput().notifier().registerTrigger( DIK_I, [this]( bool value )
	{
		m_cursorMode = value;
		ShowCursor( value );
	} );

	

	if( !m_terrain.Initialize( "Models\\terrain.json", "GeoClipMap", m_library ) )
		return false;

	//if( !m_water.Initialize( "Models\\water.json", "GeoClipMapWater" ) )
	//	return false;
	
	if( !m_grass.Initialize() )
		return false;
	m_grass.addMesh( GS::System::models().get( "GrassBlade" )->getLodById( 1 ) );
	m_grass.addMesh( GS::System::models().get( "GrassCross" )->getLodById( 0 ) );
	m_grass.addMesh( GS::System::models().get( "Romashka" )->getLodById( 0 ) );

	m_particleSystem.Initialize( 20, 200 );

	m_GUI.Initialize( m_hwnd );

	return true;
}

bool DMGraphics::Frame()
{
	m_timer.Frame();
	
	m_cameraController.frame( m_timer.GetTime() );

	if( !Render() )
		return false;

	return true;
}

void DMGraphics::ComputePass()
{
	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( 25 )->srv() );
	TIME_CHECK( m_particleSystem.update( m_timer.GetTime() ), "Particle Update = = %.3f ms" );
	
	//std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
	//m_grass.prerender();
	//std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
	if( m_visible.count("grass calc") && m_visible["grass calc"] )
		TIME_CHECK( m_grass.prerender(), "m_grass.prerender() = %.3f ms" );
}

void DMGraphics::preparePipeline()
{
	// ѕодготовка view, proj матриц
	TIME_CHECK( m_cameraPool["main"].Update( m_timer.GetTime(), m_cursorMode ), "Camera Update = = %.3f ms" );

	TIME_CHECK( m_samplerState.setDefaultSmaplers(), "setDefaultSmaplers = = %.3f ms" );

	int lightCount = m_lightDriver.setBuffer( 100, SRVType::ps );
	//установка матриц в шейдер константы
	TIME_CHECK( pipeline().shaderConstant().setPerFrameBuffer( m_cameraPool["main"], lightCount ), "setPerFrameBuffer = = %.3f ms" );
}

bool DMGraphics::Render()
{
	TIME_CHECK( preparePipeline(), "preparePipeline = %.3f ms" );

	TIME_CHECK( ComputePass(), "Compute Pass = %.3f ms" );

	DMD3D::instance().BeginScene( 0.0f, 0.0f, 0.0f, 1.0f );

	// ”становка буфера вершин и индексов
	m_vertexPool.setBuffers();

	//render sky
	TIME_CHECK( renderSky(), "Render Sky = %.3f ms" );

	auto objectCullStart = TIME_POINT();
	for( auto& queue : m_renderQueues )
	{
		queue.second.clear();
	}

	for( const auto& pair : System::models() )
	{
		static XMVECTOR lenVec;
		XMFLOAT3 camPos = m_cameraPool["main"].position();
		XMVECTOR camPosVector = XMLoadFloat3( &camPos );

		lenVec = pair.second->transformBuffer().position();
		lenVec = XMVectorSubtract( lenVec, camPosVector );
		XMVECTOR distance = XMVector3Length( lenVec );

		// достаем лод меша в зависимости от рассто€ни€ до камеры
		const DMModel::LodBlock* block = pair.second->getLod( distance.m128_f32[0] );

		if( block != nullptr && block->isRender )
		{
			m_renderQueues[block->material].push_back( block );
		}
	}
	auto objectCullEnd = TIME_POINT();
	m_GUI.addCounterInfo( "Object Prepare = %.3f ms", TIME_DIFF( objectCullStart, objectCullEnd ) / 1000.0f );

	// эта вс€ хрень дл€ вращени€
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	//	System::models().get( "Knot" )->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

	auto objectStart = TIME_POINT();
	// ѕеребираем все очереди
	for( auto& queuePair : m_renderQueues )
	{
		RenderQueue& queue = queuePair.second;
		DMShader* shader = System::materials().get( queuePair.first )->m_shader.get();
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );

		for( const auto LODblock : queue )
		{
			//установка матрицы модели в шейдер
			//m_shaderConstant.setPerObjectBuffer( model->transformBuffer().resultMatrixPtr() );
			const std::string& meshName = System::meshes().get( LODblock->mesh )->name();
			pipeline().shaderConstant().setPerObjectBuffer( LODblock->resultMatrix );

			shader->setParams( LODblock->params );
			// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
			shader->render( System::meshes().get( LODblock->mesh )->indexCount(),
							System::meshes().get( LODblock->mesh )->vertexOffset(),
							System::meshes().get( LODblock->mesh )->indexOffset() );
		}
	}
	auto objectEnd = TIME_POINT();
	m_GUI.addCounterInfo( "Object Rendering = %.3f ms", TIME_DIFF( objectStart, objectEnd ) / 1000.0f );


	DMD3D::instance().TurnOnAlphaBlending();
	if( m_visible.count( "grass render" ) && m_visible["grass render"] )
	{
		TIME_CHECK( grassRendering(), "Grass Rendering = %.3f ms" );
	}
	//if( m_visible.count( "grass render" ) && m_visible["grass render"] )
	{
		TIME_CHECK( particleRendering(), "Particle Rendering = %.3f ms" );
	}
	DMD3D::instance().TurnOffAlphaBlending();



	DMFrustum frustum( m_cameraPool["main"], 1000.0f );
	if( m_visible["terrain"] )
	{
		TIME_CHECK( m_terrain.Render( m_cameraPool["main"], frustum ), "Terrain Render = %.3f ms" );
	}
	/*if( m_visible["water"] )
	{
		DMD3D::instance().TurnOnAlphaBlending();
		m_water.Render( m_cameraPool["main"], frustum );
		DMD3D::instance().TurnOffAlphaBlending();
	}
	*/

	

	
	static uint64_t guiResult = 0;

	m_GUI.addCounterInfo( "GUI Rendering = %.3f ms", guiResult / 1000.0f );

	auto guiStart = TIME_POINT();
	m_GUI.Frame();
	m_GUI.Render();
	auto guiFinish = TIME_POINT();
	guiResult = TIME_DIFF( guiStart, guiFinish );

	
	DMD3D::instance().EndScene();

	return true;
}

bool DMGraphics::renderSky()
{
	//com_unique_ptr<ID3D11RasterizerState> prevRSState;
	//DMD3D::instance().currentRS( prevRSState );

	DMD3D::instance().TurnZBufferOff();
	DMD3D::instance().TurnFrontFacesRS();

	if( !System::materials().exists( "Texture" ) )
		return false;

	DMShader* shader = System::materials()["Texture"]->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	if( !System::models().exists( "SkySphere" ) )
		return false;

	DMModel* model = System::models()["SkySphere"].get();
	model->transformBuffer().setPosition( m_cameraPool["main"].position() );

	const DMModel::LodBlock* block = model->getLod( 0.0f );
	const std::string& meshName = System::meshes().get( block->mesh )->name();
	pipeline().shaderConstant().setPerObjectBuffer( block->resultMatrix );

	shader->setParams( block->params );
	// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
	shader->render( System::meshes().get( block->mesh )->indexCount(),
					System::meshes().get( block->mesh )->vertexOffset(),
					System::meshes().get( block->mesh )->indexOffset() );
	
	DMD3D::instance().TurnZBufferOn();
	DMD3D::instance().TurnDefaultRS();

	//DMD3D::instance().setRS( prevRSState.get() );
	
	return true;
}

void DMGraphics::beforeExit()
{
	m_library.save();
}

void DMGraphics::grassRendering()
{	
	DMD3D::instance().TurnCullingNoneRS();

	XMMATRIX worldMatrix = XMMatrixIdentity();

	for( uint16_t i = 0; i < m_grass.lodCount(); ++i )
	{
		DMModel::LodBlock* block = m_grass.lodBlock( i );
		//DMShader* shader = System::materials().get( "VertLightInstance" )->m_shader.get();
		DMShader* shader = System::materials().get( block->material )->m_shader.get();
		shader->setParams( block->params );
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );
		m_grass.Render( i, 16 );
		
		pipeline().shaderConstant().setPerObjectBuffer( &worldMatrix );
		shader->renderInstancedIndirect( m_grass.indirectArgsBuffer( i ) );
	}
	DMD3D::instance().TurnDefaultRS();
}

void DMGraphics::particleRendering()
{
	DMShader* shader = System::materials().get( "Particle" )->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_vertex );

	m_particleSystem.Render();
	DMD3D::instance().setSRV( SRVType::ps, 0, System::textures().get( "oduvanchik" )->srv() );
	shader->render( m_particleSystem.particleCount(), 0, 0 );
	DMD3D::instance().GetDeviceContext()->GSSetShader( nullptr, nullptr, 0 );
}

}
