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

# DirectXTex (загрузка текстур): при первом configure скачивается с GitHub
include(FetchContent)
set(BUILD_TOOLS OFF)
set(BUILD_SAMPLE OFF)
set(BUILD_DX12 OFF)
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

add_executable(DMEngine WIN32
    src/Common/Properties/Property.cpp
    src/Common/Properties/PropertyContainer.cpp
    src/Engine/Graphics/Scene/Shaders/ShaderUtils.cpp
    src/Logger/Logger.cpp
    src/Engine/Graphics/Scene/TextureObjects/CustomTexture.cpp
    src/Engine/Graphics/Scene/TextureObjects/DDSTexture.cpp
    src/Common/DBConnector.cpp
    src/Engine/Graphics/Scene/Scatterer/ScattererPass.cpp
    imGUI/imgui.cpp
    imGUI/imgui_demo.cpp
    imGUI/imgui_draw.cpp
    imGUI/imgui_impl_dx11.cpp
    imGUI/imgui_impl_win32.cpp
    imGUI/imgui_widgets.cpp
    src/3rdParty/ScreenGrab.cpp
    src/Engine/Graphics/GUI/GUI.cpp
    src/Engine/Graphics/Scene/Scatterer/Scatterer.cpp
    src/Engine/Graphics/Scene/Shaders/DMGrassShader.cpp
    src/Engine/Graphics/Scene/Shaders/DMParticleShader.cpp
    src/Engine/Graphics/Scene/Particle/DMParticleSystem.cpp
    src/Engine/Graphics/Scene/Shaders/DMComputeShader.cpp
    src/Engine/Graphics/Scene/Shaders/VertexLight.cpp
    src/ObjectLibrary/LibraryLoader.cpp
    src/3rdParty/tinyxml2.cpp
    src/Config/Config.cpp
    src/Common/ResourceMetaFile.cpp
    src/Engine/Graphics/Pipeline.cpp
    src/Engine/Graphics/Scene/Light/DMLight.cpp
    src/Engine/Graphics/Scene/Light/DMLightDriver.cpp
    src/Engine/Graphics/Scene/Materials/MaterialStorage.cpp
    src/Engine/Graphics/Common/Storage/DMResourceStorage.cpp
    src/Engine/Graphics/Common/Storage/DMResource.cpp
    src/Engine/Graphics/Scene/Materials/Material.cpp
    src/Engine/Graphics/Scene/Model/Mesh/AbstractMesh.cpp
    src/Engine/Graphics/Scene/Shaders/ConstantBuffers.cpp
    src/Engine/Graphics/Scene/Shaders/DMColorShader.cpp
    src/Engine/Graphics/Scene/Shaders/PBRMaterial.cpp
    src/Engine/Graphics/PostProcess.cpp
    src/Engine/Graphics/D3D/GpuProfiler.cpp
    src/Engine/Graphics/Scene/Shaders/FullscreenShader.cpp
    src/Engine/Graphics/Scene/Sky/SkyAtmosphere.cpp
    src/Engine/Graphics/Scene/Shaders/DMShader.cpp
    src/Engine/Graphics/Scene/Shaders/Layout.cpp
    src/Engine/Graphics/Scene/Shaders/TextureShader.cpp
    src/Engine/Graphics/D3D/DMStructuredBuffer.cpp
    src/DMSystem.cpp
    src/Engine/Graphics/Common/DMAABB.cpp
    src/Engine/Graphics/Common/DMTransform.cpp
    src/Engine/Graphics/D3D/DMD3D.cpp
    src/Engine/Graphics/D3D/DMSamplerState.cpp
    src/Engine/Graphics/DMGraphics.cpp
    src/Engine/Graphics/Renderer.cpp
    src/Engine/Graphics/Scene/Camera/DMCamera.cpp
    src/Engine/Graphics/Scene/Camera/DMFrustum.cpp
    src/Engine/Graphics/Scene/Scene.cpp
    src/Engine/Graphics/Scene/Sky/SkySphere.cpp
    src/Engine/Graphics/Scene/Model/DMModel.cpp
    src/Engine/Graphics/Scene/Model/Mesh/DMMesh.cpp
    src/Engine/Graphics/Scene/Model/Mesh/MeshLoader.cpp
    src/Engine/Graphics/Scene/Model/Mesh/MeshStorage.cpp
    src/Engine/Graphics/Scene/Model/ModelStorage.cpp
    src/Engine/Graphics/Scene/Model/ModelQueues.cpp
    src/Engine/Graphics/Scene/Terrain/GridMesh.cpp
    src/Engine/Graphics/Scene/Terrain/CDLODTerrain.cpp
    src/Engine/Graphics/Scene/Terrain/TerrainMaterial.cpp
    src/Engine/Graphics/Scene/TextureObjects/DMTexture.cpp
    src/Engine/Graphics/Scene/TextureObjects/DMTextureStorage.cpp
    src/Engine/Graphics/Scene/VertexPool.cpp
    src/Engine/Graphics/System.cpp
    src/Engine/Input/Input.cpp
    src/Engine/Input/KeyEventNotifier.cpp
    src/main.cpp
    src/Common/Utils/DMTimer.cpp
    src/Common/Utils/utilites.cpp
    src/Engine/Graphics/Scene/TextureObjects/TextureLoader.cpp
)

# Include-пути от корня проекта и основных каталогов движка: отсюда "Utils\DMTimer.h", "Shaders\DMShader.h" и т. п.
target_include_directories(DMEngine PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/3rdParty
    ${CMAKE_SOURCE_DIR}/src/Common
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics/Common
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics/Scene
    ${CMAKE_SOURCE_DIR}/src/Engine/Graphics/Scene/Common
)

# Символьный набор MultiByte (_MBCS); NOMINMAX убирает макросы min / max из windows.h.
# Исходники в UTF-8 без BOM: /utf-8 задаёт кодировку и исходников, и строковых литералов.
target_compile_definitions(DMEngine PRIVATE HOME_RUN _MBCS NOMINMAX)
target_compile_options(DMEngine PRIVATE /W3 /wd4005 /utf-8)

target_link_libraries(DMEngine PRIVATE
    DirectXTex
    SQLiteCpp
    d3d11 dxgi d3dcompiler dinput8 dxguid winmm windowscodecs
)
