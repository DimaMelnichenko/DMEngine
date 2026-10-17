#include "ModelCollision.h"
#include <algorithm>
#include <vector>
#include "System.h"
#include "Model\DMModel.h"

using namespace DirectX;

namespace GS
{

PhysicsWorld::BodyId addModelBody( PhysicsWorld& physics, const DMModel& model, FXMMATRIX world )
{
	const DMModel::LodBlock* lod = model.getLodById( 0 );
	if( !lod || model.collision() == DMModel::Collision::none )
		return 0;
	const BoundingBox& bounds = lod->bounds;
	switch( model.collision() )
	{
	case DMModel::Collision::box:
		return physics.addStaticBox( bounds.Center, bounds.Extents, world );
	case DMModel::Collision::capsule:
	{
		// Ствол: от низа модели на всю высоту, толщина — у земли
		const BoundingBox& base = lod->baseBounds;
		const XMFLOAT3 bottom( base.Center.x, bounds.Center.y - bounds.Extents.y, base.Center.z );
		return physics.addStaticCapsule( bottom, 2.0f * bounds.Extents.y, std::max( base.Extents.x, base.Extents.z ), world );
	}
	case DMModel::Collision::hull:
	case DMModel::Collision::mesh:
	{
		// Вершины всех секций LOD 0 (позиция — первое поле вершины любого формата, VertexData)
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
		if( points.empty() )
			return physics.addStaticBox( bounds.Center, bounds.Extents, world );
		return model.collision() == DMModel::Collision::hull ? physics.addStaticHull( points, world ) :
															   physics.addStaticMesh( points, indices, world );
	}
	default:
		return 0;
	}
}

}
