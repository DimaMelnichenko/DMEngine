#pragma once

#include <cstdint>
#include <vector>
#include "DirectX.h"
#include "D3D\DMD3D.h"
#include "Materials\MaterialRenderState.h"
#include "Properties/PropertyContainer.h"

namespace GS
{

class Material;
class SceneObject;

// Проход кадра — как EMeshPass в UE. Порядок проходов и их состояния задаёт Renderer
enum class MeshPass
{
	depthPrepass,	// только глубина непрозрачных и Masked (EMeshPass::DepthPass в UE) — до непрозрачных
	opaque,			// непрозрачные и с отсечением по альфе (Opaque и Masked); после prepass — глубина EQUAL без записи
	// После непрозрачных — те, кому нужна глубина сцены в шейдере (Material::readsSceneDepth: импостеры): глубина записана
	// prepass, в проходе она только для чтения и видна шейдерам (SLOT_SCENE_DEPTH); без prepass их рисует opaque
	opaqueDepthRead,
	sky,			// после непрозрачных: фон на дальней плоскости, глубина «ближе или равно» без записи — только где пусто
	transparent,	// последним: альфа-блендинг, глубина только читается (Translucent)
	csmShadowDepth	// глубина каскадов теней солнца (EMeshPass::CSMShadowDepth в UE) — до проходов сцены, на виды каскадов
};

// Проходов сцены с главного вида: depthPrepass, opaque, opaqueDepthRead, sky, transparent
constexpr uint32_t scenePassCount = 5;

// Проходы только глубины: материал рисуется вариантом depthPhaseFor, без цвета
constexpr bool isDepthOnlyPass( MeshPass pass )
{
	return pass == MeshPass::depthPrepass || pass == MeshPass::csmShadowDepth;
}

// Бит прохода для маски CustomBatch
constexpr uint32_t passBit( MeshPass pass )
{
	return 1u << static_cast<uint32_t>( pass );
}

// Проход для режима материала
inline MeshPass passFor( BlendMode mode )
{
	return mode == BlendMode::translucent ? MeshPass::transparent : MeshPass::opaque;
}

// Проход с учётом depth prepass: непрозрачный материал, который читает глубину сцены, после prepass — в opaqueDepthRead
inline MeshPass passFor( BlendMode mode, bool readsSceneDepth, bool depthPrepass )
{
	const MeshPass pass = passFor( mode );
	return pass == MeshPass::opaque && readsSceneDepth && depthPrepass ? MeshPass::opaqueDepthRead : pass;
}

// Меш, который объект отдаёт на отрисовку, — как FMeshBatch в UE: что рисовать, а не в каком проходе. Проход
// выбирает рендерер по режиму материала; по тем же данным он рисует меш в depth prepass и в тенях
struct MeshBatch
{
	Material* material = nullptr;
	uint32_t materialId = 0;					// для сортировки по материалу
	const PropertyContainer* params = nullptr;	// параметры материала (LOD модели)
	uint32_t indexCount = 0;					// диапазон в общем буфере VertexPool
	uint32_t indexOffset = 0;
	uint32_t vertexOffset = 0;
	DirectX::XMMATRIX world = DirectX::XMMatrixIdentity();
	MaterialRenderState state;					// режим и двусторонность материала
	float distance = 0.0f;						// до вида — для сортировки полупрозрачных
	// Постоянный номер «этот меш с этими параметрами» (у моделей — модель и LOD): по нему одинаковые меши
	// встают в сортировке подряд, и рендерер рисует их одним инстансным вызовом
	uint32_t instanceGroup = 0;
	bool castsShadow = true;					// рисуется в проход глубины теней (Cast Shadow в UE)
	// Экземпляр в полосе смены LOD (DitherFactor у ClipLODTransition в UE, Shaders/lod_dither.sh): (0; 1) — уходящий LOD,
	// (−1; 0) — приходящий, 0 — вне перехода. Такой меш рисуется вариантом материала с дизерингом и не объединяется
	// с другими в инстансный вызов: доля у каждого своя, в константах объекта
	float lodDither = 0.0f;

	// Заполняет MeshCollector
	const SceneObject* owner = nullptr;
	uint32_t ownerOrder = 0;
	bool mirrored = false;						// отрицательный определитель мировой матрицы
};

// Собственный вызов объекта с нестандартной геометрией: рендерер вызовет его renderCustom в проходах из маски
struct CustomBatch
{
	SceneObject* owner = nullptr;
	uint32_t ownerOrder = 0;
	uint32_t passMask = 0;						// биты passBit()
	float distance = 0.0f;						// для сортировки в проходе прозрачных: 0 — ближе всех, рисуется последним
};

// Куда объекты складывают меши и свои вызовы за вид — как FMeshElementCollector в UE
class MeshCollector
{
public:
	// Рендерер перед опросом очередного объекта
	void beginObject( SceneObject* owner, uint32_t order )
	{
		m_owner = owner;
		m_order = order;
	}

	void add( const MeshBatch& batch )
	{
		m_meshes.push_back( batch );
		MeshBatch& added = m_meshes.back();
		added.owner = m_owner;
		added.ownerOrder = m_order;
		added.mirrored = DirectX::XMVectorGetX( DirectX::XMMatrixDeterminant( batch.world ) ) < 0.0f;
	}

	void addCustom( uint32_t passMask, float distance = 0.0f )
	{
		m_customs.push_back( { m_owner, m_order, passMask, distance } );
	}

	void clear()
	{
		m_meshes.clear();
		m_customs.clear();
	}

	const std::vector<MeshBatch>& meshes() const { return m_meshes; }
	const std::vector<CustomBatch>& customs() const { return m_customs; }

private:
	std::vector<MeshBatch> m_meshes;
	std::vector<CustomBatch> m_customs;
	SceneObject* m_owner = nullptr;
	uint32_t m_order = 0;
};

// Состояние растеризатора для меша: двусторонний — без отсечения граней, зеркальный (отрицательный определитель) —
// с обратным обходом лицевых граней, как в UE; каркасный режим кадра (Q) остаётся каркасом
inline RasterState materialRasterState( bool twoSided, bool mirrored, RasterState frameState )
{
	if( frameState != RasterState::solid )
		return frameState;
	if( twoSided )
		return mirrored ? RasterState::noCullingMirrored : RasterState::noCulling;
	return mirrored ? RasterState::solidMirrored : RasterState::solid;
}

}
