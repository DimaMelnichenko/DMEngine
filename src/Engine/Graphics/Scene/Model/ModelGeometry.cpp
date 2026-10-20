#include "ModelGeometry.h"
#include "Assets.h"
#include "Model\DMModel.h"

using namespace DirectX;

namespace GS
{

bool modelLodGeometry( const DMModel& model, uint16_t lod, std::vector<XMFLOAT3>& points, std::vector<uint32_t>& indices )
{
	points.clear();
	indices.clear();
	const DMModel::LodBlock* block = model.getLodById( lod );
	if( !block )
		return false;
	for( const auto& section : block->sections )
	{
		auto& mesh = Assets::meshes().get( section->mesh );
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
	return !points.empty();
}

}
