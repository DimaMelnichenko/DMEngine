#pragma once

#include <cstdint>
#include <DirectXMath.h>

namespace GS
{

class DMModel;
class MeshCollector;
class SkinningPalette;
struct RenderView;

// Поза модели со скелетом в кадре: матрицы скиннинга объекта (SkinningPalette) и первая кость этого экземпляра
struct SkinBinding
{
	const SkinningPalette* palette = nullptr;
	uint32_t boneOffset = 0;
};

// Модель в мире — в список отрисовки вида: общее у экземпляров моделей уровня (ModelInstances) и сущностей мира
// симуляции (WorldEntities). LOD — по расстоянию от точки LOD вида (lodOrigin) до position: в полосе вокруг дальности
// LOD (Shaders/lod_transition.h) — оба LOD с долями дизеринга, если материал секции это умеет (DitheredLODTransition,
// как Dithered LOD Transition в UE), за последним LOD модель так же исчезает. У видов теней дальности × «Shadow LOD
// scale» модели и LOD не детальнее «Shadow min LOD»; в дальнем каскаде теней моделей нет (только лес расстановки).
// LOD отсекается по frustum вида (его границы — мировой матрицей world); секции — MeshBatch, instanceGroup — модель,
// LOD, секция: одинаковые рядом рисуются одним инстансным вызовом. skin — поза модели со скелетом (вариант SKINNED,
// отсечение — по границам во всех позах клипов, DMModel::animatedBounds); nullptr — поза покоя
void collectModelMeshes( const DMModel& model, uint32_t modelId, DirectX::FXMMATRIX world, const DirectX::XMFLOAT3& position,
						 const RenderView& view, MeshCollector& collector, const SkinBinding* skin = nullptr );

}
