#include "DMModel.h"
#include <fstream>



DMModel::DMModel( const std::wstring& name ) : m_name( name )
{
	m_textures = std::move( std::shared_ptr<std::vector<unsigned int>>( new std::vector<unsigned int>() ) );
	
	(*m_textures).resize( 5, 0 );

	m_textures_scale = std::move( std::shared_ptr<std::vector<D3DXVECTOR2>>( new std::vector<D3DXVECTOR2> ) );

	m_meshes = std::move( std::shared_ptr<std::vector<RangeMeshesContainer>>( new std::vector<RangeMeshesContainer>() ) );

	m_render_queues = std::move( std::shared_ptr<std::vector<DMRenderQueue*>>( new std::vector<DMRenderQueue*>() ) );
}

DMModel::~DMModel()
{
	
}

void DMModel::copy_internal_data( const DMModel* model )
{
	this->m_textures = model->m_textures;

	this->m_textures_scale = model->m_textures_scale;

	this->m_meshes = model->m_meshes;

	this->m_render_queues = model->m_render_queues;

	this->m_name = model->m_name;
}

DMModel::DMModel( const DMModel* model )
{
	copy_internal_data( model );
}

DMModel::DMModel( const DMModel& model )
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
		aabb() = (*m_meshes)[0].mesh.aabb();
		return true;
	}

	return false;
}

bool DMModel::addLODModel( float lod_range, DMMesh::VertexCombination combination, WCHAR* modelFilename )
{
	DMMesh mesh;

	if( mesh.loadMesh( combination, modelFilename ) )
	{
		RangeMeshesContainer container;		
		container.range = lod_range;
		container.mesh = std::move( mesh );
		(*m_meshes).push_back( std::move( container ) );
		return true;
	}

	return false;
}

void DMModel::Render( const DMCamera& camera )
{
	static D3DXVECTOR3 model_to_camera;
	position( &model_to_camera );
	model_to_camera -= camera.position();
	Render( D3DXVec3Length( &model_to_camera ) );
}

void DMModel::Render( float lod_range )
{
	for( size_t i = 0; i < m_meshes->size(); ++i )
	{
		if( (*m_meshes)[i].range > lod_range )
		{
			m_current_mesh_index = i;
			( *m_meshes )[i].mesh.Render();
			break;
		}
	}

	return;
}

unsigned long DMModel::GetIndexCount()
{
	return m_meshes[m_current_mesh_index]->GetIndexCount();
}

void DMModel::setInFrustum( float lod_range )
{
	for( auto pair : m_meshLodIndex )
	{
		if( pair.first < lod_range )
		{
			m_current_mesh_index = pair.second;
			break;
		}
	}
}

unsigned int DMModel::countOfLOD()
{
	return m_meshes.size();
}

const std::wstring& DMModel::name()
{
	return m_name;
}

DMSceneObject& DMModel::dceneObject()
{
	return m_sceneObject;
}