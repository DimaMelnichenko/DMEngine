
#include "DMGeoClipMap.h"
#include "Pipeline.h"



DMGeoClipMap::DMGeoClipMap()
{
	
}

DMGeoClipMap::~DMGeoClipMap( )
{

}

bool DMGeoClipMap::Initialize( int N, uint16_t _levels, float mapSize, float widthMultipler, float height )
{
	m_mapSize = mapSize;
	m_widthMultipler = widthMultipler;
	m_height_multipler = height;


	if( N % 2 && N < 15 )
		return false;

	m_N = N;

	m_M = ( m_N + 1 ) / 4;

	//_levels = 1024 / m_M;

	m_MxM_offsets.push_back( XMFLOAT2( 0.0f, 0.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 0.0f, (float)m_M - 1.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 0.0f, 2.0f * (float)m_M  ) );
	m_MxM_offsets.push_back( XMFLOAT2( 0.0f, 3.0f * (float)m_M - 1.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( (float)m_M - 1.0f, 0.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 2.0f * (float)m_M, 0.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 3.0f * (float)m_M - 1.0f, 0.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 3.0f * (float)m_M - 1.0f, (float)m_M - 1.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 3.0f * (float)m_M - 1.0f, 2.0f * m_M ) );
	m_MxM_offsets.push_back( XMFLOAT2( 3.0f * (float)m_M - 1.0f, 3.0f * m_M - 1.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( (float)m_M - 1.0f, 3.0f * (float)m_M - 1.0f ) );
	m_MxM_offsets.push_back( XMFLOAT2( 2.0f * (float)m_M, 3.0f * (float)m_M - 1.0f ) );


	m_Mx3_offsets.push_back( XMFLOAT2( 2.0f * (float)m_M - 2.0f, 0.0f ) );
	m_Mx3_offsets.push_back( XMFLOAT2( 2.0f * (float)m_M - 2.0f, 3.0f * (float)m_M - 1.0f ) );
	m_Mx3_offsets.push_back( XMFLOAT2( 0.0f, 2.0f * (float)m_M - 2.0f ) );
	m_Mx3_offsets.push_back( XMFLOAT2( 3.0f * (float)m_M - 1.0f, 2.0f * (float)m_M - 2.0f ) );

	//--------------------------------------------------------------------

	if( !InitializeBlocks() )
	{		
		return false;
	}

	DMGeoLevel level( m_N, 0 );

	for( size_t i = 0; i < _levels; i++ )
	{
		level.setLevel( i );
		m_levels.push_back( level );
	}

	return true;
}

bool DMGeoClipMap::InitializeBlocks( )
{
	m_NxN.Initialize( m_N, m_N );
	m_MxM.Initialize( m_M, m_M );
	m_Mx3_horiz.Initialize( m_M, 3 );
	m_Mx3_vert.Initialize( 3, m_M );
	m_2M1_vert.Initialize( 2, 2 * m_M + 1 );
	m_2M1_horiz.Initialize( 2 * m_M + 1, 2 );

	return true;
}

void DMGeoClipMap::Render( DMClipMapShader& shader, const DMCamera& camera, const DMFrustum& frustum )
{
	// Установка буфера вершин:
	UINT stride = sizeof( vertex );
	UINT offset = 0;

	// Установка способа отрисовки вершин в буфере
	DMD3D::instance().GetDeviceContext()->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );

	// new clipmap

	XMMATRIX worldMatrix;
	worldMatrix = XMMatrixIdentity();

	DMClipMapShader::ParamBuffer geo_param;
	memset( &geo_param, 0, sizeof( DMClipMapShader::ParamBuffer ) );

	static XMFLOAT2 camera_pos( 0.0, 0.0 );
	camera_pos.x = camera.position().x;
	camera_pos.y = camera.position().z;

	shader.setPass( shader.phase() );

	geo_param.levelScale = m_levels[0].scale();
	geo_param.mapSize = m_mapSize;
	geo_param.mapWidthMultipler = m_widthMultipler;
	geo_param.mapHeightMultippler = m_height_multipler;
	geo_param.map_N = (float)m_N;
	geo_param.hightOffset = m_hightOffset;
	geo_param.mapScale = m_mapScale;
	geo_param.mapOffset = m_mapOffset;
	geo_param.mapOffsetSpeed = m_mapOffsetSpeed;

	shader.SetVSParameters( &geo_param );


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
	instance_offset.offset = XMFLOAT2( 0.0f, 0.0f );

	offsets.push_back( instance_offset );

	shader.setInstanceOffset( offsets );

	ID3D11Buffer* buffer = m_NxN.vertexBuffer();
	DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
	DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_NxN.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
	shader.setDrawType( GS::DMShader::by_index_instance );
	pipeline().shaderConstant().setPerObjectBuffer( &worldMatrix );
	shader.renderInstanced( m_NxN.indexCount(), 0, 0, 1 );

	// draw low levels

	offsets.clear();

	
	for( size_t level_index = 1; level_index < m_levels.size(); ++level_index )
	{
		DMGeoLevel& level = m_levels[level_index];

		XMFLOAT2 level_pos = level.calcPos( camera_pos );

		instance_offset.level_pos = level_pos;

		// MxM offsets
		for( int offset_index = 0; offset_index < 12; ++offset_index )
		{	
			//create frustum culling for patches

			static DMAABB aabb_original;
			static DMAABB aabb_offset;

			aabb_original.CreateAABB( XMFLOAT3( (float)m_M, 100, (float)m_M ), XMFLOAT3( (float)m_M, 100, (float)m_M ) );
			aabb_offset = aabb_original;
			aabb_offset.setScale( level.scale(), level.scale(), level.scale() );
			XMFLOAT2 offset;
			offset.x = level_pos.x + m_MxM_offsets[offset_index].x * level.scale();
			offset.y = level_pos.y + m_MxM_offsets[offset_index].y * level.scale();
			aabb_offset.setPosition( offset.x, 0.5, offset.y );

			/*if( !frustum.CheckAABB( &aabb_offset ) )
				continue;*/

			
			instance_offset.offset = m_MxM_offsets[offset_index];
			instance_offset.scale = level.scale();

			offsets.push_back( instance_offset );

		}
		
		// Mx3 vertical offsets
		for( int Mx3_offset_index = 0; Mx3_offset_index < 2; ++Mx3_offset_index )
		{
			XMFLOAT2 offset;
			offset.x = level_pos.x + m_Mx3_offsets[Mx3_offset_index].x * level.scale();
			offset.y = level_pos.y + m_Mx3_offsets[Mx3_offset_index].y * level.scale();

			instance_offset.offset = m_Mx3_offsets[Mx3_offset_index];
			instance_offset.scale = level.scale();

			Mx3_vertical_offsets.push_back( instance_offset );
		}

		// Mx3 horizontal offsets
		for( size_t Mx3_offset_index = 2; Mx3_offset_index < m_Mx3_offsets.size(); ++Mx3_offset_index )
		{
			XMFLOAT2 offset;
			offset.x = level_pos.x + m_Mx3_offsets[Mx3_offset_index].x * level.scale();
			offset.y = level_pos.y + m_Mx3_offsets[Mx3_offset_index].y * level.scale();

			instance_offset.offset = m_Mx3_offsets[Mx3_offset_index];
			instance_offset.scale = level.scale();

			Mx3_horizontal_offsets.push_back( instance_offset );
		}
		
		// 2M1 offsets
		float dx = level_pos.x - m_levels[level_index - 1].calcPos( camera_pos ).x;
		float dy = level_pos.y - m_levels[level_index - 1].calcPos( camera_pos ).y;
		int calc_M_x = static_cast<int>( abs( floor( dx / level.scale() ) ) );
		int calc_M_y = static_cast<int>( abs( floor( dy / level.scale() ) ) );

		XMFLOAT2 offset;

		instance_offset.offset = XMFLOAT2( 0.0f, 0.0f );
		
		if( calc_M_x == m_M )
		{
			if( calc_M_y == m_M )
			{
				// Lower Left L Shape
				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
			else
			{
				// Upper Left L Shape
				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, 3 * m_M - 2 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, 3 * (float)m_M - 2 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
		}
		else
		{
			if( calc_M_y == m_M )
			{
				// Lower Right L Shape
				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );
				

				//instance_offset.offset = level_pos + XMFLOAT2( 3 * m_M - 2, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( 3 * (float)m_M - 2, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
			}
			else
			{
				// Upper Right L Shape 
				//instance_offset.offset = level_pos + XMFLOAT2( m_M - 1, 3 * m_M - 2 ) * level.scale();
				instance_offset.offset = XMFLOAT2( (float)m_M - 1, 3 * (float)m_M - 2 );
				instance_offset.scale = level.scale();
				_2M1_horizontal_offsets.push_back( instance_offset );

				//instance_offset.offset = level_pos + XMFLOAT2( 3 * m_M - 2, m_M - 1 ) * level.scale();
				instance_offset.offset = XMFLOAT2( 3 * (float)m_M - 2, (float)m_M - 1 );
				instance_offset.scale = level.scale();
				_2M1_vertical_offsets.push_back( instance_offset );
				
			}
		}
		
	}
	
	
	if( offsets.size() )
	{
		buffer = m_MxM.vertexBuffer();
		DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
		DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_MxM.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
		shader.setInstanceOffset( offsets );
		shader.renderInstanced( m_MxM.indexCount(), 0, 0, offsets.size() );
	}
	
	if( Mx3_vertical_offsets.size() )
	{	
		buffer = m_Mx3_vert.vertexBuffer();
		DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
		DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_Mx3_vert.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
		shader.setInstanceOffset( Mx3_vertical_offsets );
		shader.renderInstanced( m_Mx3_vert.indexCount(), 0, 0, Mx3_vertical_offsets.size() );
	}
	
	if( Mx3_horizontal_offsets.size() )
	{
		buffer = m_Mx3_horiz.vertexBuffer();
		DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
		DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_Mx3_horiz.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );				
		shader.setInstanceOffset( Mx3_horizontal_offsets );
		shader.renderInstanced( m_Mx3_horiz.indexCount(), 0, 0, Mx3_horizontal_offsets.size() );
	}
	
	if( _2M1_horizontal_offsets.size() )
	{
		buffer = m_2M1_horiz.vertexBuffer();
		DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
		DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_2M1_horiz.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
		shader.setInstanceOffset( _2M1_horizontal_offsets );
		shader.renderInstanced( m_2M1_horiz.indexCount(), 0, 0, _2M1_horizontal_offsets.size() );
	}

	if( _2M1_vertical_offsets.size() )
	{
		buffer = m_2M1_vert.vertexBuffer();
		DMD3D::instance().GetDeviceContext()->IASetVertexBuffers( 0, 1, &buffer, &stride, &offset );
		DMD3D::instance().GetDeviceContext()->IASetIndexBuffer( m_2M1_vert.indexBuffer(), DXGI_FORMAT_R32_UINT, 0 );
		shader.setInstanceOffset( _2M1_vertical_offsets );
		shader.renderInstanced( m_2M1_vert.indexCount(), 0, 0, _2M1_vertical_offsets.size() );
	}
	
}

float DMGeoClipMap::widthMultipler()
{
	return m_widthMultipler;
}

float DMGeoClipMap::heightMultipler()
{
	return m_height_multipler;
}


void DMGeoClipMap::setHightOffset( float offset )
{
	m_hightOffset = offset;
}

void DMGeoClipMap::setMapScale( float scale )
{
	m_mapScale = scale;
}

void DMGeoClipMap::setMapOffset( float x, float y )
{
	m_mapOffset = XMFLOAT2( x, y );
}

void DMGeoClipMap::setMapOffset( const XMFLOAT2& offset )
{
	m_mapOffset = offset;
}

void DMGeoClipMap::setMapOffsetSpeed( float speed )
{
	m_mapOffsetSpeed = speed;
}