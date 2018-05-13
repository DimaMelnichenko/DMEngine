#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>
#include "Shaders\Layout.h"
#include "Engine\Input\Input.h"


namespace GS
{

DMGraphics::DMGraphics()
{

}

DMGraphics::~DMGraphics()
{
	DMD3D::close();
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

	System::textures().load( "no_image.dds" );

	// Загружаем модели
	//if( !System::models().load( "barrel.ini" ) )
	//	return false;
	if( !System::models().load( "box.ini" ) )
		return false;
	if( !System::models().load( "knot.ini" ) )
		return false;
	if( !System::models().load( "plane.ini" ) )
		return false;
	if( !System::models().load( "venus.ini" ) )
		return false;
	//System::models().load( "tree.ini" );
	

	//if( System::models().clone( "box.ini", "box_clone" ) )
	//	System::models().get("box_clone")->transformBuffer().setPosition( 0.0, 4.0, 0.0 );

	if( System::models().clone( "knot.ini", "Knot01" ) )
		System::models().get( "Knot01" )->transformBuffer().setPosition( 6.0, 4.0, -8.0 );

	// Создаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// Создаем основную камеру
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 1.0f, 500.0f );
	m_cameraPool["main"].SetPosition( 0.0, 2.0, -3.0 );
	m_cameraPool["main"].SetDirection( 0.0, -0.0, 3.0 );

	m_timer.Initialize();

	Layout layout;
	layout.initLayouts();

	m_shaderConstant.initBuffers();

	m_lightDriver.Initialize();
	m_lightDriver.loadFromFile( "Scene\\Lights.ini" );

	m_samplerState.initialize();

	return true;
}

bool DMGraphics::Frame()
{
	m_timer.Frame();
	


	//update main camera position
	Input& input = getInput();
	input.Frame();

	if( input.IsEscapePressed() )
		return false;

	static float cameraX = m_cameraPool["main"].position().x;
	static float cameraY = m_cameraPool["main"].position().y;
	static float cameraZ = m_cameraPool["main"].position().z;

	if( input.IsForwarPressed() )
	{
		cameraZ += 0.01 * m_timer.GetTime();
	}

	if( input.IsBackwardPressed() )
	{
		cameraZ -= 0.01 * m_timer.GetTime();
	}

	if( input.IsRightStride() )
	{
		cameraX += 0.01 * m_timer.GetTime();
	}

	if( input.IsLeftStride() )
	{
		cameraX -= 0.01 * m_timer.GetTime();
	}

	if( input.IsUpMove() )
	{
		cameraY += 0.01 * m_timer.GetTime();
	}

	if( input.IsDownMove() )
	{
		cameraY -= 0.01 * m_timer.GetTime();
	}

	static bool keyUp = true;
	static bool Q_triggered = false;

	if( input.isKeyPressed( DIK_Q ) && keyUp )
	{
		keyUp = false;
		Q_triggered = !Q_triggered;

		if( Q_triggered )
			DMD3D::instance().TurnOnWireframe();
		else
			DMD3D::instance().TurnOffWireframe();
	}

	if( !input.isKeyPressed( DIK_Q ) && !keyUp )
	{
		keyUp = true;
	}

	m_cameraPool["main"].SetPosition( cameraX, cameraY, cameraZ );

	Render();

	return true;
}

bool DMGraphics::Render()
{
	DMD3D::instance().BeginScene( 0.0f, 0.0f, 0.0f, 1.0f );

	

	// Установка буфера вершин и индексов
	m_vertexPool.setBuffers();

	m_samplerState.setDefaultSmaplers();

	// Подготовка view, proj матриц
	m_cameraPool["main"].Render();

	int lightCount = m_lightDriver.setBuffer( 15, SRVType::ps );

	//установка матриц в шейдер константы
	m_shaderConstant.setPerFrameBuffer( m_cameraPool["main"], lightCount );
	
	for( auto& queue : m_renderQueues )
	{
		queue.second.clear();
	}

	for( const auto& pair : System::models() )
	{
		static D3DXVECTOR3 lenVec;
		const D3DXVECTOR3& camPos = m_cameraPool["main"].position();

		pair.second->transformBuffer().position( &lenVec );
		D3DXVec3Subtract( &lenVec, &lenVec, &camPos );
		float distance = D3DXVec3Length( &lenVec );

		// достаем лод меша в зависимости от расстояния до камеры
		const DMModel::LodBlock* block = pair.second->getLod( distance );

		if( block != nullptr )
		{
			m_renderQueues[block->material].push_back( block );
		}
	}
	

	/*std::sort( resultVector.begin(), resultVector.end(), [this]( const DMModel* modelLeft, const DMModel* modelRight ){
		
		static D3DXVECTOR3 lenVec;
		static D3DXVECTOR3 camPos = m_cameraPool["main"].position();

		modelLeft->transformBuffer().position( &lenVec );
		D3DXVec3Subtract( &lenVec, &lenVec, &camPos );
		float lenLeft = D3DXVec3Length( &lenVec );

		modelRight->transformBuffer().position( &lenVec );
		D3DXVec3Subtract( &lenVec, &lenVec, &camPos );
		float lenRight = D3DXVec3Length( &lenVec );

		return lenLeft < lenRight;
	} );*/


	// эта вся хрень для вращения
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	System::models().get( "knot.ini" )->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

	// Перебираем все очереди
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
			m_shaderConstant.setPerObjectBuffer( LODblock->resultMatrix );

			shader->setParams( LODblock->params );
			// отрисовка модели согласно смещению вершин и индексов для главного буфера
			shader->Render( System::meshes().get( LODblock->mesh )->indexCount(),
							System::meshes().get( LODblock->mesh )->vertexOffset(),
							System::meshes().get( LODblock->mesh )->indexOffset() );
		}
	}
	DMD3D::instance().EndScene();

	return true;
}

}