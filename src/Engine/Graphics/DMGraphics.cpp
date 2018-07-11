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


namespace GS
{

DMGraphics::DMGraphics()
{

}

DMGraphics::~DMGraphics()
{
	pipelineDestroy();
	System::destroy();
}

bool DMGraphics::Initialize( HINSTANCE hinstance, int screenWidth, int screenHeight, HWND hwnd, Config config )
{
	bool result = true;
	m_hwnd = hwnd;
	m_config = config;

	m_screenWidth = static_cast<float>( screenWidth );
	m_screenHeight = static_cast<float>( screenHeight );

	// Initialize the Direct3D object.
	result = DMD3D::instance().Initialize( 
		screenWidth, screenHeight, m_config.vSync(), hwnd, m_config.fullScreen(), 
		m_config.ScreenDepth(), m_config.ScreenNear() );

	if( !result )
	{
		MessageBox( hwnd, "Could not initialize DirectX 11.", "Error", MB_OK );
		return false;
	}

	m_library.load();

	// «агружаем модели
	//if( !System::models().load( "barrel.ini" ) )
	//	return false;
	if( !System::models().load( "box.ini" ) )
		return false;
	/*if( !System::models().load( "knot.json" ) )
		return false;
	if( !System::models().load( "plane.ini" ) )
		return false;
	if( !System::models().load( "venus.ini" ) )
		return false;
	if( !System::models().load( "skysphere.xml" ) )
		return false;*/
	

	//if( System::models().clone( "Knot", "Knot01" ) )
	//	System::models().get( "Knot01" )->transformBuffer().setPosition( 6.0, 4.0, -8.0 );

	// —оздаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// —оздаем основную камеру
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 0.1f, 5000.0f );
	m_cameraPool["main"].SetPosition( 0.0, 20.0, 0.0 );
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

	//if( !m_terrain.Initialize( "Models\\terrain.json", "GeoClipMap" ) )
	//	return false;

	//if( !m_water.Initialize( "Models\\water.json", "GeoClipMapWater" ) )
	//	return false;

	return true;
}

bool DMGraphics::Frame()
{
	m_timer.Frame();
	
	m_cameraController.frame( m_timer.GetTime() );

	Render();

	return true;
}

bool DMGraphics::Render()
{
	DMD3D::instance().BeginScene( 0.0f, 0.0f, 0.0f, 1.0f );

	// ѕодготовка view, proj матриц
	m_cameraPool["main"].Update( m_timer.GetTime() );

	// ”становка буфера вершин и индексов
	m_vertexPool.setBuffers();

	m_samplerState.setDefaultSmaplers();

	int lightCount = m_lightDriver.setBuffer( 100, SRVType::ps );

	//установка матриц в шейдер константы
	pipeline().shaderConstant().setPerFrameBuffer( m_cameraPool["main"], lightCount );

	//render sky
	renderSky();

	
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

		if( block != nullptr )
		{
			m_renderQueues[block->material].push_back( block );
		}
	}

	// эта вс€ хрень дл€ вращени€
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	System::models().get( "Knot" )->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

	// ѕеребираем все очереди
	for( auto& queuePair : m_renderQueues )
	{	
		RenderQueue& queue = queuePair.second;
		DMShader* shader = System::materials().get( queuePair.first )->m_shader.get();
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );
		
		for( const auto LODblock : queue )
		{
			//установка матрицы маодели в шейдер
			//m_shaderConstant.setPerObjectBuffer( model->transformBuffer().resultMatrixPtr() );
			const std::string& meshName = System::meshes().get( LODblock->mesh )->name();
			pipeline().shaderConstant().setPerObjectBuffer( LODblock->resultMatrix );

			shader->setParams( LODblock->params );
			// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
			shader->Render( System::meshes().get( LODblock->mesh )->indexCount(),
							System::meshes().get( LODblock->mesh )->vertexOffset(),
							System::meshes().get( LODblock->mesh )->indexOffset() );
		}
	}

	DMFrustum frustum( m_cameraPool["main"], 1000.0f );
	if( m_visible["terrain"] )
	{
		m_terrain.Render( m_cameraPool["main"], frustum );
	}
	if( m_visible["water"] )
	{
		DMD3D::instance().TurnOnAlphaBlending();
		m_water.Render( m_cameraPool["main"], frustum );
		DMD3D::instance().TurnOffAlphaBlending();
	}

	DMD3D::instance().EndScene();

	return true;
}

bool DMGraphics::renderSky()
{
	ID3D11RasterizerState* prevRSState = DMD3D::instance().currentRS();

	DMD3D::instance().TurnZBufferOff();
	DMD3D::instance().TurnBackFacesRS();

	DMShader* shader = System::materials()["Texture"]->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	DMModel* model = System::models()["SkySphere"].get();
	model->transformBuffer().setPosition( m_cameraPool["main"].position() );

	const DMModel::LodBlock* block = model->getLod( 0.0f );
	const std::string& meshName = System::meshes().get( block->mesh )->name();
	pipeline().shaderConstant().setPerObjectBuffer( block->resultMatrix );

	shader->setParams( block->params );
	// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
	shader->Render( System::meshes().get( block->mesh )->indexCount(),
					System::meshes().get( block->mesh )->vertexOffset(),
					System::meshes().get( block->mesh )->indexOffset() );


	
	DMD3D::instance().TurnZBufferOn();

	DMD3D::instance().setRS( prevRSState );

	return true;
}

}