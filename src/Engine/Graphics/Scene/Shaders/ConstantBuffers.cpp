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

	XMMATRIX viewMatrix;
	XMMATRIX viewInverseMatrix;
	XMMATRIX projectionMatrix;
	XMMATRIX viewProjectionMatrix;

	camera.viewMatrix( &viewMatrix );
	viewInverseMatrix = XMMatrixInverse( nullptr, viewMatrix );
	camera.projectionMatrix( &projectionMatrix );
	viewProjectionMatrix = XMMatrixMultiply( viewMatrix, projectionMatrix );


	// Transpose the matrices to prepare them for the shader.
	
	viewMatrix = XMMatrixTranspose( viewMatrix );
	viewInverseMatrix = XMMatrixTranspose( viewInverseMatrix );
	projectionMatrix = XMMatrixTranspose( projectionMatrix );
	viewProjectionMatrix = XMMatrixTranspose( viewProjectionMatrix );
	
	Device::updateResource<ShaderFrameConstant>( m_frameConstant.get(), [&]( ShaderFrameConstant& data )
	{
		data.view = viewMatrix;
		data.projection = projectionMatrix;
		data.viewInverse = viewInverseMatrix;
		data.viewProjection = viewProjectionMatrix;
		camera.position( &data.cameraPosition );
		camera.viewDirection( &data.viewDirection );
		data.appTime = static_cast<float>( m_timer.totalTime() );
		data.elapsedTime = static_cast<float>( m_timer.GetTime() );
		data.lightsCount = static_cast<float>( lightsCount );
	} );

	ID3D11Buffer* buffer = m_frameConstant.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->GSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->GSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->DSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->PSSetConstantBuffers( 0, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->CSSetConstantBuffers( 0, 1, &buffer );
}

void ConstantBuffers::setPerObjectBuffer( const XMMATRIX& matrix )
{
	setPerObjectBuffer( &matrix );
}

void ConstantBuffers::setPerObjectBuffer( const XMMATRIX* matrix )
{
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	XMMATRIX mat;

	if( !matrix )
	{
		mat = XMMatrixIdentity();
		mat = XMMatrixTranspose( mat );
	}
	else
	{
		mat = XMMatrixTranspose( *matrix );
	}

	Device::updateResource<ShaderModelConstant>( m_modelConstant.get(), [&]( ShaderModelConstant& data )
	{
		data.world = mat;
	} );

	ID3D11Buffer* buffer = m_modelConstant.get();
	DMD3D::instance().GetDeviceContext()->VSSetConstantBuffers( 1, 1, &buffer );
	DMD3D::instance().GetDeviceContext()->DSSetConstantBuffers( 1, 1, &buffer );
}

}