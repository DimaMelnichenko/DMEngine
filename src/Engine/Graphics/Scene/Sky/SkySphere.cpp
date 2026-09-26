#include "SkySphere.h"
#include "System.h"
#include "Pipeline.h"

namespace GS
{

SkySphere::SkySphere() :
	SceneObject( "Sky", RenderPass::sky )
{
}

void SkySphere::setModel( uint32_t modelId )
{
	m_modelId = modelId;
}

void SkySphere::update( const FrameContext& frame )
{
	if( m_modelId && System::models().exists( m_modelId ) )
		System::models().get( m_modelId )->transformBuffer().setPosition( frame.camera.position() );
}

void SkySphere::render( const FrameContext& frame )
{
	if( !m_modelId || !System::models().exists( m_modelId ) )
		return;

	const DMModel::LodBlock* block = System::models().get( m_modelId )->getLod( 0.0f );
	if( !block || !System::materials().exists( block->material ) )
		return;

	DMShader* shader = System::materials().get( block->material )->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	pipeline().shaderConstant().setPerObjectBuffer( block->resultMatrix );
	shader->setParams( block->params );

	ScopedRenderState skyState( DepthState::disabled, RasterState::frontCulling );

	const auto& mesh = System::meshes().get( block->mesh );
	shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
}

}
