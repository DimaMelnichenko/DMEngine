#include "SkySphere.h"
#include "System.h"
#include "Shaders\ConstantBuffers.h"

namespace GS
{

SkySphere::SkySphere() :
	SceneObject( "Sky" )
{
}

void SkySphere::setModel( uint32_t modelId )
{
	m_modelId = modelId;
}

void SkySphere::collectMeshes( const RenderView&, MeshCollector& collector )
{
	if( m_modelId && System::models().exists( m_modelId ) )
		collector.addCustom( passBit( MeshPass::sky ) );
}

void SkySphere::renderCustom( const RenderContext& context )
{
	// Сфера всегда вокруг камеры вида
	m_transform.setPosition( context.view.position );

	const DMModel::LodBlock* block = System::models().get( m_modelId )->getLod( 0.0f );
	if( !block || !System::materials().exists( block->material ) )
		return;

	DMShader* shader = System::materials().get( block->material )->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	context.constants.setPerObjectBuffer( m_transform.worldMatrix() );
	shader->setParams( block->params );

	ScopedRenderState skyState( DepthState::disabled, RasterState::frontCulling );

	const auto& mesh = System::meshes().get( block->mesh );
	shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
}

}
