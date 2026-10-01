#include "SkySphere.h"
#include <algorithm>
#include "System.h"
#include "ConstantBuffers.h"

using namespace DirectX;

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
	if( !block || block->sections.empty() )
		return;

	// Сфера всегда вокруг камеры вида и почти до дальней плоскости: дальше любой геометрии уровня, но ещё не отсечена
	const XMFLOAT3& extents = block->bounds.Extents;
	const float radius = std::max( XMVectorGetX( XMVector3Length( XMLoadFloat3( &extents ) ) ), 1e-3f );
	const float scale = 0.9f * context.view.farPlane / radius;
	m_transform.setPosition( context.view.position );
	m_transform.setScale( XMFLOAT3( scale, scale, scale ) );
	context.constants.setPerObjectBuffer( m_transform.worldMatrix() );

	// Камера внутри сферы: видны внутренние грани. Глубину (LESS_EQUAL без записи) задаёт проход неба
	ScopedRenderState skyState( RasterState::frontCulling );

	for( const auto& section : block->sections )
	{
		if( !System::materials().exists( section->material ) )
			continue;
		Material* material = System::materials().get( section->material ).get();
		material->setPass( 0 );
		material->setParams( section->params );
		const auto& mesh = System::meshes().get( section->mesh );
		DMD3D::instance().drawIndexed( mesh->indexCount(), mesh->indexOffset(), mesh->vertexOffset() );
	}
}

}
