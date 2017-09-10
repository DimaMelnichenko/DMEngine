
#include "DMGeoClipMap.h"



DMGeoClipMap::DMGeoClipMap( DMD3D* parent ) : DMSceneObject( parent )
{
	m_N = 15;
	
}

DMGeoClipMap::~DMGeoClipMap( )
{

}

bool DMGeoClipMap::Initialize( int N, int _levels, float scale, float height )
{
	if( N % 2 && N < 15 )
		return false;

	m_N = N;

	m_M = ( m_N + 1 ) / 4;

	m_MxM_offsets.push_back( D3DXVECTOR2( 0.0, 0.0 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 0.0, m_M - 1 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 0.0, 2 * m_M  ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 0.0, 3 * m_M - 1 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( m_M - 1, 0.0 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 2 * m_M, 0.0 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 3 * m_M - 1, 0.0 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 3 * m_M - 1, m_M - 1 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 3 * m_M - 1, 2 * m_M ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 3 * m_M - 1, 3 * m_M - 1 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( m_M - 1, 3 * m_M - 1 ) );
	m_MxM_offsets.push_back( D3DXVECTOR2( 2 * m_M, 3 * m_M - 1 ) );


	m_Mx3_offsets.push_back( D3DXVECTOR2( 2 * m_M - 2, 0.0 ) );
	m_Mx3_offsets.push_back( D3DXVECTOR2( 2 * m_M - 2, 3 * m_M - 1 ) );
	m_Mx3_offsets.push_back( D3DXVECTOR2( 0.0, 2 * m_M - 2 ) );
	m_Mx3_offsets.push_back( D3DXVECTOR2( 3 * m_M - 1, 2 * m_M - 2 ) );

	//--------------------------------------------------------------------

	m_scale_multipler = 1.0f / scale;
	m_height_multipler = height;

	if( !InitializeBuffers() )
	{		
		return false;
	}

	if( !inint_MxM_blocks() )
		return false;

	if( !inint_Mx3_blocks() )
		return false;

	if( !inint_2M1_blocks() )
		return false;

	DMGeoLevel level( m_N, 0 );

	for( size_t i = 0; i < _levels; i++ )
	{
		level.setLevel( i );
		m_levels.push_back( level );
	}

	return true;
}

bool DMGeoClipMap::InitializeBuffers( )
{
	HRESULT hr;

	/////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////////////////////////////////////////////////////////////////////////////
	std::vector<vertex> vertexes;
	vertex vert;
	vert.tex = D3DXVECTOR2( 0.0, 0.0 );

	for( size_t i = 0; i < m_N; i++ )
	{
		for( size_t j = 0; j < m_N; j++ )
		{
			vert.pos.x = i;
			vert.pos.y = 0.0f;
			vert.pos.z = j;
			vert.tex.x = (float)i / m_N;
			vert.tex.y = (float)j / m_N;
			vertexes.push_back( vert );
		}
	}

	unsigned long index_count = ( m_N - 1 ) * 6;
	std::vector<unsigned long> indexes;
	//indexes.reserve( index_count );	
	//indexes_cut.reserve( index_count );

	int M = ( m_N + 1 ) / 4;

	for( unsigned long i = m_N; i < vertexes.size( ); i++ )
	{
		if( i % m_N )
		{
			indexes.push_back( i );
			indexes.push_back( i - 1 );
			indexes.push_back( i - m_N - 1 );

			indexes.push_back( i );
			indexes.push_back( i - m_N - 1 );
			indexes.push_back( i - m_N );

			if( i % m_N > ( M - 1 ) && i % m_N < ( m_N + 1 - M ) && i / m_N >( M - 1 ) && i / m_N < ( m_N + 1 - M ) )
			{
				continue;
			}

			m_indexes_cut.push_back( i );
			m_indexes_cut.push_back( i - 1 );
			m_indexes_cut.push_back( i - m_N - 1 );

			m_indexes_cut.push_back( i );
			m_indexes_cut.push_back( i - m_N - 1 );
			m_indexes_cut.push_back( i - m_N );
		}
	}

	m_index_count_L = ( ( m_N + 1 ) - ( M * 2 ) ) * 2 * 6 - 6;

	m_index_count_full = indexes.size( );
	m_index_count_cut = m_indexes_cut.size( );

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );

	D3D11_SUBRESOURCE_DATA init_data;
	memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );

	m_vertex_count = vertexes.size( );

	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.ByteWidth = sizeof(vertex)* vertexes.size( );
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	init_data.pSysMem = &( vertexes[0] );

	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_vertex_buffer ) )
		return false;


	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = sizeof(unsigned long) * m_index_count_full;
	init_data.pSysMem = &( indexes[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_index_buffer_full ) )
		return false;
	

	buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
	buffer_desc.ByteWidth = sizeof(unsigned long) * ( m_index_count_cut + m_index_count_L );
	init_data.pSysMem = &( m_indexes_cut[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_index_buffer_cut ) )
		return false;

	return true;
}

void DMGeoClipMap::Render( DMClipMapShader* shader, DMCamera* camera, DMFrustum* frustum )
{
	// Установка буфера вершин:
	UINT stride = sizeof( vertex );
	UINT offset = 0;

	// Установка способа отрисовки вершин в буфере
	m_dmd3d->GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	// new clipmap

	D3DXMATRIX worldMatrix;
	D3DXMatrixIdentity( &worldMatrix );

	DMClipMapShader::ParamBuffer geo_param;
	memset( &geo_param, 0, sizeof( DMClipMapShader::ParamBuffer ) );

	static D3DXVECTOR2 camera_pos( 0.0, 0.0 );
	camera_pos.x = camera->position().x;
	camera_pos.y = camera->position().z;

	shader->Prepare( camera, shader->phase() );

	shader->setDrawType( DMShader::by_index );

	//geo_param.map_scale = m_levels[0].scale();
	//geo_param.map_offset = m_levels[0].calcPos( camera_pos );
	geo_param.map_N = m_N;
	geo_param.map_texture_scale = m_scale_multipler;
	geo_param.map_height_multippler = m_height_multipler;

	shader->SetVSParameters( &geo_param );


	static std::vector<DMClipMapShader::InstanceOffset> offsets;
	offsets.clear();
	static std::vector<DMClipMapShader::InstanceOffset> Mx3_vertical_offsets;
	Mx3_vertical_offsets.clear();
	static std::vector<DMClipMapShader::InstanceOffset> Mx3_horizontal_offsets;
	Mx3_horizontal_offsets.clear();
	static std::vector<DMClipMapShader::InstanceOffset> _2M1_vertical_offsets;
	_2M1_vertical_offsets.clear();
	static std::vector<DMClipMapShader::InstanceOffset> _2M1_horizontal_offsets;
	_2M1_horizontal_offsets.clear();

	// draw main patch

	DMClipMapShader::InstanceOffset instance_offset;
	

	instance_offset.level_pos = m_levels[0].calcPos( camera_pos );
	instance_offset.scale = m_levels[0].scale();	
	instance_offset.offset = D3DXVECTOR2( 0.0f, 0.0f );

	offsets.push_back( instance_offset );

	shader->setInstanceOffset( offsets );

	ID3D11Buffer* buffer = m_vertex_buffer.get();
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
	m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_index_buffer_full.get(), DXGI_FORMAT_R32_UINT, 0 );
	shader->RenderInstanced( m_index_count_full, 1, &worldMatrix );

	// draw low levels

	offsets.clear();

	


	
	for( int level_index = 1; level_index < m_levels.size(); ++level_index )
	{
		DMGeoLevel& level = m_levels[level_index];

		D3DXVECTOR2 level_pos = level.calcPos( camera_pos );

		instance_offset.level_pos = level_pos;

		// MxM offsets
		for( int offset_index = 0; offset_index < 12; ++offset_index )
		{	
			//create frustum culling for patches
			

			static DMAABB aabb_original;
			static DMAABB aabb_offset;

			aabb_original.CreateAABB( D3DXVECTOR3( m_M, 100, m_M ), D3DXVECTOR3( m_M, 100, m_M ) );
			aabb_offset = aabb_original;
			aabb_offset.setScale( level.scale(), level.scale(), level.scale() );
			D3DXVECTOR2 offset = level_pos + m_MxM_offsets[offset_index] * level.scale();
			aabb_offset.setPosition( offset.x, 0.5, offset.y );

			if( !frustum->CheckAABB( &aabb_offset ) )
				continue;

			
			instance_offset.offset = m_MxM_offsets[offset_index];
			instance_offset.scale = level.scale();

			offsets.push_back( instance_offset );

		}
		
		// Mx3 vertical offsets
		for( int Mx3_offset_index = 0; Mx3_offset_index < 2; ++Mx3_offset_index )
		{
			D3DXVECTOR2 offset = level_pos + m_Mx3_offsets[Mx3_offset_index] * level.scale();

			instance_offset.offset = m_Mx3_offsets[Mx3_offset_index];
			instance_offset.scale = level.scale();

			Mx3_vertical_offsets.push_back( instance_offset );
		}

		// Mx3 horizontal offsets
		for( int Mx3_offset_index = 2; Mx3_offset_index < m_Mx3_offsets.size(); ++Mx3_offset_index )
		{
			D3DXVECTOR2 offset = level_pos + m_Mx3_offsets[Mx3_offset_index] * level.scale();

			instance_offset.offset = m_Mx3_offsets[Mx3_offset_index];
			instance_offset.scale = level.scale();

			Mx3_horizontal_offsets.push_back( instance_offset );
		}
		
		// 2M1 offsets
		float dx = level_pos.x - m_levels[level_index - 1].calcPos( camera_pos ).x;
		float dy = level_pos.y - m_levels[level_index - 1].calcPos( camera_pos ).y;
		int calc_M_x = static_cast<int>( abs( floor( dx / level.scale() ) ) );
		int calc_M_y = static_cast<int>( abs( floor( dy / level.scale() ) ) );

		D3DXVECTOR2 offset;

		instance_offset.offset = D3DXVECTOR2( 0.0f, 0.0f );
		
		if( calc_M_x == m_M )
		{
			if( calc_M_y == m_M )
			{
				// Lower Left L Shape
				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
			else
			{
				// Upper Left L Shape
				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, 3 * m_M - 2 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, 3 * m_M - 2 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
		}
		else
		{
			if( calc_M_y == m_M )
			{
				// Lower Right L Shape
				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );
				

				//instance_offset.offset = level_pos + D3DXVECTOR2( 3 * m_M - 2, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( 3 * m_M - 2, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
			else
			{
				// Upper Right L Shape 
				//instance_offset.offset = level_pos + D3DXVECTOR2( m_M - 1, 3 * m_M - 2 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( m_M - 1, 3 * m_M - 2 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + D3DXVECTOR2( 3 * m_M - 2, m_M - 1 ) * level.scale();
				instance_offset.offset = D3DXVECTOR2( 3 * m_M - 2, m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
				
			}
		}
		
	}

	

	buffer = m_MxM_vertex_buffer.get();
	m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
	
	if( offsets.size() )
	{	
		m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_MxM_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );
		shader->setInstanceOffset( offsets );
		shader->RenderInstanced( m_MxM_index_count, offsets.size(), &worldMatrix );
	}
	
	if( Mx3_vertical_offsets.size() )
	{	
		m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_Mx3_vertical_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );

		shader->setInstanceOffset( Mx3_vertical_offsets );

		shader->RenderInstanced( m_Mx3_index_count, Mx3_vertical_offsets.size(), &worldMatrix );
	}

	if( Mx3_horizontal_offsets.size() )
	{
		m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_Mx3_horizontal_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );

		shader->setInstanceOffset( Mx3_horizontal_offsets );

		shader->RenderInstanced( m_Mx3_index_count, Mx3_horizontal_offsets.size(), &worldMatrix );
	}

	

	if( _2M1_horizontal_offsets.size() )
	{
		buffer = m_2M1_horizontal_vertex_buffer.get();
		m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

		m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_2M1_horizontal_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );

		shader->setInstanceOffset( _2M1_horizontal_offsets );

		shader->RenderInstanced( m_2M1_horizontal_index_count, _2M1_horizontal_offsets.size(), &worldMatrix );
	}

	if( _2M1_vertical_offsets.size() )
	{
		buffer = m_2M1_vertical_vertex_buffer.get();
		m_dmd3d->GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );

		m_dmd3d->GetDeviceContext()->IASetIndexBuffer( m_2M1_vertical_index_buffer.get(), DXGI_FORMAT_R32_UINT, 0 );

		shader->setInstanceOffset( _2M1_vertical_offsets );

		shader->RenderInstanced( m_2M1_vertical_index_count, _2M1_vertical_offsets.size(), &worldMatrix );
	}
	
}

void DMGeoClipMap::fill_L_indexes( D3DXVECTOR2* _cam_pos, DMGeoLevel& _current_level, DMGeoLevel& _prev_level )
{	
	int M = ( m_N + 1 ) / 4;

	unsigned int* indexes;

	D3D11_MAPPED_SUBRESOURCE ms;

	m_dmd3d->GetDeviceContext()->Map( m_index_buffer_cut.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms );

	unsigned int index = m_index_count_cut;

	memcpy( ms.pData, &m_indexes_cut[0], sizeof(unsigned int)* m_indexes_cut.size( ) );

	indexes = (unsigned int*)ms.pData;

	int row;

	float dx = _current_level.calcPos( *_cam_pos ).x - _prev_level.calcPos( *_cam_pos ).x;
	float dy = _current_level.calcPos( *_cam_pos ).y - _prev_level.calcPos( *_cam_pos ).y;
	int calc_M_x = abs( floor( dx / _current_level.scale( ) ) );
	int calc_M_y = abs( floor( dy / _current_level.scale( ) ) );

	if( calc_M_x == M )
	{
		if( calc_M_y == M )
		{
			// Upper Left L Shape 
			// Up 
			row = M;
			fill_row_hor( row, M + 1, m_N - M + 1, indexes, index );

			// Left 
			row = M;
			fill_row_vert( row, M, m_N - M + 1, indexes, index );
		}
		else
		{
			// Lower Left L Shape 
			// Left 
			row = M;
			fill_row_vert( row, M, m_N - M + 1, indexes, index );

			// Bottom 
			row = m_N - M;
			fill_row_hor( row, M + 1, m_N - M + 1, indexes, index );
		}
	}
	else
	{
		if( calc_M_y == M )
		{
			// Upper Right L Shape 
			// Up 
			row = M;
			fill_row_hor( row, M, m_N - M, indexes, index );

			// Right 
			row = m_N - M;
			fill_row_vert( row, M, m_N - M + 1, indexes, index );
		}
		else
		{
			// Lower Right L Shape 
			// Right 
			row = m_N - M;
			fill_row_vert( row, M, m_N - M + 1, indexes, index );

			// Bottom 
			row = m_N - M;
			fill_row_hor( row, M, m_N - M, indexes, index );
		}
	}

	m_dmd3d->GetDeviceContext()->Unmap( m_index_buffer_cut.get(), 0 );
}

void DMGeoClipMap::fill_row_vert( int _row, int _start, int _end, unsigned int* _indexes, unsigned int& index )
{	
	_row *= m_N;

	for( unsigned long i = _start; i < _end; i++ )
	{
		_indexes[index++] = ( _row + i );
		_indexes[index++] = ( _row + i - 1 );
		_indexes[index++] = ( _row + i - m_N - 1 );

		_indexes[index++] = ( _row + i );
		_indexes[index++] = ( _row + i - m_N - 1 );
		_indexes[index++] = ( _row + i - m_N );
	}
}

void DMGeoClipMap::fill_row_hor( int _row, int _start, int _end, unsigned int* _indexes, unsigned int& index )
{
	for( unsigned long i = _start; i < _end; i++ )
	{
		_indexes[index++] = ( _row + i * m_N );
		_indexes[index++] = ( _row + i * m_N - 1 );
		_indexes[index++] = ( _row + i * m_N - m_N - 1 );

		_indexes[index++] = ( _row + i * m_N );
		_indexes[index++] = ( _row + i * m_N - m_N - 1 );
		_indexes[index++] = ( _row + i * m_N - m_N );
	}
}

bool DMGeoClipMap::inint_MxM_blocks()
{
	std::vector<vertex> vertexes;
	vertex vert;
	vert.tex = D3DXVECTOR2( 0.0, 0.0 );

	for( size_t i = 0; i < m_M; i++ )
	{
		for( size_t j = 0; j < m_M; j++ )
		{
			vert.pos.x = i;
			vert.pos.y = 0.0f;
			vert.pos.z = j;
			vert.tex.x = (float)i / m_M;
			vert.tex.y = (float)j / m_M;
			vertexes.push_back( vert );
		}
	}

	
	std::vector<unsigned long> indexes;
	//indexes.reserve( index_count );	
	//indexes_cut.reserve( index_count );

	for( unsigned long i = m_M; i < vertexes.size(); i++ )
	{
		if( i % m_M )
		{
			indexes.push_back( i );
			indexes.push_back( i - 1 );
			indexes.push_back( i - m_M - 1 );

			indexes.push_back( i );
			indexes.push_back( i - m_M - 1 );
			indexes.push_back( i - m_M );
		}
	}

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );

	D3D11_SUBRESOURCE_DATA init_data;
	memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );

	m_MxM_vertex_count = vertexes.size();

	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.ByteWidth = sizeof( vertex )* vertexes.size();
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	init_data.pSysMem = &( vertexes[0] );

	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_MxM_vertex_buffer ) )
		return false;

	m_MxM_index_count = indexes.size();

	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = sizeof( unsigned long ) * indexes.size();
	init_data.pSysMem = &( indexes[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_MxM_index_buffer ) )
		return false;

	return true;
}

bool DMGeoClipMap::inint_Mx3_blocks()
{
	std::vector<unsigned long> indexes_horizontal;
	std::vector<unsigned long> indexes_verical;
	//indexes.reserve( index_count );	
	//indexes_cut.reserve( index_count );

	for( unsigned long i = m_M; i < m_M * 3; i++ )
	{
		if( i % m_M )
		{
			indexes_verical.push_back( i );
			indexes_verical.push_back( i - 1 );
			indexes_verical.push_back( i - m_M - 1 );

			indexes_verical.push_back( i );
			indexes_verical.push_back( i - m_M - 1 );
			indexes_verical.push_back( i - m_M );
		}
	}

	for( unsigned long i = m_M; i < m_vertex_count; i++ )
	{
		if( i % m_M && ( i % m_M < 3 ) )
		{
			indexes_horizontal.push_back( i );
			indexes_horizontal.push_back( i - 1 );
			indexes_horizontal.push_back( i - m_M - 1 );

			indexes_horizontal.push_back( i );
			indexes_horizontal.push_back( i - m_M - 1 );
			indexes_horizontal.push_back( i - m_M );
		}
	}

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );

	D3D11_SUBRESOURCE_DATA init_data;
	memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );

	m_Mx3_index_count = indexes_verical.size();

	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = sizeof( unsigned long ) * m_Mx3_index_count;
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	init_data.pSysMem = &( indexes_verical[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_Mx3_vertical_index_buffer ) )
		return false;

	
	init_data.pSysMem = &( indexes_horizontal[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_Mx3_horizontal_index_buffer ) )
		return false;

	return true;
}

bool DMGeoClipMap::inint_2M1_blocks()
{
	std::vector<unsigned long> indexes_horizontal;
	std::vector<unsigned long> indexes_verical;
	//indexes.reserve( index_count );	
	//indexes_cut.reserve( index_count );

	unsigned int length_2M1 = 2 * m_M + 1;

	std::vector<vertex> vertexes_horizontal;
	std::vector<vertex> vertexes_verical;
	vertex vert;
	vert.tex = D3DXVECTOR2( 0.0, 0.0 );
	vert.pos.y = 0.0f;


	for( size_t i = 0; i < length_2M1; i++ )
	{
		//horizontal
		vert.pos.x = (float)i;		
		vert.pos.z = (float)0.0f;
		vertexes_horizontal.push_back( vert );
		vert.pos.x = (float)i;
		vert.pos.z = ( float )1.0f;
		vertexes_horizontal.push_back( vert );

		//vertical
		vert.pos.x = (float)0.0;
		vert.pos.z = ( float )i;
		vertexes_verical.push_back( vert );
		vert.pos.x = (float)1.0;
		vert.pos.z = ( float )i;
		vertexes_verical.push_back( vert );
	}


	for( unsigned long i = 0; i < length_2M1 * 2 - 2; i += 2 )
	{	
		indexes_horizontal.push_back( i + 2 );
		indexes_horizontal.push_back( i );
		indexes_horizontal.push_back( i + 1 );

		indexes_horizontal.push_back( i + 1 );
		indexes_horizontal.push_back( i + 3 );
		indexes_horizontal.push_back( i + 2);

		indexes_verical.push_back( i );
		indexes_verical.push_back( i + 2 );
		indexes_verical.push_back( i + 1 );

		indexes_verical.push_back( i + 1 );
		indexes_verical.push_back( i + 2 );
		indexes_verical.push_back( i + 3 );
		
	}

	D3D11_BUFFER_DESC buffer_desc;
	memset( &buffer_desc, 0, sizeof( D3D11_BUFFER_DESC ) );

	D3D11_SUBRESOURCE_DATA init_data;
	memset( &init_data, 0, sizeof( D3D11_SUBRESOURCE_DATA ) );

	//create verices buffer

	m_2M1_horizontal_vertex_count = vertexes_horizontal.size();
	m_2M1_vertical_vertex_count = vertexes_verical.size();

	buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	buffer_desc.ByteWidth = sizeof( vertex )* vertexes_horizontal.size();
	buffer_desc.CPUAccessFlags = 0;
	buffer_desc.Usage = D3D11_USAGE_DEFAULT;
	init_data.pSysMem = &( vertexes_horizontal[0] );

	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_2M1_horizontal_vertex_buffer ) )
		return false;

	init_data.pSysMem = &( vertexes_verical[0] );

	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_2M1_vertical_vertex_buffer ) )
		return false;


	//create indexces buffer
	m_2M1_horizontal_index_count = indexes_horizontal.size();
	m_2M1_vertical_index_count = indexes_verical.size();

	buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	buffer_desc.ByteWidth = sizeof( unsigned long ) * indexes_horizontal.size();
	init_data.pSysMem = &( indexes_horizontal[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_2M1_horizontal_index_buffer ) )
		return false;


	init_data.pSysMem = &( indexes_verical[0] );
	if( !m_dmd3d->CreateBuffer( &buffer_desc, &init_data, m_2M1_vertical_index_buffer ) )
		return false;

	return true;
}

float DMGeoClipMap::scaleMultipler()
{
	return m_scale_multipler;
}

float DMGeoClipMap::heightMultipler()
{
	return m_height_multipler;
}