#include "DMModel.h"
#include <fstream>



DMModel::DMModel( DMD3D* parent, DMTexturePool* texture_pool ) : DMSceneObject( parent )
{
	m_textures = std::move( std::shared_ptr<std::vector<unsigned int>>( new std::vector<unsigned int>() ) );
	
	(*m_textures).resize( 5, 0 );

	m_textures_scale = std::move( std::shared_ptr<std::vector<D3DXVECTOR2>>( new std::vector<D3DXVECTOR2> ) );

	m_meshes = std::move( std::shared_ptr<std::vector<RangeMeshesContainer>>( new std::vector<RangeMeshesContainer>() ) );

	m_render_queues = std::move( std::shared_ptr<std::vector<DMRenderQueue*>>( new std::vector<DMRenderQueue*>() ) );

	m_texture_pool = texture_pool;
}

DMModel::~DMModel()
{
	// Shutdown the vertex and index buffers.
	Shutdown();
	
}

void DMModel::copy_internal_data( const DMModel* model )
{
	this->m_textures = model->m_textures;

	this->m_textures_scale = model->m_textures_scale;

	this->m_meshes = model->m_meshes;

	this->m_render_queues = model->m_render_queues;
}

DMModel::DMModel( const DMModel* model ) : DMSceneObject( dynamic_cast<const DMSceneObject*>( model ) )
{
	copy_internal_data( model );
}

DMModel::DMModel( const DMModel& model ) : DMSceneObject( model )
{	
	copy_internal_data( &model );
}

DMModel* DMModel::operator=( const DMModel* model )
{
	copy_internal_data( model );

	return this;
}

DMModel& DMModel::operator=( const DMModel& model )
{
	copy_internal_data( &model );

	return *this;
}

bool DMModel::Initialize( float lod_range, DMMesh::VertexCombination combination, WCHAR* modelFilename )
{
	if( addLODModel( lod_range, combination, modelFilename ) )
	{
		aabb() = ( *m_meshes )[0].mesh->aabb();
		return true;
	}

	return false;
}

bool DMModel::addLODModel( float lod_range, DMMesh::VertexCombination combination, WCHAR* modelFilename )
{
	std::unique_ptr<DMMesh> mesh( new DMMesh( m_dmd3d ) );

	if( mesh->loadMesh( combination, modelFilename ) )
	{
		RangeMeshesContainer container;		
		container.range = lod_range;
		container.mesh = std::move( mesh );
		(*m_meshes).push_back( container );
		return true;
	}

	return false;
}

void DMModel::Render( DMCamera* camera )
{
	static D3DXVECTOR3 model_to_camera;
	position( &model_to_camera );
	model_to_camera -= camera->position();
	Render( D3DXVec3Length( &model_to_camera ) );
}

void DMModel::Render( float lod_range )
{
	for( size_t i = 0; i < m_meshes->size(); ++i )
	{
		if( (*m_meshes)[i].range > lod_range )
		{
			m_current_mesh_index = i;
			( *m_meshes )[i].mesh->Render();
			break;
		}
	}

	return;
}

unsigned int DMModel::texture( DMModel::TextureType type )
{
	return ( (*m_textures).size() > static_cast<unsigned int>( type ) ) ? (*m_textures)[type] : 0 ;
}


void DMModel::Shutdown( )
{
	return;
}

ID3D11ShaderResourceView* DMModel::LoadTexture( const WCHAR* filename )
{
	ID3D11ShaderResourceView* texture = nullptr;

	D3DX11CreateShaderResourceViewFromFile( m_dmd3d->GetDevice(), filename, NULL, NULL, &texture, NULL );
	if( !texture )
	{
		return nullptr;
	}

	return texture;
}

void DMModel::setTexures()
{
	ID3D11ShaderResourceView* views[] = { m_texture_pool->texture( texture( DMModel::albedo ) ),
											m_texture_pool->texture( texture( DMModel::normal ) ),
											m_texture_pool->texture( texture( DMModel::height ) ),
											m_texture_pool->texture( texture( DMModel::gim ) ) };

	m_dmd3d->GetDeviceContext()->PSSetShaderResources( 0, 4, views );
}

void DMModel::setTexure( DMModel::TextureType type, unsigned int index, float u_scale, float v_scale )
{
 	(*m_textures)[type] = index;

	m_textures_scale->push_back( D3DXVECTOR2( u_scale, v_scale ) );
}

void DMModel::textureScale( unsigned int index, D3DXVECTOR2* uv_sacle ) const
{
	memcpy( uv_sacle, &(*m_textures_scale)[index], sizeof( D3DXVECTOR2 ) );
}

void DMModel::setPos( float x, float y, float z ) 
{
	DMSceneObject::setPosition( x, y, z );
}

void DMModel::setPos( const D3DXVECTOR3 & vec )
{
	DMSceneObject::setPosition( vec );
}

void DMModel::setScale( float scale )
{
	DMSceneObject::setScale( scale );
}

unsigned long DMModel::GetIndexCount()
{
	return (*m_meshes)[m_current_mesh_index].mesh->GetIndexCount();
}

void DMModel::setInFrustum( DMCamera* camera )
{
	static D3DXVECTOR3 model_to_camera;
	position( &model_to_camera );
	model_to_camera -= camera->position();
	setInFrustum( D3DXVec3Length( &model_to_camera ) );
}

void DMModel::setInFrustum( float lod_range )
{
	for( auto queue : *m_render_queues )
	{
		queue->append( this );
	}
}

void DMModel::addRenderQueue( DMRenderQueue* queue )
{
	if( queue )
		m_render_queues->push_back( queue );
}