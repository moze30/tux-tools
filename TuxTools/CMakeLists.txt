cmake_minimum_required(VERSION 3.16)
project(TuxTools LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The decoder cores are C. C11 is what the upstream project targets and what
# these sources are written against.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(TuxTools WIN32
    src/main.cpp
    src/report.cpp
    src/gl_collector.cpp
    src/vulkan_collector.cpp
    src/ui_controls.cpp
    src/app_log.h
    src/app_log.cpp
    src/settings.h
    src/settings.cpp
    # Interface language: the table of translations and the environment lookup.
    src/i18n.h
    src/i18n.cpp
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
    # Decode test. The five decoder sources are DSMF-Decode-Test (GPL-3.0) used
    # unmodified; decode_log.c adapts their log calls to this project and
    # decode_test.cpp is the shell around them.
    src/decode/decode_test.h
    src/decode/decode_test.cpp
    src/decode/decode_log.c
    src/decode/log.h
    src/decode/directshow_decoder.h
    src/decode/directshow_decoder.c
    src/decode/mf_decoder.h
    src/decode/mf_decoder.c
    src/decode/dxva2_helper.h
    src/decode/dxva2_helper.c
    src/decode/d3d11_video_helper.h
    src/decode/d3d11_video_helper.c
    src/decode/d3d12_video_helper.h
    src/decode/d3d12_video_helper.c
    res/app.rc
    res/resource.h
    res/Application.manifest
    res/app.ico
    res/penguin.ico
)

target_include_directories(TuxTools PRIVATE
    src
    src/render
    src/decode
    res
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)

target_compile_definitions(TuxTools PRIVATE
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# The decoder cores are C and call the COM interfaces through the flat macros
# (IMFSample_Release, ID3D11Device_QueryInterface, ...), which only exist when
# COBJMACROS is defined. It must not be set for the C++ translation units:
# there the same names are member functions of the interface types, and the C
# macros would collide with them. WINVER is pinned to Windows 7 exactly as the
# upstream Makefile does, which is the floor the sources are written against.
target_compile_definitions(TuxTools PRIVATE
    $<$<COMPILE_LANGUAGE:C>:COBJMACROS>
    $<$<COMPILE_LANGUAGE:C>:WINVER=0x0601>
    $<$<COMPILE_LANGUAGE:C>:_WIN32_WINNT=0x0601>
)

# msimg32 provides AlphaBlend, used to dim the client area behind the about
# overlay. It is a Windows component.
target_link_libraries(TuxTools PRIVATE
    # version.dll, for reading kernel32's version resource: it is how the log
    # reports which Windows - or which Wine build - it is running on.
    version
    msimg32
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
    oleaut32
    uuid
    # Decode test: DirectShow and Media Foundation are Windows components, so
    # the "only system DLLs" rule the project holds to is unchanged.
    quartz
    strmiids
    amstrmid
    dxva2
    dxguid
    mfplat
    mf
    mfreadwrite
    mfuuid
    evr
    comdlg32
    shlwapi
    winmm
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
