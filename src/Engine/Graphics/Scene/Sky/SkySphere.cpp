#include "SkySphere.h"
#include <algorithm>
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
	const DMModel::LodBlock* block = System::models().get( m_modelId )->getLod( 0.0f );
	if( !block || !System::materials().exists( block->material ) )
		return;

	// Сфера всегда вокруг камеры вида и почти до дальней плоскости: дальше любой геометрии уровня, но ещё не отсечена
	const auto& mesh = System::meshes().get( block->mesh );
	const XMFLOAT3& extents = mesh->bounds().Extents;
	const float radius = std::max( XMVectorGetX( XMVector3Length( XMLoadFloat3( &extents ) ) ), 1e-3f );
	const float scale = 0.9f * context.view.farPlane / radius;
	m_transform.setPosition( context.view.position );
	m_transform.setScale( XMFLOAT3( scale, scale, scale ) );

	DMShader* shader = System::materials().get( block->material )->m_shader.get();
	shader->setPass( 0 );
	shader->setDrawType( DMShader::by_index );

	context.constants.setPerObjectBuffer( m_transform.worldMatrix() );
	shader->setParams( block->params );

	// Камера внутри сферы: видны внутренние грани. Глубину (LESS_EQUAL без записи) задаёт проход неба
	ScopedRenderState skyState( RasterState::frontCulling );

	shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
}

}
