#pragma once

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
	// Глубину уже записал depth prepass: проход цвета с проверкой EQUAL берёт вариант без отсечения — ни по альфе, ни
	// дизерингом (как r.EarlyZPassOnlyMaterialMasking в UE)
	bool depthFromPrepass = false;
	bool lodDither = false;			// экземпляр в полосе смены LOD: дизеринг (LOD_DITHER, Shaders/lod_dither.sh)
};

}
