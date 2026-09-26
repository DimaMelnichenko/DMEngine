#include "ModelQueues.h"
#include "System.h"
#include "Pipeline.h"

namespace GS
{

ModelQueues::ModelQueues() :
	SceneObject( "Models", RenderPass::opaque )
{
	m_properties.setName( "Models" );
}

void ModelQueues::initialize()
{
	for( const auto& pair : System::models() )
	{
		if( !m_excluded.count( pair.second->name() ) )
			m_properties.addSubContainer( pair.second->properties() );
	}
}

void ModelQueues::exclude( const std::string& modelName )
{
	m_excluded.insert( modelName );
}

void ModelQueues::update( const FrameContext& frame )
{
	for( auto& queue : m_renderQueues )
	{
		queue.second.clear();
	}

	XMVECTOR camPosVector = XMLoadFloat3( &frame.camera.position() );

	for( const auto& pair : System::models() )
	{
		if( m_excluded.count( pair.second->name() ) )
			continue;

		XMVECTOR lenVec = XMVectorSubtract( pair.second->transformBuffer().position(), camPosVector );
		XMVECTOR distance = XMVector3Length( lenVec );

		// достаем лод меша в зависимости от расстояния до камеры
		const DMModel::LodBlock* block = pair.second->getLod( distance.m128_f32[0] );

		if( block != nullptr && block->isRender )
		{
			m_renderQueues[block->material].push_back( block );
		}
	}
}

void ModelQueues::render( const FrameContext& frame )
{
	for( auto& queuePair : m_renderQueues )
	{
		DMShader* shader = System::materials().get( queuePair.first )->m_shader.get();
		shader->setPass( 0 );
		shader->setDrawType( DMShader::by_index );

		for( const auto LODblock : queuePair.second )
		{
			//установка матрицы модели в шейдер
			pipeline().shaderConstant().setPerObjectBuffer( LODblock->resultMatrix );

			shader->setParams( LODblock->params );
			// отрисовка модели согласно смещению вершин и индексов для главного буфера
			const auto& mesh = System::meshes().get( LODblock->mesh );
			shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
		}
	}
}

PropertyContainer* ModelQueues::properties()
{
	return &m_properties;
}

}
