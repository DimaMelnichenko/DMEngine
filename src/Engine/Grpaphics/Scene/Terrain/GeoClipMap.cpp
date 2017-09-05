#include "GeoClipMap.h"
#include "xresources.h"
#include "XShader.h"

Geoclipmap::Geoclipmap( ID3D11Device* _device ) : XRootDraw( _device )
{
	m_vertex_buffer = nullptr;
	m_index_buffer_full = nullptr;
	m_index_buffer_cut = nullptr;
	m_constant_buffer = nullptr;
}

Geoclipmap::~Geoclipmap( )
{

}

void Geoclipmap::elapsedTime( float )
{

}

bool Geoclipmap::init(  )
{
	HRESULT hr;
	D3D11_BUFFER_DESC bd;  // Структура, описывающая создаваемый буфер
	ZeroMemory( &bd, sizeof( bd ) );                    // очищаем ее

	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof( ConstantBuffer );
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = 0;
	bd.MiscFlags = 0;
	bd.StructureByteStride = 0;

	ConstantBuffer cb;
	ZeroMemory( &cb, sizeof( ConstantBuffer ) );
	cb.scale = 1.0f;

	D3D11_SUBRESOURCE_DATA subResData;
	ZeroMemory( &subResData, sizeof( D3D11_SUBRESOURCE_DATA ) );
	subResData.pSysMem = &cb;
	subResData.SysMemPitch = 0;
	subResData.SysMemSlicePitch = 0;

	hr = m_device->CreateBuffer( &bd, &subResData, &m_constant_buffer );
	if( FAILED( hr ) )
		return false;

	/////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<vertex> vertexes;
	vertex vert;
	vert.tex = D3DXVECTOR2( 0.0, 0.0 );

	int N = 15;

	for( size_t i = 0; i < N; i++ )
	{
		for( size_t j = 0; j < N; j++ )
		{
			vert.pos.x = i;
			vert.pos.y = 0.0f;
			vert.pos.z = j;
			vertexes.push_back( vert );
		}
	}

	unsigned long index_count = ( N - 1 ) * 6;
	std::vector<unsigned long> indexes;
	//indexes.reserve( index_count );
	std::vector<unsigned long> indexes_cut;
	//indexes_cut.reserve( index_count );

	int M = ( N + 1 ) / 4;

	for( unsigned long i = N; i < vertexes.size( ); i++ )
	{
		if( i % N )
		{
			indexes.push_back( i );
			indexes.push_back( i - 1 );			
			indexes.push_back( i - N - 1 );

			indexes.push_back( i );
			indexes.push_back( i - N - 1 );
			indexes.push_back( i - N );

			if( i % N > ( M - 1 ) && i % N < ( N + 1 - M ) && i / N >( M - 1 ) && i / N < ( N + 1 - M ) )
			{
				continue;
			}
			indexes_cut.push_back( i );
			indexes_cut.push_back( i - 1 );
			indexes_cut.push_back( i - N - 1 );

			indexes_cut.push_back( i );
			indexes_cut.push_back( i - N - 1 );
			indexes_cut.push_back( i - N );
		}
	}

	m_index_count_L = (( N + 1 ) - ( M * 2 ) ) * 2 * 6 - 6;

	m_index_count_full = indexes.size();
	m_index_count_cut = indexes_cut.size();

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );

	D3D11_SUBRESOURCE_DATA init_data;
	memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );

	m_vertex_count = vertexes.size();

	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.ByteWidth = sizeof(vertex) * vertexes.size();
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	init_data.pSysMem = &( vertexes[0] );	
	hr = XResources::inst( ).device( )->CreateBuffer( &buffer_desc, &init_data, &m_vertex_buffer );
	
	if( FAILED( hr ) )
	{
		Release();
		return false;
	}
	

	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = sizeof(unsigned long) * m_index_count_full;		

	init_data.pSysMem = &( indexes[0] );
	hr = XResources::inst( ).device( )->CreateBuffer( &buffer_desc, &init_data, &m_index_buffer_full );

	if( FAILED( hr ) )
	{
		Release( );
		return false;
	}

	buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	buffer_desc.ByteWidth = sizeof(unsigned long) * m_index_count_cut + m_index_count_L;
	init_data.pSysMem = &( indexes_cut[0] );
	hr = XResources::inst( ).device( )->CreateBuffer( &buffer_desc, &init_data, &m_index_buffer_cut );

	if( FAILED( hr ) )
	{
		Release( );
		return false;
	}
	
	GeoLevel level( 15, 0 );

	for( size_t i = 0; i < 2; i++ )
	{
		level.setLevel( i );
		//level.m_central_pos = D3DXVECTOR2( 0.0, 0.0 );
		m_levels.push_back( level );
	}

	return true;
}

bool Geoclipmap::PrepareRender( ID3D11DeviceContext* pImmediateContext )
{
	// Подключение шаблона вершин
	pImmediateContext->IASetInputLayout( XShader::inst( ).vertexLayout( vs_group ) );

	return true;
}

void Geoclipmap::Render( ID3D11DeviceContext* pImmediateContext )
{
	// Установка буфера вершин:
	UINT stride = sizeof( vertex );
	UINT offset = 0;
	// Установка способа отрисовки вершин в буфере
	pImmediateContext->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	pImmediateContext->IASetVertexBuffers( 0, 1, &m_vertex_buffer, &stride, &offset );

	ConstantBuffer cb;
	memset( &cb, 0, sizeof( ConstantBuffer ) );

	static D3DXVECTOR2 camera_pos( 0.0, 0.0 );

	for( GeoLevel& level : m_levels )
	{	
		cb.scale = level.scale();
		D3DXVECTOR2 pos = level.calcPos( D3DXVECTOR2( 2.1, 4.1 ) );
		cb.offset = XMFLOAT2( pos.x, pos.y );

		pImmediateContext->UpdateSubresource( m_constant_buffer, 0, NULL, &cb, 0, 0 );
		pImmediateContext->VSSetConstantBuffers( 2, 1, &m_constant_buffer );
		pImmediateContext->PSSetConstantBuffers( 2, 1, &m_constant_buffer );

		// Установка индексного буфера
		if( level.level() == 0 )
		{
			pImmediateContext->IASetIndexBuffer( m_index_buffer_full, DXGI_FORMAT_R32_UINT, 0 );
			pImmediateContext->DrawIndexed( m_index_count_full, 0, 0 );
		}
		else
		{
			fill_L_indexes( pImmediateContext );
			pImmediateContext->IASetIndexBuffer( m_index_buffer_cut, DXGI_FORMAT_R32_UINT, 0 );
			pImmediateContext->DrawIndexed( m_index_count_cut, 0, 0 );
		}

		//pImmediateContext->PSSetSamplers( 0, 1, &pSamplerLinear );

		
	}
	
}

void Geoclipmap::fill_L_indexes( ID3D11DeviceContext* pImmediateContext )
{
	int N = 15;
	int M = ( N + 1 ) / 4;

	unsigned int* indexes;

	D3D11_MAPPED_SUBRESOURCE ms;	
	pImmediateContext->Map( m_index_buffer_cut, 0, D3D11_MAP_WRITE_DISCARD, 0, &ms );

	int index = m_index_count_cut;

	indexes = (unsigned int*)ms.pData;

	for( unsigned long i = M; i < N - M + 1; i++ )
	{
		int row = ( N - M + 1 ) * N;
		indexes[index++] = ( row + i );
		indexes[index++] = ( row + i - 1 );
		indexes[index++] = ( row + i - N - 1 );
			   
		indexes[index++] = ( row + i );
		indexes[index++] = ( row + i - N - 1 );
		indexes[index++] = ( row + i - N );
	}

	pImmediateContext->Unmap( m_index_buffer_cut, 0 );
}

void Geoclipmap::Release( void )
{
	if( m_constant_buffer )
	{
		m_constant_buffer->Release();
		m_constant_buffer = nullptr;
	}

	if( m_index_buffer_cut )
	{
		m_index_buffer_cut->Release( );
		m_index_buffer_cut = nullptr;
	}

	if( m_index_buffer_full )
	{
		m_index_buffer_full->Release( );
		m_index_buffer_full = nullptr;
	}

	if( m_vertex_buffer )
	{
		m_vertex_buffer->Release();
		m_vertex_buffer = nullptr;
	}
}