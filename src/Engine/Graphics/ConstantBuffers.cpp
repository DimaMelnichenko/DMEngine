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

void ConstantBuffers::setViewBuffer( const RenderView& view, const FrameParameters& frame )
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
		data.gameTime = frame.gameTime;
		data.deltaTime = frame.deltaTime;
		data.lightCount = static_cast<float>( frame.lightsCount );
		data.lodOrigin = view.lodOrigin;
		data.skyLightScale = frame.skyLightScale;
		data.aerialPerspectiveDistance = frame.aerialPerspectiveDistance;
		data.aerialPerspectiveScale = frame.aerialPerspectiveScale;
		data.skyScale = frame.skyScale;
		const WindParameters& wind = frame.wind;
		data.windDirection = wind.direction;
		data.windStrength = wind.strength;
		data.windSpeed = wind.speed;
		data.windGustMin = wind.gustMin;
		data.windGustMax = wind.gustMax;
		data.windGustSize = wind.gustSize;
		data.windFieldScale = wind.fieldScale;
		const FogParameters& fog = frame.fog;
		data.fogLayer0 = fog.layer0;
		data.fogLayer1 = fog.layer1;
		data.fogAlbedo = fog.albedo;
		data.fogScale = fog.scale;
		data.fogGridZ = fog.gridZ;
		data.fogVolumeDistance = fog.volumeDistance;
		data.cloudShadow = frame.cloudShadow;
		data.waterDetail = frame.waterDetail;
		data.foliageInteraction = frame.foliageInteraction;
		data.ambientOcclusion = frame.ambientOcclusion;
		for( uint32_t i = 0; i < 2; ++i )
		{
			data.reflectionCaptureSphere[i] = frame.reflectionCaptureSphere[i];
			data.reflectionCapture[i] = frame.reflectionCapture[i];
		}
		data.terrain = frame.terrain;
	} );

	DMD3D::instance().setConstantBuffer( SLOT_CB_FRAME, m_frameConstant );
}

void ConstantBuffers::setPerObjectBuffer( const XMMATRIX& world, float lodDither, uint32_t boneOffset )
{
	// HLSL читает матрицы по столбцам: транспонирование даёт ту же запись mul( v, M ), что и в C++
	const XMMATRIX normals = normalMatrix( world );
	Device::updateResource<WorldBuffer>( m_modelConstant, [&]( WorldBuffer& data )
	{
		data.worldMatrix = XMMatrixTranspose( world );
		data.worldInverseTransposeMatrix = XMMatrixTranspose( normals );
		data.lodDither = lodDither;
		data.boneOffset = boneOffset;
		data.objectPadding = XMFLOAT2( 0.0f, 0.0f );
	} );

	DMD3D::instance().setConstantBuffer( SLOT_CB_OBJECT, m_modelConstant );
}

}