#include "DMFullScreen.h"
#include "D3D\DMD3D.h"


DMFullScreen::DMFullScreen()
{
}


DMFullScreen::~DMFullScreen()
{
}

void DMFullScreen::Render()
{
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( nullptr, (DXGI_FORMAT)0, 0 );
}

unsigned int DMFullScreen::vertexCount()
{
	return 3;
}