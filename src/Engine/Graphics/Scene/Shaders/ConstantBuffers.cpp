#include "ConstantBuffers.h"
#include "D3D\DMD3D.h"

namespace GS
{

ConstantBuffers::ConstantBuffers()
{

}

ConstantBuffers::~ConstantBuffers()
{

}

void ConstantBuffers::initBuffers()
{
	DMD3D::instance().createShaderConstantBuffer( sizeof( ShaderFrameConstant ), m_frameConstant );
	DMD3D::instance().createShaderConstantBuffer( sizeof( ShaderModelConstant ), m_modelConstant );	
	m_timer.Initialize();
}

void ConstantBuffers::setPerFrameBuffer( const DMCamera& camera, int lightsCount )
{
	m_timer.Frame();

	D3DXMATRIX viewMatrix;
	D3DXMATRIX viewInverseMatrix;
	D3DXMATRIX projectionMatrix;
	D3DXMATRIX viewProjectionMatrix;

	camera.viewMatrix( &viewMatrix );
	D3DXMatrixInverse( &viewInverseMatrix, nullptr, &viewMatrix );
	camera.projectionMatrix( &projectionMatrix );
	D3DXMatrixMultiply( &viewProjectionMatrix, &viewMatrix, &projectionMatrix );


	// Transpose the matrices to prepare them for the shader.

	D3DXMatrixTranspose( &viewMatrix, &viewMatrix );
	D3DXMatrixTranspose( &viewInverseMatrix, &viewInverseMatrix );
	D3DXMatrixTranspose( &projectionMatrix, &projectionMatrix );
	D3DXMatrixTranspose( &viewProjectionMatrix, &viewProjectionMatrix );
	
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_frameConstant.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	ShaderFrameConstant* dataPtr = reinterpret_cast<ShaderFrameConstant*>( mappedResource.pData );
	// Copy the matrices into the constant buffer.
	memcpy( &dataPtr->view, &viewMatrix, sizeof( D3DXMATRIX ) );
	memcpy( &dataPtr->projection, &projectionMatrix, sizeof( D3DXMATRIX ) );
	memcpy( &dataPtr->viewInverse, &viewInverseMatrix, sizeof( D3DXMATRIX ) );
	memcpy( &dataPtr->viewProjection, &viewProjectionMatrix, sizeof( D3DXMATRIX ) );
	camera.position( &dataPtr->cameraPosition );
	camera.viewDirection( &dataPtr->viewDirection );
	dataPtr->appTime = static_cast<float>( m_timer.totalTime() );
	dataPtr->elapsedTime = static_cast<float>( m_timer.GetTime() );
	dataPtr->lightsCount = static_cast<float>( lightsCount );

	DMD3D::instance().GetDeviceContext()->Unmap( m_frameConstant.get(), 0 );

	ID3D11Buffer* buffer = m_frameConstant.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->GSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 0, 1, &buffer );
}

void ConstantBuffers::setPerObjectBuffer( const D3DXMATRIX* matrix )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	D3DXMATRIX mat;

	if( !matrix )
	{
		D3DXMatrixIdentity( &mat );
		D3DXMatrixTranspose( &mat, &mat );
	}
	else
	{
		D3DXMatrixTranspose( &mat, matrix );
	}

	// Lock the constant buffer so it can be written to.
	HRESULT result = DMD3D::instance().GetDeviceContext()->Map( m_modelConstant.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource );
	if( FAILED( result ) )
	{
		return;
	}

	// Get a pointer to the data in the constant buffer.
	ShaderModelConstant* dataPtr = reinterpret_cast<ShaderModelConstant*>( mappedResource.pData );

	// Copy the matrices into the constant buffer.
	memcpy( &dataPtr->world, &mat, sizeof( D3DXMATRIX ) );

	// Unlock the constant buffer.
	DMD3D::instance().GetDeviceContext()->Unmap( m_modelConstant.get(), 0 );

	ID3D11Buffer* buffer = m_modelConstant.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 1, 1, &buffer );
}

}