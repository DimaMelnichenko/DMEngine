#include "ConstantBuffers.h"
#include "Shaders\slots.h"
#include "D3D\DMD3D.h"

namespace GS
{

XMMATRIX normalMatrix( const XMMATRIX& world )
{
	XMMATRIX linear = world;
	linear.r[3] = XMVectorSet( 0.0f, 0.0f, 0.0f, 1.0f );
	XMVECTOR determinant;
	const XMMATRIX inverse = XMMatrixInverse( &determinant, linear );
	return fabsf( XMVectorGetX( determinant ) ) > 1e-12f ? XMMatrixTranspose( inverse ) : linear;
}

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
}

void ConstantBuffers::beginFrame( const FrameParameters& frame )
{
	m_frame = frame;
}

void ConstantBuffers::setViewBuffer( const RenderView& view )
{
	// HLSL читает матрицы по столбцам: транспонирование даёт ту же запись mul( v, M ), что и в C++
	Device::updateResource<ShaderFrameConstant>( m_frameConstant, [&]( ShaderFrameConstant& data )
	{
		data.view = XMMatrixTranspose( view.view );
		data.projection = XMMatrixTranspose( view.projection );
		data.viewInverse = XMMatrixTranspose( view.viewInverse );
		data.viewProjection = XMMatrixTranspose( view.viewProjection );
		data.cameraPosition = view.position;
		data.viewDirection = view.direction;
		data.gameTime = m_frame.gameTime;
		data.deltaTime = m_frame.deltaTime;
		data.lightsCount = static_cast<float>( m_frame.lightsCount );
		data.lodOrigin = view.lodOrigin;
		data.skyLightScale = m_frame.skyLightScale;
		data.aerialPerspectiveDistance = m_frame.aerialPerspectiveDistance;
		data.aerialPerspectiveScale = m_frame.aerialPerspectiveScale;
		data.skyScale = m_frame.skyScale;
		const WindParameters& wind = m_frame.wind;
		data.windDirection = wind.direction;
		data.windStrength = wind.strength;
		data.windSpeed = wind.speed;
		data.windGustMin = wind.gustMin;
		data.windGustMax = wind.gustMax;
		data.windGustSize = wind.gustSize;
		data.framePadding = 0.0f;
	} );

	DMD3D::instance().setConstantBufferAllStages( SLOT_CB_FRAME, m_frameConstant );
}

void ConstantBuffers::setPerObjectBuffer( const XMMATRIX& world, float lodDither )
{
	// HLSL читает матрицы по столбцам: транспонирование даёт ту же запись mul( v, M ), что и в C++
	const XMMATRIX normals = normalMatrix( world );
	Device::updateResource<ShaderModelConstant>( m_modelConstant, [&]( ShaderModelConstant& data )
	{
		data.world = XMMatrixTranspose( world );
		data.worldInverseTranspose = XMMatrixTranspose( normals );
		data.lodDither = lodDither;
		data.padding = XMFLOAT3( 0.0f, 0.0f, 0.0f );
	} );

	// Всем графическим стадиям; compute-шейдеры объектов не рисуют
	for( SRVType stage : { SRVType::vs, SRVType::hs, SRVType::ds, SRVType::gs, SRVType::ps } )
		DMD3D::instance().setConstantBuffer( stage, SLOT_CB_OBJECT, m_modelConstant );
}

}