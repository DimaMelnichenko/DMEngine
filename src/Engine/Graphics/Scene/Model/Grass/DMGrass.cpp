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

	ID3D11ShaderResourceView* view;
	if( FAILED( DMD3D::instance().GetDevice()->CreateShaderResourceView( m_vertexBuffer.get(), &viewDesc, &view ) ) )
	{
		return false;
	}

	m_srvVertex = make_com_ptr<ID3D11ShaderResourceView>( view );


	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.FirstElement = 0;
	uavDesc.Buffer.NumElements = m_maxVertexNum;

	ID3D11UnorderedAccessView* uavView;
	if( FAILED( DMD3D::instance().GetDevice()->CreateUnorderedAccessView( m_vertexBuffer.get(), &uavDesc, &uavView ) ) )
	{
		return false;
	}

	m_uavVertex = make_com_ptr<ID3D11UnorderedAccessView>( uavView );



	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = sizeof( ArgsBuffer );
	desc.StructureByteStride = sizeof( ArgsBuffer );
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	if( !DMD3D::instance().CreateBuffer( &desc, nullptr, m_indirectBuffer ) )
		return false;

	return true;
}

void DMGrass::Populate()
{

}

void DMGrass::Render()
{
	/*ID3D11Buffer* buffer[] = { nullptr };
	unsigned int offset = 0;
	buffer[0] = m_vertex_buffer.get();
	unsigned int stride = sizeof( GrassVertex );
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, buffer, &stride, &offset );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
	*/
}


ID3D11ShaderResourceView* DMGrass::vertexBuffer()
{
	return m_srvVertex.get();
}

}