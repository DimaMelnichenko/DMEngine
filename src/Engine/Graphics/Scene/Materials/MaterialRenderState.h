#pragma once

#include <cstdint>

namespace GS
{

// Режим смешивания материала — Blend Mode в UE, alphaMode в glTF (OPAQUE, MASK, BLEND)
enum class BlendMode
{
	opaque,			// альфа не учитывается
	masked,			// отсечение по альфе с порогом: вырезанные лепестки, листья, решётки
	translucent		// альфа-блендинг в проходе прозрачных: стекло, дым
};

// Как рисовать материал: от режима зависят проход и вариант пиксельного шейдера, от двусторонности — отсечение граней
struct MaterialRenderState
{
	BlendMode blendMode = BlendMode::opaque;
	bool twoSided = false;	// Two Sided в UE, doubleSided в glTF: задние грани рисуются, нормаль у них развёрнута
	// Dithered LOD Transition в UE: в полосе перехода экземпляр рисуется обоими LOD с дополняющими друг друга масками
	// дизеринга (вариант шейдера ShaderPhaseOptions::lodDither); без него LOD сменяется мгновенно
	bool ditheredLodTransition = false;
};

// Вариант шейдера материала для вызова — то, что зависит не от параметров материала, а от того, как и где рисуют
// (перестановки шейдера материала в UE)
struct ShaderPhaseOptions
{
	bool instanced = false;			// матрицы экземпляров из буфера (INST_MATRIX): модели уровня одним вызовом
	// Экземпляры расстановки и леса из пула (INST_POS, INST_SCALE, INST_ROTATE — Shaders/instance.sh): положение, размер,
	// поворот по списку вида. Материал собирает этот вариант по Material::enablePlacedInstances (как Used with Instanced
	// Static Meshes в UE), у материала расстановки (PBRInstance) он и есть основной
	bool placed = false;
	// Запекание импостера (ImpostorMaterial::bake): цвет с покрытием и нормаль модели в две цели вместо освещения;
	// вариант собирает Material::enableImpostorBake
	bool impostorBake = false;
	// Глубину уже записал depth prepass: проход цвета с проверкой EQUAL берёт вариант без отсечения — ни по альфе, ни
	// дизерингом (как r.EarlyZPassOnlyMaterialMasking в UE)
	bool depthFromPrepass = false;
	bool lodDither = false;			// экземпляр в полосе смены LOD: дизеринг (LOD_DITHER, Shaders/lod_dither.sh)
	// Скиннинг (SKINNED, Shaders/skinning.sh): вершины по костям из матриц скиннинга вызова; вариант собирает
	// Material::enableSkinning (как Used with Skeletal Mesh в UE)
	bool skinned = false;
};

// Ключ перестановки шейдера материала — признаки варианта битами, как permutation domains в UE: материал собирает
// фазы по ключам (ShaderProgram::setPermutation), вызов находит свою по ключу (Material::permutationKey из
// ShaderPhaseOptions и режима материала). Новый признак — бит здесь и его defines у материала, а не ещё одно измерение
// массивов фаз
namespace ShaderPermutation
{
constexpr uint32_t instanced = 1u << 0;		// матрицы экземпляров из буфера (INST_MATRIX): модели уровня одним вызовом
constexpr uint32_t placed = 1u << 1;		// экземпляры расстановки из пула (INST_POS, INST_SCALE, INST_ROTATE)
constexpr uint32_t lodDither = 1u << 2;		// вершинный шейдер отдаёт долю смены LOD (LOD_DITHER)
constexpr uint32_t clipAlpha = 1u << 3;		// пиксельный шейдер отсекает по альфе (ALPHA_MASK)
constexpr uint32_t clipDither = 1u << 4;	// пиксельный шейдер отсекает дизерингом смены LOD (LOD_DITHER)
// Тени и depth prepass: вершинный шейдер «только глубина» (DEPTH_ONLY), пиксельный — только отсечение или его нет
constexpr uint32_t depthOnly = 1u << 5;
constexpr uint32_t impostorBake = 1u << 6;	// запекание импостера (mainBake): цвет и нормаль модели в цели кадра
constexpr uint32_t skinned = 1u << 7;		// скиннинг по костям (SKINNED): модели со скелетом
}

}
