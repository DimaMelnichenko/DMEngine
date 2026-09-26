#include "ModelQueues.h"
#include <unordered_set>
#include "System.h"
#include "Pipeline.h"

namespace GS
{

ModelQueues::ModelQueues() :
	SceneObject( "Models", RenderPass::opaque )
{
	m_properties.setName( "Models" );
}

void ModelQueues::initialize( const std::vector<LevelDescription::ModelInstance>& instances )
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

void ModelQueues::update( const FrameContext& frame )
{
	for( auto& queue : m_renderQueues )
	{
		queue.second.clear();
	}

	const XMVECTOR cameraPosition = XMLoadFloat3( &frame.camera.position() );

	for( const Instance& instance : m_instances )
	{
		const XMVECTOR offset = XMVectorSubtract( XMLoadFloat3( &instance.transform.position() ), cameraPosition );
		const float distance = XMVectorGetX( XMVector3Length( offset ) );

		const DMModel::LodBlock* lod = instance.model->getLod( distance );
		if( lod != nullptr && lod->isRender )
			m_renderQueues[lod->material].push_back( { lod, &instance.transform } );
	}
}

void ModelQueues::render( const FrameContext& frame )
{
	for( auto& queuePair : m_renderQueues )
	{
		DMShader* shader = System::materials().get( queuePair.first )->m_shader.get();
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );

		for( const DrawItem& item : queuePair.second )
		{
			pipeline().shaderConstant().setPerObjectBuffer( item.transform->worldMatrix() );

			shader->setParams( item.lod->params );
			// отрисовка модели согласно смещению вершин и индексов для главного буфера
			const auto& mesh = System::meshes().get( item.lod->mesh );
			shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
		}
	}
}

PropertyContainer* ModelQueues::properties()
{
	return &m_properties;
}

}
