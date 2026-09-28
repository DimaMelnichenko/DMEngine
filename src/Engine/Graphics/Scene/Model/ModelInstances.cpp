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
		const int lodIndex = instance.model->lodIndex( XMVectorGetX( XMVector3Length( position - lodOrigin ) ) );
		if( lodIndex < 0 )
			continue;
		const DMModel::LodBlock* lod = instance.model->getLodById( static_cast<uint16_t>( lodIndex ) );
		if( !lod->isRender )
			continue;

		// Отсечение по frustum вида: границы LOD (все секции), переведённые мировой матрицей экземпляра
		DirectX::BoundingBox bounds;
		lod->bounds.Transform( bounds, instance.transform.worldMatrix() );
		const XMFLOAT3 boundsMin( bounds.Center.x - bounds.Extents.x, bounds.Center.y - bounds.Extents.y,
								  bounds.Center.z - bounds.Extents.z );
		const XMFLOAT3 boundsMax( bounds.Center.x + bounds.Extents.x, bounds.Center.y + bounds.Extents.y,
								  bounds.Center.z + bounds.Extents.z );
		if( !view.frustum.checkBox( boundsMin, boundsMax ) )
			continue;

		// Элемент отрисовки — секция: у каждой свой меш и материал, проход — по режиму её материала
		const float distance = XMVectorGetX( XMVector3Length( position - viewPosition ) );
		for( size_t i = 0; i < lod->sections.size(); ++i )
		{
			const DMModel::Section& section = *lod->sections[i];
			const auto& mesh = System::meshes().get( section.mesh );
			MeshBatch batch;
			batch.material = System::materials().get( section.material )->m_shader.get();
			batch.materialId = section.material;
			batch.params = &section.params;
			batch.indexCount = mesh->indexCount();
			batch.indexOffset = mesh->indexOffset();
			batch.vertexOffset = mesh->vertexOffset();
			batch.world = instance.transform.worldMatrix();
			// Режим читается каждый кадр: параметры материала меняются в GUI
			batch.state = batch.material->renderState( section.params );
			batch.distance = distance;
			// «Эта секция этого LOD этой модели»: одинаковые у соседних экземпляров рисуются одним вызовом
			batch.instanceGroup = ( instance.modelId << 8 ) | ( static_cast<uint32_t>( lodIndex ) << 4 ) | static_cast<uint32_t>( i );
			collector.add( batch );
		}
	}
}

PropertyContainer* ModelInstances::properties()
{
	return &m_properties;
}

}
