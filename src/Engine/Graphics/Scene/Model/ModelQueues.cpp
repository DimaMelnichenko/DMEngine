#include "ModelQueues.h"
#include <algorithm>
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
	m_translucent.clear();

	const XMVECTOR cameraPosition = XMLoadFloat3( &frame.camera.position() );

	for( const Instance& instance : m_instances )
	{
		const XMVECTOR offset = XMVectorSubtract( XMLoadFloat3( &instance.transform.position() ), cameraPosition );
		const float distance = XMVectorGetX( XMVector3Length( offset ) );

		const DMModel::LodBlock* lod = instance.model->getLod( distance );
		if( lod == nullptr || !lod->isRender )
			continue;

		// Режим читается каждый кадр: параметры материала меняются в GUI
		const DrawItem item = { lod, &instance.transform,
								System::materials().get( lod->material )->m_shader->renderState( lod->params ), distance };
		if( item.state.blendMode == BlendMode::translucent )
			m_translucent.push_back( item );
		else
			m_renderQueues[lod->material].push_back( item );
	}

	// Полупрозрачные смешиваются с уже нарисованным, поэтому дальние — раньше. Сортировка по опорной точке
	// экземпляра: пересекающиеся модели и грани внутри одной модели не сортируются
	std::sort( m_translucent.begin(), m_translucent.end(),
			   []( const DrawItem& a, const DrawItem& b ) { return a.distance > b.distance; } );
}

bool ModelQueues::drawsIn( RenderPass pass ) const
{
	return pass == RenderPass::opaque || ( pass == RenderPass::transparent && !m_translucent.empty() );
}

void ModelQueues::render( const FrameContext& frame, RenderPass pass )
{
	ScopedRenderState modelState;
	const RasterState frameRaster = modelState.previous().raster;

	if( pass == RenderPass::transparent )
	{
		for( const DrawItem& item : m_translucent )
			draw( item, frameRaster );
		return;
	}

	for( const auto& queuePair : m_renderQueues )
	{
		for( const DrawItem& item : queuePair.second )
			draw( item, frameRaster );
	}
}

void ModelQueues::draw( const DrawItem& item, RasterState frameRaster ) const
{
	DMShader* shader = System::materials().get( item.lod->material )->m_shader.get();
	DMD3D::instance().setState( materialRasterState( item.state.twoSided, frameRaster ) );
	shader->setPass( shader->phaseFor( item.lod->params ) );
	shader->setParams( item.lod->params );
	shader->setDrawType( DMShader::by_index );

	pipeline().shaderConstant().setPerObjectBuffer( item.transform->worldMatrix() );

	// отрисовка модели согласно смещению вершин и индексов для главного буфера
	const auto& mesh = System::meshes().get( item.lod->mesh );
	shader->render( mesh->indexCount(), mesh->vertexOffset(), mesh->indexOffset() );
}

PropertyContainer* ModelQueues::properties()
{
	return &m_properties;
}

}
