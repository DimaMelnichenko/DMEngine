#pragma once

// Мост между картинками DirectXTex (ScratchImage: файлы текстур, карта высот, панорама, снимки) и ресурсами GPU.
// Единственное место вне DMD3D, где DirectXTex встречается с бэкендом: текстуры из картинок и обратно
#include <DirectXTex.h>
#include <string>
#include <vector>
#include "GpuResources.h"

namespace GpuImages
{

// Текстура из картинки со всеми мипами и срезами и вид на неё. viewKind — вид явно (массив из одного слоя DirectXTex
// счёл бы обычной текстурой), automatic — как у картинки: 2D, массив, куб или объём
bool createTexture( const DirectX::ScratchImage& image, Texture& texture, ShaderView& view,
					TextureViewDesc::Kind viewKind = TextureViewDesc::Kind::automatic );

// Копия текстуры с GPU на CPU (все мипы и срезы)
bool captureTexture( const Texture& texture, DirectX::ScratchImage& image );

// Картинка 2D из байтов (шаг строки rowPitch) в файл: PNG или JPG по расширению (снимки заднего буфера)
bool saveImage( const std::wstring& path, uint32_t width, uint32_t height, DXGI_FORMAT format, const uint8_t* bytes, uint32_t rowPitch );

}
