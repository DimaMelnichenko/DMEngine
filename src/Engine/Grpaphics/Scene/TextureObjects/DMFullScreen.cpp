#include "DMFullScreen.h"



DMFullScreen::DMFullScreen( DMD3D* parent ) : DMSceneObject( parent )
{
}


DMFullScreen::~DMFullScreen()
{
}

void DMFullScreen::Render()
{
	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 0, nullptr, nullptr, nullptr );
	m_dmd3d->GetDeviceContext()->IASetIndexBuffer( nullptr, (DXGI_FORMAT)0, 0 );
}

unsigned int DMFullScreen::vertexCount()
{
	return 3;
}