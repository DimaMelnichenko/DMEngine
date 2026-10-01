#pragma once

// D3D12 — из Agility SDK (его каталог include стоит в путях раньше Windows SDK, CMakeLists.txt), DXGI и DirectXMath — из
// Windows SDK. d3dcommon.h — ID3DBlob, D3D_SHADER_MACRO, D3D_PRIMITIVE_TOPOLOGY: общие у компилятора шейдеров и рантайма
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcommon.h>
#include <DirectXMath.h>

// Без using namespace DirectX: заголовки пишут DirectX::XMFLOAT3, .cpp — using namespace DirectX после include
