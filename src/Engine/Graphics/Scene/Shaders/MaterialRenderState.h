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
};

}
