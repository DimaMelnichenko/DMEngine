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
	
	Device::updateResource<ShaderFrameConstant>( m_frameConstant.get(), [&]( ShaderFrameConstant& data )
	{
		memcpy( &data.view, &viewMatrix, sizeof( D3DXMATRIX ) );
		memcpy( &data.projection, &projectionMatrix, sizeof( D3DXMATRIX ) );
		memcpy( &data.viewInverse, &viewInverseMatrix, sizeof( D3DXMATRIX ) );
		memcpy( &data.viewProjection, &viewProjectionMatrix, sizeof( D3DXMATRIX ) );
		camera.position( &data.cameraPosition );
		camera.viewDirection( &data.viewDirection );
		data.appTime = static_cast<float>( m_timer.totalTime() );
		data.elapsedTime = static_cast<float>( m_timer.GetTime() );
		data.lightsCount = static_cast<float>( lightsCount );
	} );

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

	Device::updateResource<ShaderModelConstant>( m_modelConstant.get(), [&]( ShaderModelConstant& data )
	{
		data.world = mat;
	} );

	ID3D11Buffer* buffer = m_modelConstant.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 1, 1, &buffer );
}

}