#include "DMGrass.h"
#include "D3D\DMD3D.h"

namespace GS
{

DMGrass::DMGrass()
{

}


DMGrass::~DMGrass()
{

}

bool DMGrass::Initialize()
{

	if( !DMD3D::instance().createShaderConstantBuffer( sizeof( ArgsBuffer ), m_initArgsBuffer, nullptr ) )
		return false;


	D3D11_BUFFER_DESC desc;
	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( GrassVertex ) * m_maxVertexNum;
	desc.StructureByteStride = sizeof( GrassVertex );
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.CPUAccessFlags = 0;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_vertexBuffer ) )
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	viewDesc.Format = DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = 0;
	viewDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createSRV( m_vertexBuffer, viewDesc, m_srvVertex ) )
	{
		return false;
	}


	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createUAV( m_vertexBuffer, uavDesc, m_uavVertex ) )
	{
		return false;
	}

	///////////////***** args

	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS ;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 4 * 5;
	desc.StructureByteStride = 4;
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_argsBuffer ) )
		return false;

	viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	viewDesc.BufferEx.FirstElement = 0;
	viewDesc.BufferEx.NumElements = 5;
	viewDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if( !DMD3D::instance().createSRV( m_argsBuffer, viewDesc, m_srvArgs ) )
	{
		return false;
	}

	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = 5;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if( !DMD3D::instance().createUAV( m_argsBuffer, uavDesc, m_uavArgs ) )
	{
		return false;
	}

	if( !m_computeShader.Initialize( "Shaders\\grass.cs", "main" ) )
		return false;

	if( !m_initShader.Initialize( "Shaders\\grass.cs", "init" ) )
		return false;

	return true;
}

void DMGrass::Populate()
{
	m_computeShader.setUAVBuffer( 0, m_uavArgs.get() );
	m_computeShader.setUAVBuffer( 1, m_uavVertex.get() );

	//float clearValue[4] = {0.0,0.0,0.0,0.0};
	//DMD3D::instance().GetDeviceContext()->ClearUnorderedAccessViewFloat( m_uavVertex.get(), clearValue );

	m_computeShader.Dispatch( 316, 316, 0.0 );
}

void DMGrass::setInstanceParameters( uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset )
{
	Device::updateResource<ArgsBuffer>( m_initArgsBuffer.get(), [&]( ArgsBuffer& v )
	{
		v.indexCountPerInstance = indexCount;
		v.baseVertexLocation = vertexOffset;
		v.startIndexLocation = indexOffset;
		v.instanceCount = 0;
		v.startInstanceLocation = 0;
	} );

	DMD3D::instance().setConstantBuffer( SRVType::cs, 1, m_initArgsBuffer );

	m_initShader.setUAVBuffer( 0, m_uavArgs.get() );

	m_initShader.Dispatch( 1, 1, 0.0 );
}

void DMGrass::Render()
{
	ID3D11ShaderResourceView* srv = m_srvVertex.get();
	DMD3D::instance().GetDeviceContext()->VSSetShaderResources( 0, 1, &srv );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 0, NULL, 0, 0 );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( NULL, DXGI_FORMAT_R32_UINT, 0 );
	
}


ID3D11ShaderResourceView* DMGrass::vertexBuffer()
{
	return m_srvVertex.get();
}

ID3D11Buffer* DMGrass::indirectArgsBuffer()
{
	return m_argsBuffer.get();
}

}