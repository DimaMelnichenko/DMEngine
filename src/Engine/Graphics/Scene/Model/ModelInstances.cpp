#include "ModelInstances.h"
#include <unordered_set>
#include "System.h"

namespace GS
{

ModelInstances::ModelInstances() :
	SceneObject( "Models" )
{
	m_properties.setName( "Models" );
}

void ModelInstances::initialize( const std::vector<LevelDescription::ModelInstance>& instances )
{
	std::unordered_set<uint32_t> withProperties;
	m_instances.reserve( instances.size() );
	for( const LevelDescription::ModelInstance& description : instances )
	{
		Instance instance;
		instance.model = System::models().get( description.model ).get();
		instance.transform.setPosition( description.position );
		instance.transform.setRotation( description.rotation );
		instance.transform.setScale( description.scale );
		m_instances.push_back( instance );

		if( withProperties.insert( description.model ).second )
			m_properties.addSubContainer( instance.model->properties() );
	}
}

void ModelInstances::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	const XMVECTOR lodOrigin = XMLoadFloat3( &view.lodOrigin );
	const XMVECTOR viewPosition = XMLoadFloat3( &view.position );

	for( const Instance& instance : m_instances )
	{
		const XMVECTOR position = XMLoadFloat3( &instance.transform.position() );
		const DMModel::LodBlock* lod = instance.model->getLod( XMVectorGetX( XMVector3Length( position - lodOrigin ) ) );
		if( lod == nullptr || !lod->isRender )
			continue;

		const auto& mesh = System::meshes().get( lod->mesh );
		MeshBatch batch;
		batch.material = System::materials().get( lod->material )->m_shader.get();
		batch.materialId = lod->material;
		batch.params = &lod->params;
		batch.indexCount = mesh->indexCount();
		batch.indexOffset = mesh->indexOffset();
		batch.vertexOffset = mesh->vertexOffset();
		batch.world = instance.transform.worldMatrix();
		// Режим читается каждый кадр: параметры материала меняются в GUI
		batch.state = batch.material->renderState( lod->params );
		batch.distance = XMVectorGetX( XMVector3Length( position - viewPosition ) );
		collector.add( batch );
	}
}

PropertyContainer* ModelInstances::properties()
{
	return &m_properties;
}

}
