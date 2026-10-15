#include "ConstantBuffers.h"
#include "Shaders\slots.h"
#include "D3D\DMD3D.h"

using namespace DirectX;

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
	DMD3D::instance().createShaderConstantBuffer( sizeof( FrameConstantBuffer ), m_frameConstant );
	DMD3D::instance().createShaderConstantBuffer( sizeof( WorldBuffer ), m_modelConstant );
}

void ConstantBuffers::beginFrame( const FrameParameters& frame )
{
	m_frame = frame;
}

void ConstantBuffers::setViewBuffer( const RenderView& view )
{
	// HLSL читает матрицы по столбцам: транспонирование даёт ту же запись mul( v, M ), что и в C++
	Device::updateResource<FrameConstantBuffer>( m_frameConstant, [&]( FrameConstantBuffer& data )
	{
		data.viewMatrix = XMMatrixTranspose( view.view );
		data.projectionMatrix = XMMatrixTranspose( view.projection );
		data.viewInverseMatrix = XMMatrixTranspose( view.viewInverse );
		data.viewProjectionMatrix = XMMatrixTranspose( view.viewProjection );
		data.cameraPosition = view.position;
		data.viewDirection = view.direction;
		data.gameTime = m_frame.gameTime;
		data.deltaTime = m_frame.deltaTime;
		data.lightCount = static_cast<float>( m_frame.lightsCount );
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
		data.windFieldScale = wind.fieldScale;
		const FogParameters& fog = m_frame.fog;
		data.fogLayer0 = fog.layer0;
		data.fogLayer1 = fog.layer1;
		data.fogAlbedo = fog.albedo;
		data.fogScale = fog.scale;
		data.fogGridZ = fog.gridZ;
		data.fogVolumeDistance = fog.volumeDistance;
		data.cloudShadow = m_frame.cloudShadow;
		data.waterDetail = m_frame.waterDetail;
		data.foliageInteraction = m_frame.foliageInteraction;
		data.ambientOcclusion = m_frame.ambientOcclusion;
	} );

	DMD3D::instance().setConstantBuffer( SLOT_CB_FRAME, m_frameConstant );
}

void ConstantBuffers::setPerObjectBuffer( const XMMATRIX& world, float lodDither )
{
	// HLSL читает матрицы по столбцам: транспонирование даёт ту же запись mul( v, M ), что и в C++
	const XMMATRIX normals = normalMatrix( world );
	Device::updateResource<WorldBuffer>( m_modelConstant, [&]( WorldBuffer& data )
	{
		data.worldMatrix = XMMatrixTranspose( world );
		data.worldInverseTransposeMatrix = XMMatrixTranspose( normals );
		data.lodDither = lodDither;
		data.objectPadding = XMFLOAT3( 0.0f, 0.0f, 0.0f );
	} );

	DMD3D::instance().setConstantBuffer( SLOT_CB_OBJECT, m_modelConstant );
}

}