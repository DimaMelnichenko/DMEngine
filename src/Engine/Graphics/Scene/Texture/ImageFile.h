#pragma once

#include <string>
#include <DirectXTex.h>

namespace ImageFile
{

// Картинка из файла (путь в UTF-8) по расширению: DDS, TGA, HDR, EXR (tinyexr), остальное — WIC (PNG, JPG…).
// Формат — как в файле; цветовое пространство и мипы — забота вызывающего (DMTextureStorage::load)
bool load( const std::string& path, DirectX::ScratchImage& image );

}
