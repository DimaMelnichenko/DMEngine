#include "DMOcean.h"


DMOcean::DMOcean(  ) : DMGeoClipMap( parent )
{
}


DMOcean::~DMOcean()
{
}

void DMOcean::Render( std::unique_ptr<ID3D11DeviceContext>& _device_context, std::unique_ptr<DMOceanShader>& _shader, DMCamera* _camera, float _time )
{
	// Установка буфера вершин:
	UINT stride = sizeof( DMGeoClipMap::vertex );
	UINT offset = 0;

	// Установка способа отрисовки вершин в буфере
	_device_context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	_device_context->IASetVertexBuffers( 0, 1, &m_vertex_buffer, &stride, &offset );

	DMOceanShader::VSParamBuffer vs_geo_param;
	memset( &vs_geo_param, 0, sizeof( DMOceanShader::VSParamBuffer ) );

	DMOceanShader::PSParamBuffer ps_geo_param;
	memset( &ps_geo_param, 0, sizeof( DMOceanShader::PSParamBuffer ) );

	D3DXMATRIX worldMatrix;
	D3DXMatrixIdentity( &worldMatrix );

	static D3DXVECTOR2 camera_pos( 0.0, 0.0 );
	camera_pos.x = _camera->position().x;
	camera_pos.y = _camera->position().z;

	_shader->Prepare( _camera, 0 );

	for( size_t i = 0; i < m_levels.size( ); ++i )
	{
		DMGeoLevel& level = m_levels[i];

		vs_geo_param.map_scale = level.scale( );
		vs_geo_param.map_offset = level.calcPos( camera_pos );
		vs_geo_param.map_N = static_cast<float>( m_N );
		vs_geo_param.map_texture_scale = 0.01f;
		vs_geo_param.map_height_multippler = 1.0f;
		vs_geo_param.map_time = _time;

		ps_geo_param.app_time = _time;

		_shader->SetParameters( &vs_geo_param, &ps_geo_param );

		// Установка индексного буфера
		if( level.level( ) == 0 )
		{
			_device_context->IASetIndexBuffer( m_index_buffer_full, DXGI_FORMAT_R32_UINT, 0 );
			_shader->Render( m_index_count_full, &worldMatrix );
		}
		else
		{
			fill_L_indexes( &camera_pos, level, m_levels[i - 1] );
			_device_context->IASetIndexBuffer( m_index_buffer_cut, DXGI_FORMAT_R32_UINT, 0 );
			_shader->Render( m_index_count_cut + m_index_count_L, &worldMatrix );

		}
	}
}