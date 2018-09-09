#include "DMGrass.h"
#include "D3D\DMD3D.h"
#include "System.h"

namespace GS
{

DMGrass::DMGrass()
{

}

DMGrass::~DMGrass()
{

}

bool DMGrass::Initialize( uint32_t modelId )
{
	for( uint16_t i = 0; i < System::models().get( modelId )->lodCount(); ++i )
	{
		m_modelLODs.insert( { i, System::models().get( modelId )->getLodById( i ) } );

		ModelBuffers modelBuffer;
		if( !createBuffers( modelBuffer ) )
			return false;
		m_LODBuffer.insert( { i, std::move( modelBuffer ) } );
	}


	if( !m_computeShader.Initialize( "Shaders\\grass.cs", "main" ) )
		return false;

	if( !m_initShader.Initialize( "Shaders\\grass.cs", "init" ) )
		return false;

	return true;
}

bool DMGrass::createBuffers( ModelBuffers& modelBuffer )
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
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, modelBuffer.m_vertexBuffer ) )
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc;
	viewDesc.Format = DXGI_FORMAT_UNKNOWN;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	viewDesc.Buffer.FirstElement = 0;
	viewDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createSRV( modelBuffer.m_vertexBuffer, viewDesc, modelBuffer.m_srvVertex ) )
	{
		return false;
	}

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = m_maxVertexNum;
	if( !DMD3D::instance().createUAV( modelBuffer.m_vertexBuffer, uavDesc, modelBuffer.m_uavVertex ) )
	{
		return false;
	}

	///////////////***** args

	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = 4 * 5;
	desc.StructureByteStride = 4;
	desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, modelBuffer.m_argsBuffer ) )
		return false;

	viewDesc.Format = DXGI_FORMAT_R32_TYPELESS;;
	viewDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	viewDesc.BufferEx.FirstElement = 0;
	viewDesc.BufferEx.NumElements = 5;
	viewDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	if( !DMD3D::instance().createSRV( modelBuffer.m_argsBuffer, viewDesc, modelBuffer.m_srvArgs ) )
	{
		return false;
	}

	uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = 5;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
	if( !DMD3D::instance().createUAV( modelBuffer.m_argsBuffer, uavDesc, modelBuffer.m_uavArgs ) )
	{
		return false;
	}
}

void DMGrass::Populate()
{
	m_computeShader.setUAVBuffer( 0, m_LODBuffer[0].m_uavArgs.get() );
	m_computeShader.setUAVBuffer( 1, m_LODBuffer[1].m_uavArgs.get() );
	m_computeShader.setUAVBuffer( 2, m_LODBuffer[0].m_uavVertex.get() );
	m_computeShader.setUAVBuffer( 3, m_LODBuffer[1].m_uavVertex.get() );

	m_computeShader.Dispatch( 1024, 1024, 0.0 );
	//m_computeShader.Dispatch( 50, 50, 0.0 );
}

void DMGrass::setInstanceParameters( uint16_t lod, uint32_t indexCount, uint32_t indexOffset, uint32_t vertexOffset )
{
	Device::updateResource<ArgsBuffer>( m_initArgsBuffer.get(), [&]( ArgsBuffer& v )
	{
		v.indexCountPerInstance = indexCount;
		v.baseVertexLocation = vertexOffset;
		v.startIndexLocation = indexOffset;
		v.instanceCount = 0;
		v.startInstanceLocation = 0;
	} );

	DMD3D::instance().setConstantBuffer( SRVType::cs, 3, m_initArgsBuffer );

	m_initShader.setUAVBuffer( 0, m_LODBuffer[lod].m_uavArgs.get() );

	m_initShader.Dispatch( 1, 1, 0.0 );
}

void DMGrass::prerender()
{
	for( uint16_t i = 0; i < m_modelLODs.size(); ++i )
	{
		const DMModel::LodBlock* block = m_modelLODs[i];
		AbstractMesh* mesh = System::meshes().get( block->mesh ).get();
		setInstanceParameters( i, mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );
	}
	
	DMD3D::instance().setSRV( SRVType::cs, 0, System::textures().get( 7 )->srv() );
	DMD3D::instance().setSRV( SRVType::cs, 1, System::textures().get( 17 )->srv() );
	DMD3D::instance().setSRV( SRVType::cs, 2, System::textures().get( 19 )->srv() );

	Populate();
}

void DMGrass::Render( uint16_t lod, uint16_t instanceSlot )
{
	DMD3D::instance().setSRV( SRVType::vs, instanceSlot, m_LODBuffer[lod].m_srvVertex );
}

ID3D11ShaderResourceView* DMGrass::vertexBuffer( uint16_t lod )
{
	return m_LODBuffer[lod].m_srvVertex.get();
}

ID3D11Buffer* DMGrass::indirectArgsBuffer( uint16_t lod )
{
	return m_LODBuffer[lod].m_argsBuffer.get();
}

DMModel::LodBlock* DMGrass::lodBlock( uint16_t lod )
{
	return m_modelLODs[lod];
}

uint16_t DMGrass::lodCount()
{
	return m_LODBuffer.size();
}

}