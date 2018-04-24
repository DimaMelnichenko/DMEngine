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

	if( !System::models().load("sphere.ini") )
	{
		MessageBox( hwnd, "Could not load model.", "Error", MB_OK );
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
	DMD3D::instance().BeginScene( 0.6f, 0.6f, 1.0f, 1.0f );
	// Present the rendered scene to the screen.
	DMD3D::instance().EndScene();

	return true;
}

}