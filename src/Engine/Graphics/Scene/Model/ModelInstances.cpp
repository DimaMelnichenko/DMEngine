#include "ModelInstances.h"
#include <algorithm>
#include <unordered_set>
#include "System.h"
#include "Shaders\lod_transition.h"

using namespace DirectX;

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
		instance.modelId = description.model;
		instance.transform.setPosition( description.position );
		instance.transform.setRotation( description.rotation );
		instance.transform.setScale( description.scale );
		m_instances.push_back( instance );

		if( withProperties.insert( description.model ).second )
			m_properties.addSubContainer( instance.model->properties() );

		// Экземпляры неподвижны: границы считаются один раз
		const DMModel::LodBlock* lod = instance.model->getLodById( 0 );
		if( !lod )
			continue;
		DirectX::BoundingBox meshBounds;
		lod->bounds.Transform( meshBounds, instance.transform.worldMatrix() );
		if( m_instances.size() == 1 )
			m_bounds = meshBounds;
		else
			DirectX::BoundingBox::CreateMerged( m_bounds, m_bounds, meshBounds );
	}
}

bool ModelInstances::bounds( DirectX::BoundingBox& bounds ) const
{
	if( m_instances.empty() )
		return false;
	bounds = m_bounds;
	return true;
}

void ModelInstances::collectMeshes( const RenderView& view, MeshCollector& collector )
{
	const XMVECTOR lodOrigin = XMLoadFloat3( &view.lodOrigin );
	const XMVECTOR viewPosition = XMLoadFloat3( &view.position );

	for( const Instance& instance : m_instances )
	{
		const XMVECTOR position = XMLoadFloat3( &instance.transform.position() );
		const float lodDistance = XMVectorGetX( XMVector3Length( position - lodOrigin ) );
		const float distance = XMVectorGetX( XMVector3Length( position - viewPosition ) );

		// LOD i — до своей дальности; в полосе дальность × (1 ± LOD_TRANSITION_WIDTH / 2) — вместе со следующим (за
		// последним — с пустотой), t — доля перехода от начала полосы
		const uint16_t lodCount = instance.model->lodCount();
		for( uint16_t i = 0; i < lodCount; ++i )
		{
			const float range = instance.model->lodRange( i );
			const float halfBand = range * LOD_TRANSITION_WIDTH * 0.5f;
			if( lodDistance >= range + halfBand )
				continue;
			const float t = halfBand > 0.0f ? std::clamp( ( lodDistance - ( range - halfBand ) ) / ( 2.0f * halfBand ), 0.0f, 1.0f ) : 0.0f;
			addLod( instance, i, t, distance, view, collector );
			if( t > 0.0f && i + 1 < lodCount )
				addLod( instance, static_cast<uint16_t>( i + 1 ), t - 1.0f, distance, view, collector );
			break;
		}
	}
}

void ModelInstances::addLod( const Instance& instance, uint16_t lodIndex, float lodDither, float distance,
							 const RenderView& view, MeshCollector& collector ) const
{
	const DMModel::LodBlock* lod = instance.model->getLodById( lodIndex );
	if( !lod || !lod->isRender )
		return;

	// Отсечение по frustum вида: границы LOD (все секции), переведённые мировой матрицей экземпляра
	DirectX::BoundingBox bounds;
	lod->bounds.Transform( bounds, instance.transform.worldMatrix() );
	const XMFLOAT3 boundsMin( bounds.Center.x - bounds.Extents.x, bounds.Center.y - bounds.Extents.y,
							  bounds.Center.z - bounds.Extents.z );
	const XMFLOAT3 boundsMax( bounds.Center.x + bounds.Extents.x, bounds.Center.y + bounds.Extents.y,
							  bounds.Center.z + bounds.Extents.z );
	if( !view.frustum.checkBox( boundsMin, boundsMax ) )
		return;

	// Элемент отрисовки — секция: у каждой свой меш и материал, проход — по режиму её материала
	for( size_t i = 0; i < lod->sections.size(); ++i )
	{
		const DMModel::Section& section = *lod->sections[i];
		MeshBatch batch;
		batch.material = System::materials().get( section.material ).get();
		// Режим читается каждый кадр: параметры материала меняются в GUI
		batch.state = batch.material->renderState( section.params );
		if( lodDither != 0.0f && !batch.state.ditheredLodTransition )
		{
			// Без дизеринга — та сторона перехода, которой в полосе больше: смена ровно на дальности LOD
			const bool outgoing = lodDither > 0.0f;
			if( outgoing ? lodDither > 0.5f : lodDither + 1.0f <= 0.5f )
				continue;
			batch.lodDither = 0.0f;
		}
		else
		{
			batch.lodDither = lodDither;
		}

		const auto& mesh = System::meshes().get( section.mesh );
		batch.materialId = section.material;
		batch.params = &section.params;
		batch.indexCount = mesh->indexCount();
		batch.indexOffset = mesh->indexOffset();
		batch.vertexOffset = mesh->vertexOffset();
		batch.world = instance.transform.worldMatrix();
		batch.distance = distance;
		// «Эта секция этого LOD этой модели»: одинаковые у соседних экземпляров рисуются одним вызовом
		batch.instanceGroup = ( instance.modelId << 8 ) | ( static_cast<uint32_t>( lodIndex ) << 4 ) | static_cast<uint32_t>( i );
		collector.add( batch );
	}
}

PropertyContainer* ModelInstances::properties()
{
	return &m_properties;
}

}
