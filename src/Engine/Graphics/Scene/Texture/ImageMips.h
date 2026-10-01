#pragma once

#include <DirectXTex.h>

// Мипы картинок на CPU (DirectXTex): текстуры из файлов (DMTextureStorage::load) и импостеры (ImpostorMaterial::bake)
namespace ImageMips
{

// Полная цепочка мипов вместо картинки (TEX_FILTER_DEFAULT; sRGB фильтруется в линейном). preserveAlphaCoverage > 0 —
// порог альфы (AlphaCutoff материала Masked), при котором мипы сохраняют долю непрозрачных текселей нулевого мипа:
// при усреднении тонкие травинки, лепестки, хвоя уходят под порог и вдали тают. false — мипы не построены или покрытие не
// сохранено (формат не 8-битный RGBA)
bool generate( DirectX::ScratchImage& image, float preserveAlphaCoverage = 0.0f );

// Цвет прозрачных текселей (альфа 0) — от соседних непрозрачных за passes проходов: у мипов и билинейной выборки на
// краю силуэта нет тёмной каймы от чёрного фона. Альфа не меняется; coverage — картинка того же размера, по альфе которой
// решается, где тексель есть (у нормали импостера в альфе — пропускание). Форматы — 8-битные RGBA / BGRA (растекается
// RGB) и R16_UNORM (глубина импостера; только с coverage)
bool dilateTransparent( DirectX::ScratchImage& image, int passes, const DirectX::ScratchImage* coverage = nullptr );

}
