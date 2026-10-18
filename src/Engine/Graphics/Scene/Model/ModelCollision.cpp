#include "ModelCollision.h"
#include <algorithm>
#include <vector>
#include "System.h"
#include "Model\DMModel.h"
#include "Model\Mesh\AbstractMesh.h"
#include "TrunkCollision.h"

using namespace DirectX;

namespace GS
{

PhysicsWorld::BodyId ModelCollision::addBody( const DMModel& model, FXMMATRIX world )
{
	const DMModel::LodBlock* lod = model.getLodById( 0 );
	if( !lod || model.collision() == DMModel::Collision::none )
		return 0;
	if( model.collision() == DMModel::Collision::box )
		return m_physics.addStaticBox( lod->bounds.Center, lod->bounds.Extents, world );
	return m_physics.addStaticBody( shape( model ), world );
}

PhysicsWorld::ShapeId ModelCollision::shape( const DMModel& model )
{
	auto it = m_shapes.find( &model );
	if( it != m_shapes.end() )
		return it->second;

	// Вершины всех секций LOD 0 (позиция — первое поле вершины любого формата, VertexData)
	const DMModel::LodBlock* lod = model.getLodById( 0 );
	const BoundingBox& bounds = lod->bounds;
	std::vector<XMFLOAT3> points;
	std::vector<uint32_t> indices;
	for( const auto& section : lod->sections )
	{
		auto& mesh = System::meshes().get( section->mesh );
		if( !mesh )
			continue;
		const uint32_t first = static_cast<uint32_t>( points.size() );
		const char* vertices = mesh->getVertices();
		const uint32_t stride = mesh->vertexStride();
		for( uint32_t i = 0; i < mesh->vertexCount(); ++i )
			points.push_back( *reinterpret_cast<const XMFLOAT3*>( vertices + static_cast<size_t>( i ) * stride ) );
		for( uint32_t index : mesh->getIndices() )
			indices.push_back( first + index );
	}

	PhysicsWorld::ShapeId shape = 0;
	TrunkBase trunk;
	if( points.empty() )
		shape = m_physics.createBoxShape( bounds.Center, bounds.Extents );
	else if( model.collision() == DMModel::Collision::capsule &&
			 trunkBase( points, AbstractMesh::baseBand( 2.0f * bounds.Extents.y ), trunk ) )
	{
		// Ствол: ось и низ — по вершинам у общего низа модели, на всю высоту; толщина — своя (Models.collision_radius) или
		// у земли
		const float radius = model.collisionRadius() > 0.0f ? model.collisionRadius() : trunk.radius;
		shape = m_physics.createCapsuleShape( XMFLOAT3( trunk.center.x, trunk.bottom, trunk.center.y ), trunk.height, radius );
	}
	else if( model.collision() == DMModel::Collision::mesh )
		shape = m_physics.createMeshShape( points, indices );
	else
		shape = m_physics.createHullShape( points );
	m_shapes[&model] = shape;
	return shape;
}

}
