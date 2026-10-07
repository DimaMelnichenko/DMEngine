cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# C нужен встроенному в SQLiteCpp sqlite3
project(DMEngine C CXX)

if(NOT MSVC)
    message(FATAL_ERROR "DMEngine собирается только компилятором MSVC (toolchain Visual Studio, amd64)")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# /MDd в Debug, /MD в остальных конфигурациях — одинаково для движка и библиотек из FetchContent
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

# Все зависимости скачиваются при первом configure (нужны сеть и git). Версии — docs/d3d12.md §2
include(FetchContent)

# DirectXTex (загрузка текстур, мипы на CPU, запись снимков через WIC): только часть D3D12
set(BUILD_TOOLS OFF)
set(BUILD_SAMPLE OFF)
set(BUILD_DX11 OFF)
set(BUILD_DX12 ON)
set(BC_USE_OPENMP OFF)
FetchContent_Declare(DirectXTex
    GIT_REPOSITORY https://github.com/microsoft/DirectXTex.git
    GIT_TAG        may2026
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(DirectXTex)

# SQLiteCpp со встроенным sqlite3 собирается из исходников в конфигурации движка
set(SQLITECPP_RUN_CPPLINT OFF)
set(SQLITECPP_RUN_CPPCHECK OFF)
set(SQLITECPP_BUILD_EXAMPLES OFF)
set(SQLITECPP_BUILD_TESTS OFF)
set(SQLITECPP_INCLUDE_SCRIPT OFF)
FetchContent_Declare(SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG        3.3.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SQLiteCpp)

# tinyexr читает .exr (ImageFile::load). Его CMakeLists — для тестов самой библиотеки: SOURCE_SUBDIR без CMakeLists
# не даёт FetchContent подключить его, библиотека собирается здесь из tinyexr.cc и встроенного miniz
FetchContent_Declare(tinyexr
    GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
    GIT_TAG        v1.0.13
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-add
)
FetchContent_MakeAvailable(tinyexr)
add_library(tinyexr STATIC
    ${tinyexr_SOURCE_DIR}/tinyexr.cc
    ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c
)
target_include_directories(tinyexr PUBLIC ${tinyexr_SOURCE_DIR} ${tinyexr_SOURCE_DIR}/deps/miniz)
target_compile_definitions(tinyexr PRIVATE _CRT_SECURE_NO_WARNINGS)

# DirectX 12 Agility SDK: рантайм D3D12 (D3D12Core.dll, debug-слой) и заголовки новее системных — пакет NuGet как zip.
# Версия рантайма (экспорт D3D12SDKVersion в DMD3D.cpp) — число после «1.»; DLL копируются в D3D12\ рядом с exe
set(DM_AGILITY_SDK_VERSION 619)
FetchContent_Declare(agilitysdk
    URL            https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.619.6
    DOWNLOAD_NAME  Microsoft.Direct3D.D3D12.1.619.6.zip
    SOURCE_SUBDIR  do-not-add
)
# DXC: компилятор HLSL → DXIL (SM 6.6) в процессе; dxil.dll подписывает байткод — обе DLL рядом с exe
FetchContent_Declare(dxc
    URL            https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
    SOURCE_SUBDIR  do-not-add
)
# WinPixEventRuntime: метки PIXBeginEvent / PIXEndEvent в командном списке — области профайлера в PIX
FetchContent_Declare(winpixeventruntime
    URL            https://www.nuget.org/api/v2/package/WinPixEventRuntime/1.0.240308001
    DOWNLOAD_NAME  WinPixEventRuntime.1.0.240308001.zip
    SOURCE_SUBDIR  do-not-add
)
# Dear ImGui с бэкендами Win32 и DX12 (свой CMake у него нет — файлы собираются в движке)
FetchContent_Declare(imgui
    URL            https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip
    SOURCE_SUBDIR  do-not-add
)
# D3D12 Memory Allocator (AMD): память под ресурсы
set(D3D12MA_BUILD_SAMPLE OFF)
FetchContent_Declare(d3d12ma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git
    GIT_TAG        v3.2.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(agilitysdk dxc winpixeventruntime imgui d3d12ma)
set(AGILITY_INCLUDE ${agilitysdk_SOURCE_DIR}/build/native/include)
set(AGILITY_BIN ${agilitysdk_SOURCE_DIR}/build/native/bin/x64)
# Аллокатор — против тех же заголовков D3D12, что движок
target_include_directories(D3D12MemoryAllocator BEFORE PRIVATE ${AGILITY_INCLUDE})

add_executable(DMEngine WIN32
    src/Common/Properties/Property.cpp
    src/Common/Properties/PropertyContainer.cpp
    src/Logger/Logger.cpp
    src/Common/DBConnector.cpp
    src/Engine/Console/ConsoleCommands.cpp
    src/Engine/Console/PropertyCommands.cpp
    src/Engine/Console/RemoteControlServer.cpp
    src/Engine/Graphics/Scene/Scatterer/ScattererPass.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    src/Engine/Graphics/GUI/GUI.cpp
    src/Engine/Graphics/Scene/Scatterer/Scatterer.cpp
    src/Engine/Graphics/Scene/Particle/ParticleSystem.cpp
    src/Engine/Graphics/DMComputeShader.cpp
    src/ObjectLibrary/LibraryLoader.cpp
    src/ObjectLibrary/LibraryLoaderEnvironment.cpp
    src/Engine/Graphics/Scene/Level/LevelSettings.cpp
    src/3rdParty/tinyxml2.cpp
    src/Config/Config.cpp
    src/Engine/Graphics/Scene/Light/DMLight.cpp
    src/Engine/Graphics/Scene/Light/DMLightDriver.cpp
    src/Engine/Graphics/Scene/Light/SunPosition.cpp
    src/Engine/Graphics/Scene/Materials/MaterialStorage.cpp
    src/Engine/Graphics/Common/Storage/DMResourceStorage.cpp
    src/Engine/Graphics/Common/Storage/DMResource.cpp
    src/Engine/Graphics/Scene/Materials/Material.cpp
    src/Engine/Graphics/Scene/Materials/ImpostorMaterial.cpp
    src/Engine/Graphics/Scene/Model/Mesh/AbstractMesh.cpp
    src/Engine/Graphics/ConstantBuffers.cpp
    src/Engine/Graphics/Scene/Materials/ColorMaterial.cpp
    src/Engine/Graphics/Scene/Materials/PBRMaterial.cpp
    src/Engine/Graphics/PostProcess.cpp
    src/Engine/Graphics/D3D/GpuProfiler.cpp
    src/Engine/Graphics/FullscreenShader.cpp
    src/Engine/Graphics/Scene/Sky/SkyAtmosphere.cpp
    src/Engine/Graphics/Scene/Wind/Wind.cpp
    src/Engine/Graphics/Scene/Water/WaterSimulation.cpp
    src/Engine/Graphics/ShaderProgram.cpp
    src/Engine/Graphics/Scene/Materials/TextureMaterial.cpp
    src/Engine/Graphics/D3D/DMStructuredBuffer.cpp
    src/DMSystem.cpp
    src/Engine/Graphics/Common/DMTransform.cpp
    src/Engine/Graphics/D3D/DMD3D.cpp
    src/Engine/Graphics/D3D/DMD3DPasses.cpp
    src/Engine/Graphics/D3D/DMD3DPipelines.cpp
    src/Engine/Graphics/D3D/DMD3DResources.cpp
    src/Engine/Graphics/D3D/DMD3DCommands.cpp
    src/Engine/Graphics/D3D/RenderTarget.cpp
    src/Engine/Graphics/D3D/TextureImages.cpp
    src/Engine/Graphics/D3D/ConstantRing.cpp
    src/Engine/Graphics/D3D/ShaderCompiler.cpp
    src/Engine/Graphics/D3D/CubeTarget.cpp
    src/Engine/Graphics/Scene/Sky/SkyLight.cpp
    src/Engine/Graphics/Scene/Sky/HDRIBackdrop.cpp
    src/Engine/Graphics/D3D/DMSamplerState.cpp
    src/Engine/Graphics/DMGraphics.cpp
    src/Engine/Graphics/Renderer.cpp
    src/Engine/Graphics/ShadowCascades.cpp
    src/Engine/Graphics/SceneTargets.cpp
    src/Engine/Graphics/Scene/Camera/DMCamera.cpp
    src/Engine/Graphics/Scene/Camera/DMFrustum.cpp
    src/Engine/Graphics/Scene/Scene.cpp
    src/Engine/Graphics/Scene/Sky/SkySphere.cpp
    src/Engine/Graphics/Scene/Model/DMModel.cpp
    src/Engine/Graphics/Scene/Model/Mesh/DMMesh.cpp
    src/Engine/Graphics/Scene/Model/Mesh/MeshLoader.cpp
    src/Engine/Graphics/Scene/Model/Mesh/MeshStorage.cpp
    src/Engine/Graphics/Scene/Model/ModelStorage.cpp
    src/Engine/Graphics/Scene/Model/ModelInstances.cpp
    src/Engine/Graphics/Scene/Terrain/GridMesh.cpp
    src/Engine/Graphics/Scene/Terrain/CDLODTerrain.cpp
    src/Engine/Graphics/Scene/Terrain/TerrainEdits.cpp
    src/Engine/Graphics/Scene/Terrain/TerrainMaterial.cpp
    src/Engine/Graphics/Scene/Texture/DMTexture.cpp
    src/Engine/Graphics/Scene/Texture/DMTextureStorage.cpp
    src/Engine/Graphics/Scene/VertexPool.cpp
    src/Engine/Graphics/System.cpp
    src/Engine/Input/Input.cpp
    src/Engine/Input/KeyEventNotifier.cpp
    src/main.cpp
    src/Common/Utils/DMTimer.cpp
    src/Common/Utils/utilites.cpp
    src/Engine/Graphics/Scene/Texture/ImageFile.cpp
    src/Engine/Graphics/Scene/Texture/ImageMips.cpp
)

# Заголовки D3D12 Agility SDK — раньше Windows SDK (BEFORE), иначе d3d12.h возьмётся системный
target_include_directories(DMEngine BEFORE PRIVATE ${AGILITY_INCLUDE} ${AGILITY_INCLUDE}/d3dx12)
# Include-пути от корня проекта и основных каталогов движка: отсюда "Utils\DMTimer.h", "Materials\Material.h" и т. п.
target_include_directories(DMEngine PRIVATE
    ${dxc_SOURCE_DIR}/inc
    ${winpixeventruntime_SOURCE_DIR}/Include/WinPixEventRuntime
    ${imgui_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/3rdParty
    ${CMAKE_SOURCE_DIR}/src/Common
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics/Common
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics/Scene
)

# Символьный набор MultiByte (_MBCS); NOMINMAX убирает макросы min / max из windows.h.
# Исходники в UTF-8 без BOM: /utf-8 задаёт кодировку и исходников, и строковых литералов.
target_compile_definitions(DMEngine PRIVATE HOME_RUN _MBCS NOMINMAX DM_AGILITY_SDK_VERSION=${DM_AGILITY_SDK_VERSION})
target_compile_options(DMEngine PRIVATE /W3 /wd4005 /utf-8)
# Карта линковки рядом с exe: адрес сбоя из журнала Windows (смещение в модуле) → функция
target_link_options(DMEngine PRIVATE /MAP)

target_link_libraries(DMEngine PRIVATE
    DirectXTex
    SQLiteCpp
    tinyexr
    D3D12MemoryAllocator
    ${dxc_SOURCE_DIR}/lib/x64/dxcompiler.lib
    ${winpixeventruntime_SOURCE_DIR}/bin/x64/WinPixEventRuntime.lib
    d3d12 dxgi dinput8 dxguid winmm windowscodecs
    d3dcompiler # только шейдеры imgui_impl_dx12 (D3DCompile); шейдеры движка — DXC
)

# Рантайм рядом с exe: Agility SDK в D3D12\ (D3D12SDKPath), DXC и PIX — в каталог exe
add_custom_command(TARGET DMEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:DMEngine>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AGILITY_BIN}/D3D12Core.dll" "${AGILITY_BIN}/d3d12SDKLayers.dll" "$<TARGET_FILE_DIR:DMEngine>/D3D12"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dxc_SOURCE_DIR}/bin/x64/dxcompiler.dll" "${dxc_SOURCE_DIR}/bin/x64/dxil.dll"
            "${winpixeventruntime_SOURCE_DIR}/bin/x64/WinPixEventRuntime.dll" "$<TARGET_FILE_DIR:DMEngine>"
    COMMENT "Copying D3D12 Agility SDK, DXC and PIX runtime next to the executable"
    VERBATIM
)
