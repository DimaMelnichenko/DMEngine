#include "DMGrass.h"
#include "D3D\DMD3D.h"


DMGrass::DMGrass()
{
	m_max_grass_count = 5000;
}


DMGrass::~DMGrass()
{

}

bool DMGrass::Initialize( float patch_size )
{

	m_max_grass_count = floor( patch_size * patch_size );

	m_patch_width_size = patch_size;

	D3D11_BUFFER_DESC desc;
	memset( &desc, 0, sizeof( D3D11_BUFFER_DESC ) );
	desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_STREAM_OUTPUT;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.ByteWidth = m_max_grass_count * sizeof( GrassVertex );

	ID3D11Buffer* buffer;
	HRESULT hr = DMD3D::instance().GetDevice()->CreateBuffer( &desc, nullptr, &buffer );

	if( FAILED(hr) )
	{
		return false;
	}

	m_vertex_buffer = make_com_ptr<ID3D11Buffer>( buffer );

	return true;
}

void DMGrass::Generate()
{
	ID3D11Buffer* buffer[] = { nullptr };
	unsigned int stride = 0;
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, buffer, &stride, &stride );
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 1, 1, buffer, &stride, &stride );
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	buffer[0] = m_vertex_buffer.get();
	DMD3D::instance().GetDeviceContext()->SOSetTargets( 1, buffer, &stride );

}

void DMGrass::EndGenerate()
{
	ID3D11Buffer* buffer[] = { nullptr };
	unsigned int stride = 0;
	DMD3D::instance().GetDeviceContext()->SOSetTargets( 1, buffer, &stride );
}

void DMGrass::Render()
{
	ID3D11Buffer* buffer[] = { nullptr };	
	unsigned int offset = 0;
	buffer[0] = m_vertex_buffer.get();
	unsigned int stride = sizeof( GrassVertex );
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, buffer, &stride, &offset );

	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
}

unsigned int DMGrass::grassCount()
{
	return m_max_grass_count;
}

ID3D11Buffer* DMGrass::vertexBuffer()
{
	return m_vertex_buffer.get();
}

float DMGrass::grassMapSize()
{
	return m_patch_width_size;
}