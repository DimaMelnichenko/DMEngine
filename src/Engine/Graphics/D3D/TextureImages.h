#pragma once

// Мост между картинками DirectXTex (ScratchImage: файлы текстур, карта высот, панорама) и ресурсами GPU. Здесь
// единственное место вне DMD3D, где DirectXTex говорит с устройством D3D11; с бэкендом D3D12 меняется только оно
// (DirectXTex с BUILD_DX12, шаг B2 плана переезда)
#include <DirectXTex.h>
#include "GpuResources.h"

namespace GpuImages
{

// Текстура из картинки со всеми мипами и срезами и вид на неё. viewKind — вид явно (массив из одного слоя DirectXTex
// счёл бы обычной текстурой), automatic — как у картинки: 2D, массив, куб или объём
bool createTexture( const DirectX::ScratchImage& image, Texture& texture, ShaderView& view,
					TextureViewDesc::Kind viewKind = TextureViewDesc::Kind::automatic );

// Копия текстуры с GPU на CPU (все мипы и срезы)
bool captureTexture( const Texture& texture, DirectX::ScratchImage& image );

}
