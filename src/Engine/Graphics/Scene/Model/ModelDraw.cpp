#include "ModelDraw.h"
#include <algorithm>
#include "System.h"
#include "MeshBatch.h"
#include "RenderView.h"
#include "Model\DMModel.h"
#include "Shaders\lod_transition.h"

using namespace DirectX;

namespace GS
{

namespace
{

// Секции LOD в список вида. lodDither — доля перехода (MeshBatch::lodDither): (0; 1) — уходящий LOD, (−1; 0) —
// приходящий. Секция без дизеринга рисуется, только пока её сторона перехода — не меньше половины: LOD сменяется
// мгновенно на дальности модели, как без полосы
void addLod( const DMModel& model, uint32_t modelId, FXMMATRIX world, uint16_t lodIndex, float lodDither, float distance,
			 const RenderView& view, MeshCollector& collector, const SkinBinding* skin )
{
	const DMModel::LodBlock* lod = model.getLodById( lodIndex );
	if( !lod || !lod->isRender )
		return;

	// Отсечение по frustum вида: границы LOD (все секции; у позы скелета — во всех позах клипов), переведённые мировой
	// матрицей
	DirectX::BoundingBox bounds;
	( skin ? model.animatedBounds() : lod->bounds ).Transform( bounds, world );
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
		batch.state = batch.material->renderState( *section.params );
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
		batch.params = section.params;
		batch.indexCount = mesh->indexCount();
		batch.indexOffset = mesh->indexOffset();
		batch.vertexOffset = mesh->vertexOffset();
		batch.world = world;
		batch.distance = distance;
		// Поза скелета — если материал собрал варианты скиннинга (Material::enableSkinning), иначе поза покоя
		ShaderPhaseOptions skinned;
		skinned.skinned = true;
		if( skin && batch.material->phaseFor( *section.params, skinned ) >= 0 )
		{
			batch.skin = skin->palette;
			batch.boneOffset = skin->boneOffset;
		}
		// «Эта секция этого LOD этой модели»: одинаковые у соседних экземпляров рисуются одним вызовом
		batch.instanceGroup = ( modelId << 8 ) | ( static_cast<uint32_t>( lodIndex ) << 4 ) | static_cast<uint32_t>( i );
		collector.add( batch );
	}
}

}

void collectModelMeshes( const DMModel& model, uint32_t modelId, FXMMATRIX world, const XMFLOAT3& position,
						 const RenderView& view, MeshCollector& collector, const SkinBinding* skin )
{
	// Дальний каскад теней — только лес расстановки: модели в нём не рисуются (их немного и они рядом)
	if( view.kind == RenderView::Kind::farShadow )
		return;
	const XMVECTOR point = XMLoadFloat3( &position );
	const float lodDistance = XMVectorGetX( XMVector3Length( point - XMLoadFloat3( &view.lodOrigin ) ) );
	const float distance = XMVectorGetX( XMVector3Length( point - XMLoadFloat3( &view.position ) ) );

	// LOD i — до своей дальности; в полосе дальность × (1 ± LOD_TRANSITION_WIDTH / 2) — вместе со следующим (за
	// последним — с пустотой), t — доля перехода от начала полосы. У видов теней дальности моделей с «Shadow LOD
	// scale» < 1 ближе (DMModel::shadowLodScale): тень дальних — более грубым LOD; переход у каждого вида свой. LOD
	// детальнее «Shadow min LOD» у видов теней нет (DMModel::shadowMinLod)
	const uint16_t lodCount = model.lodCount();
	const float lodScale = view.shadow() ? model.shadowLodScale() : 1.0f;
	const uint16_t firstLod = view.shadow() ? static_cast<uint16_t>( std::min<uint32_t>( model.shadowMinLod(), lodCount ) ) : 0;
	for( uint16_t i = firstLod; i < lodCount; ++i )
	{
		// Дальность последнего LOD — конец модели: её множитель не сдвигает
		const float range = model.lodRange( i ) * ( i + 1 < lodCount ? lodScale : 1.0f );
		const float halfBand = range * LOD_TRANSITION_WIDTH * 0.5f;
		if( lodDistance >= range + halfBand )
			continue;
		const float t = halfBand > 0.0f ? std::clamp( ( lodDistance - ( range - halfBand ) ) / ( 2.0f * halfBand ), 0.0f, 1.0f ) : 0.0f;
		addLod( model, modelId, world, i, t, distance, view, collector, skin );
		if( t > 0.0f && i + 1 < lodCount )
			addLod( model, modelId, world, static_cast<uint16_t>( i + 1 ), t - 1.0f, distance, view, collector, skin );
		break;
	}
}

}
