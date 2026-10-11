cmake_minimum_required(VERSION 3.16)
project(TuxTools LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(TuxTools WIN32
    src/main.cpp
    src/report.cpp
    src/gl_collector.cpp
    src/vulkan_collector.cpp
    src/ui_controls.cpp
    src/theme.h
    src/dpi.h
    src/render/render_tool.h
    src/render/test_content.h
    src/render/test_content.cpp
    src/render/dx9_tool.cpp
    src/render/dx10_tool.cpp
    src/render/dx11_tool.cpp
    src/render/dx12_tool.cpp
    src/render/gl_tool.cpp
    src/render/vk_tool.cpp
    src/render/vk_cube.vert.inc
    src/render/vk_cube.frag.inc
    src/render/vk_cube.mesh.inc
    src/render/vk_cube.texture.inc
    res/app.rc
    res/resource.h
    res/Application.manifest
    res/app.ico
    res/penguin.ico
)

target_include_directories(TuxTools PRIVATE
    src
    src/render
    res
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(TuxTools PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(TuxTools PRIVATE
    comctl32
    gdi32
    opengl32
    user32
    shell32
    uxtheme
    dwmapi
    d3d9
    d3d10
    d3d11
    d3d12
    dxgi
    d3dcompiler
    ole32
)

# 交付产物叫 Tux-Tools.exe，所以输出名用这个，而不是 CMake 目标名。
set_target_properties(TuxTools PROPERTIES
    OUTPUT_NAME "Tux-Tools"
)

if(MINGW)
    target_compile_options(TuxTools PRIVATE -municode)
    target_link_options(TuxTools PRIVATE
        -mwindows -municode -static -static-libgcc -static-libstdc++)
endif()
