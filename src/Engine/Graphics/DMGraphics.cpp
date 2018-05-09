#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>
#include "Shaders\Layout.h"


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

	// «агружаем модели
	//if( !System::models().load( "barrel.ini" ) )
	//	return false;
	if( !System::models().load( "box.ini" ) )
		return false;
	if( !System::models().load( "knot.ini" ) )
		return false;
	if( !System::models().load( "plane.ini" ) )
		return false;
	//if( !System::models().load( "venus.ini" ) )
	//	return false;
	//System::models().load( "tree.ini" );
	

	//if( System::models().clone( "box.ini", "box_clone" ) )
	//	System::models().get("box_clone")->transformBuffer().setPosition( 0.0, 4.0, 0.0 );

	if( System::models().clone( "knot.ini", "Knot01" ) )
		System::models().get( "Knot01" )->transformBuffer().setPosition( 6.0, 4.0, -8.0 );

	// —оздаем общий буфер вершин и индексов
	m_vertexPool.prepareMeshes();

	// —оздаем основную камеру
	m_cameraPool["main"].Initialize( DMCamera::CT_PERSPECTIVE, m_screenWidth, m_screenHeight, 1.0f, 500.0f );
	m_cameraPool["main"].SetPosition( 0.0, 2.0, -30.0 );
	m_cameraPool["main"].SetDirection( 0.0, -2.0, 30.0 );

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
	Render();
	return true;
}

bool DMGraphics::Render()
{
	DMD3D::instance().BeginScene( 0.0f, 0.0f, 0.0f, 1.0f );

	//DMD3D::instance().TurnOnWireframe();

	// ”становка буфера вершин и индексов
	m_vertexPool.setBuffers();

	m_samplerState.setDefaultSmaplers();

	// ѕодготовка view, proj матриц
	m_cameraPool["main"].Render();

	int lightCount = m_lightDriver.setBuffer( 15, SRVType::ps );

	//установка матриц в шейдер константы
	m_shaderConstant.setPerFrameBuffer( m_cameraPool["main"], lightCount );
	
	static bool firstTime = true;
	static std::vector<DMModel*> resultVector;
	if( firstTime )
	{
		resultVector.reserve( System::models().size() );
		for( auto& pair : System::models() )
		{
			resultVector.push_back( pair.second.get() );
		}
		firstTime = false;
	}

	std::sort( resultVector.begin(), resultVector.end(), [this]( const DMModel* modelLeft, const DMModel* modelRight ){
		
		static D3DXVECTOR3 lenVec;
		static D3DXVECTOR3 camPos = m_cameraPool["main"].position();

		modelLeft->transformBuffer().position( &lenVec );
		D3DXVec3Subtract( &lenVec, &lenVec, &camPos );
		float lenLeft = D3DXVec3Length( &lenVec );

		modelRight->transformBuffer().position( &lenVec );
		D3DXVec3Subtract( &lenVec, &lenVec, &camPos );
		float lenRight = D3DXVec3Length( &lenVec );

		return lenLeft < lenRight;
	} );

	// «агружаем материал ( он пока один )
	DMShader* shader = System::materials().get( "Phong" )->m_shader.get();
	// заполн€ем параметры шейдера матрицами
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	// эта вс€ хрень дл€ вращени€
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	// ѕеребираем все модели
	for( auto model : resultVector )
	{	
		// достаем лод меша в зависимости от рассто€ни€ до камеры
		const DMModel::LodBlock* block = model->getLod( 10 );
		if( block != nullptr )
		{
			//примен€ем вращение модели
			if( model->name() == "Knot" || model->name() == "Knot01" )
				model->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

			//установка матрицы маодели в шейдер
			m_shaderConstant.setPerObjectBuffer( model->transformBuffer().resultMatrixPtr() );

			shader->setParams( block->params );
			// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
			shader->Render( System::meshes().get( block->mesh )->indexCount(),
							System::meshes().get( block->mesh )->vertexOffset(),
							System::meshes().get( block->mesh )->indexOffset() );
		}
	}
	DMD3D::instance().EndScene();

	return true;
}

}