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
	if( !System::models().load( "board.ini" ) )
		return false;
	if( !System::models().load( "barrel.ini" ) )
		return false;
	if( !System::models().load( "box.ini" ) )
		return false;
	if( !System::models().load( "knot.ini" ) )
		return false;
	//System::models().load( "tree.ini" );
	
	
	System::models().get( "knot.ini" )->transformBuffer().setScale( 0.2f );
	if( System::models().clone( "box.ini", "box_clone" ) )
		System::models().get("box_clone")->transformBuffer().setPosition( 0.0, 4.0, 0.0 );

	if( System::models().clone( "knot.ini", "knot01" ) )
		System::models().get( "knot01" )->transformBuffer().setPosition( 6.0, -2.0, -8.0 );

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

	DMLight light( DMLight::Point );
	light.setColor( 0.3, 0.3, 0.3 );
	light.attenuation = 1400.0f;
	light.m_transformBuffer.setPosition( 0.0, 20.0, -25.0 );
	m_lightDriver.addLight( light );

	DMLight light2( DMLight::Point );
	light2.setColor( 0.5, 0.2, 0.0 );
	light2.attenuation = 1400.0f;
	light2.m_transformBuffer.setPosition( -25.0, 0.0, 0.0 );
	m_lightDriver.addLight( light2 );

	DMLight light3( DMLight::Point );
	light3.setColor( 0.3, 0.3, 0.5 );
	light3.attenuation = 1400.0f;
	light3.m_transformBuffer.setPosition( 25.0, 0.0, 0.0 );
	m_lightDriver.addLight( light3 );

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
	DMD3D::instance().BeginScene( 0.1f, 0.1f, 0.1f, 1.0f );

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
	DMShader* shader = System::materials().get( 1 )->m_shader.get();
	// заполн€ем параметры шейдера матрицами
	shader->setPass( 0 );

	// эта вс€ хрень дл€ вращени€
	double elapsedTime = m_timer.GetTime();
	static double counter = 0.0;
	counter += 0.001 * elapsedTime;

	ID3D11ShaderResourceView* tex = System::textures().get( 0 )->GetTexture();
	DMD3D::instance().GetDeviceContext()->PSSetShaderResources( 0, 1, &tex );

	// ѕеребираем все модели
	for( auto model : resultVector )
	{
		//DMModel* model = modelPair.second.get();
		uint32_t meshid = 0;
		uint32_t materialid = 0;
		// достаем лод меша в зависимости от рассто€ни€ до камеры
		if( model->getLod( 10, meshid, materialid ) )
		{
			//примен€ем вращение модели
			model->transformBuffer().setRotationAxis( 0.0, 1.0, 0.0, counter );

			//установка матрицы маодели в шейдер
			m_shaderConstant.setPerObjectBuffer( model->transformBuffer().resultMatrixPtr() );

			shader->setDrawType( DMShader::by_index );
			// отрисовка модели согласно смещению вершин и индексов дл€ главного буфера
			shader->Render( System::meshes().get( meshid )->indexCount(),
							System::meshes().get( meshid )->vertexOffset(),
							System::meshes().get( meshid )->indexOffset() );
		}
	}
	DMD3D::instance().EndScene();

	return true;
}

}