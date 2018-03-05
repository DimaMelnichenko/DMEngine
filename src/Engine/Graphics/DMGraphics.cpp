#include "DMGraphics.h"
#include <string>
#include "Scene\TextureObjects\DMTexture2D.h"
#include <random>
#include <ctime>
#include <algorithm>
#include <D3DX11tex.h>

DMGraphics::DMGraphics(  )
{

}

DMGraphics::~DMGraphics()
{
	DMD3D::close();
	DMTexturePool::close();
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
		MessageBox( hwnd, L"Could not initialize DirectX 11.", L"Error", MB_OK );
		return false;
	}

	if( !DMTexturePool::instance().initialize( L"textures\\no_image.dds" ) )
	{
		::MessageBox( 0, L"No found default texture: textures\\no_image.dds ", L"Error", MB_OK );
		return false;
	}

	return true;
}


bool DMGraphics::Frame()
{
	Render();
	
	return true;
}


bool DMGraphics::Render()
{
	DMD3D::instance().BeginScene( 0.4f, 0.4f, 0.4f, 1.0f );
	// Present the rendered scene to the screen.
	DMD3D::instance().EndScene( );

	return true;
}