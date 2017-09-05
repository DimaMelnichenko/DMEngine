#include "DMGrass.h"



DMGrass::DMGrass( DMD3D* parent ) : DMSceneObject( parent )
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
	HRESULT hr = m_dmd3d->GetDevice()->CreateBuffer( &desc, nullptr, &buffer );

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
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, buffer, &stride, &stride );
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 1, 1, buffer, &stride, &stride );
	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );

	buffer[0] = m_vertex_buffer.get();
	m_dmd3d->GetDeviceContext()->SOSetTargets( 1, buffer, &stride );

}

void DMGrass::EndGenerate()
{
	ID3D11Buffer* buffer[] = { nullptr };
	unsigned int stride = 0;
	m_dmd3d->GetDeviceContext()->SOSetTargets( 1, buffer, &stride );
}

void DMGrass::Render()
{
	ID3D11Buffer* buffer[] = { nullptr };	
	unsigned int offset = 0;
	buffer[0] = m_vertex_buffer.get();
	unsigned int stride = sizeof( GrassVertex );
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, buffer, &stride, &offset );

	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_POINTLIST );
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