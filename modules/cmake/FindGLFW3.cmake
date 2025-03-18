# FindGLFW3.cmake
# 在 modules/glfw-3.4.bin.WIN64 中定位 GLFW 3.4 预编译库（Windows 为主）

set(_GLFW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../glfw-3.4.bin.WIN64")

if(NOT EXISTS "${_GLFW_ROOT}/include/GLFW/glfw3.h")
    set(GLFW3_FOUND FALSE)
    if(GLFW3_FIND_REQUIRED)
        message(FATAL_ERROR
            "GLFW3 not found: expected ${_GLFW_ROOT}/include/GLFW/glfw3.h\n"
            "Download GLFW 3.4 Windows precompiled binaries and extract to modules/glfw-3.4.bin.WIN64")
    endif()
    return()
endif()

set(GLFW3_INCLUDE_DIR "${_GLFW_ROOT}/include")

# 按编译器选择预编译库目录
# Clang on Windows (x86_64-pc-windows-msvc) 链接 MSVC 版 glfw3.lib
if(MSVC)
    if(MSVC_VERSION GREATER_EQUAL 1930)
        set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2022")
    elseif(MSVC_VERSION GREATER_EQUAL 1920)
        set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2019")
    elseif(MSVC_VERSION GREATER_EQUAL 1910)
        set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2017")
    elseif(MSVC_VERSION GREATER_EQUAL 1900)
        set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2015")
    else()
        set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2013")
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WIN32)
    set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-vc2022")
elseif(MINGW)
    set(_GLFW_LIB_DIR "${_GLFW_ROOT}/lib-mingw-w64")
endif()

# 优先静态库 glfw3.lib；MinGW 用 libglfw3.a
if(EXISTS "${_GLFW_LIB_DIR}/glfw3.lib")
    set(GLFW3_LIBRARY "${_GLFW_LIB_DIR}/glfw3.lib")
    set(GLFW3_DLL "")
elseif(EXISTS "${_GLFW_LIB_DIR}/libglfw3.a")
    set(GLFW3_LIBRARY "${_GLFW_LIB_DIR}/libglfw3.a")
    set(GLFW3_DLL "${_GLFW_LIB_DIR}/glfw3.dll")
else()
    set(GLFW3_LIBRARY "${_GLFW_LIB_DIR}/glfw3dll.lib")
    set(GLFW3_DLL "${_GLFW_LIB_DIR}/glfw3.dll")
endif()

if(NOT EXISTS "${GLFW3_LIBRARY}")
    set(GLFW3_FOUND FALSE)
    if(GLFW3_FIND_REQUIRED)
        message(FATAL_ERROR "GLFW3 library not found under ${_GLFW_LIB_DIR}")
    endif()
    return()
endif()

# 静态链接 GLFW 在 Windows 上需要的系统库
if(WIN32)
    list(APPEND GLFW3_LIBRARY gdi32 user32 shell32)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GLFW3
    REQUIRED_VARS GLFW3_LIBRARY GLFW3_INCLUDE_DIR
)

mark_as_advanced(GLFW3_INCLUDE_DIR GLFW3_LIBRARY GLFW3_DLL)
