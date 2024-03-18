# =============================================================================
# OpenGLWorkspaceOutput.cmake — 构建产物路径
# =============================================================================
#
# 运行目录（exe / dll / resources）：run/Debug|Release/<Target>/  — 见 ogl_ws_set_run_output()
# CMake 中间产物（.obj / .lib / 工程文件）：out/build/<preset>/
#
# =============================================================================

if(CMAKE_CONFIGURATION_TYPES)
    foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${_cfg} _cfg_upper)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_upper} "${CMAKE_BINARY_DIR}/lib/${_cfg}")
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg_upper} "${CMAKE_BINARY_DIR}/lib/${_cfg}")
    endforeach()
else()
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
endif()

# 可执行文件路径由各 Demo 调用 ogl_ws_set_run_output(target) 单独设置到 run/Debug|Release/<target>/
