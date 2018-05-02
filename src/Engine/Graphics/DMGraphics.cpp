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

	// «агружаем модели
	System::models().load( "board.ini" );
	System::models().load( "barrel.ini" );
	System::models().load( "box.ini" );
	System::models().load( "knot.ini" );
	System::models().load( "tree.ini" );

	
	System::models().get( "knot.ini" )->transformBuffer().setScale( 0.2f );
	if( System::models().clone( "box.ini", "box_clone" ) )
		System::models().get("box_clone")->transformBuffer().setPosition( 0.0, 4.0, 0.0 );

	// —оздаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// —оздаем основную камеру
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

	DMD3D::instance().TurnOnWireframe();

	// ”становка буфера вершин и индексов
	m_vertexPool.setBuffers();

	// ѕодготовка view, proj матриц
	m_cameraPool["main"].Render();

	// «агружаем материал ( он пока один )
	DMShader* shader = System::materials().get( 0 )->m_shader.get();
	// заполн€ем параметры шейдера матрицами
	shader->Prepare( m_cameraPool["main"], 0 );

	// эта вс€ хрень дл€ вращени€
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	// ѕеребираем все модели
	for( auto& modelPair : System::models() )
	{
		DMModel* model = modelPair.second.get();
		uint32_t meshid = 0;
		uint32_t materialid = 0;
		// достаем лод меша в зависимости от рассто€ни€ до камеры
		if( model->getLod( 10, meshid, materialid ) )
		{
			//примен€ем вращение модели
			model->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

			shader->setDrawType( DMShader::by_index );
			// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
			shader->Render( System::meshes().get( meshid )->indexCount(),
							System::meshes().get( meshid )->vertexOffset(),
							System::meshes().get( meshid )->indexOffset(), model->transformBuffer().resultMatrix() );
		}
	}
	DMD3D::instance().EndScene();

	return true;
}

}