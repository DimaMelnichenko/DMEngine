#pragma once

#include "DirectX.h"

namespace GS
{

// Флаги компиляции шейдеров: в Debug — отладочная информация без оптимизации (исходник виден в RenderDoc / PIX),
// в Release — полная оптимизация
UINT shaderCompileFlags();

}
