#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>


namespace GS
{

DMGraphics::DMGraphics()
{

}

DMGraphics::~DMGraphics()
{
	DMD3D::close();
}

bool DMGraphics::Initialize( HINSTANCE hinstance, int screenWidth, int screenHeight, HWND hwnd )
{
	bool result = true;

	m_hwnd = hwnd;

	m_screenWidth = static_cast<float>( screenWidth );
	m_screenHeight = static_cast<float>( screenHeight );

	// Initialize the Direct3D object.
	result = DMD3D::instance().Initialize( screenWidth, screenHeight, VSYNC_ENABLED, hwnd, FULL_SCREEN, SCREEN_DEPTH, SCREEN_NEAR );
	if( !result )
	{
		MessageBox( hwnd, "Could not initialize DirectX 11.", "Error", MB_OK );
		return false;
	}

	// Загружаем модели
	System::models().load( "board.ini" );
	System::models().load( "box.ini" );
	System::models().load( "knot.ini" );

	// Создаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// Создаем основную камеру
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 1.0f, 500.0f );
	m_cameraPool["main"].SetPosition( 0.0, 2.0, -30.0 );
	m_cameraPool["main"].SetDirection( 0.0, -2.0, 30.0 );

	m_timer.Initialize();

	return true;
}


bool DMGraphics::Frame()
{
	m_timer.Frame();
	Render();
	return true;
}


bool DMGraphics::Render()
{
	DMD3D::instance().BeginScene( 0.1f, 0.1f, 0.1f, 1.0f );
	
	// Установка буфера вершин и индексов
	m_vertexPool.setBuffers();

	// Подготовка view, proj матриц
	m_cameraPool["main"].Render();

	// Загружаем материал ( он пока один )
	DMShader* shader = System::materials().get( 0 )->m_shader.get();
	// заполняем параметры шейдера матрицами
	shader->Prepare( m_cameraPool["main"], 0 );

	// эта вся хрень для вращения
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	// Перебираем все модели
	for( auto& modelPair : System::models() )
	{
		DMModel* model = modelPair.second.get();
		uint32_t meshid = 0;
		uint32_t materialid = 0;
		// достаем лод меша в зависимости от расстояния до камеры
		model->getLod( 10, meshid, materialid );

		//применяем вращение модели
		model->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

		shader->setDrawType( DMShader::by_index );
		// отрисовка модели согласно смещению вершин и инлесов для главного буфера
		shader->Render( System::meshes().get( meshid )->indexCount(),
						System::meshes().get( meshid )->vertexOffset(),
						System::meshes().get( meshid )->indexOffset(), model->transformBuffer().resultMatrix() );
	}
	DMD3D::instance().EndScene();

	return true;
}

}