# =============================================================================
# OpenGLWorkspaceTargets.cmake — 子项目共用 target 辅助函数
# =============================================================================
#
# ogl_ws_set_run_output(target) — exe 输出到 run/Debug|Release/<target>/（按配置分目录）
# ogl_ws_link_assimp(target)     — 链接 Assimp + POST_BUILD 拷贝运行时 DLL
#
# =============================================================================

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()

function(_ogl_ws_run_subdir_for_config cfg_name out_var)
    if(cfg_name STREQUAL "Debug")
        set(${out_var} "Debug" PARENT_SCOPE)
    else()
        set(${out_var} "Release" PARENT_SCOPE)
    endif()
endfunction()

function(ogl_ws_set_run_output target_name)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_cfg ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${_cfg} _cfg_upper)
            _ogl_ws_run_subdir_for_config("${_cfg}" _subdir)
            set(_run_dir "${OGL_WS_ROOT}/run/${_subdir}/${target_name}")
            set_target_properties(${target_name} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${_cfg_upper} "${_run_dir}"
                VS_DEBUGGER_WORKING_DIRECTORY_${_cfg_upper} "${_run_dir}")
        endforeach()
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_subdir "Debug")
        else()
            set(_subdir "Release")
        endif()
        set(_run_dir "${OGL_WS_ROOT}/run/${_subdir}/${target_name}")
        set_target_properties(${target_name} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${_run_dir}"
            VS_DEBUGGER_WORKING_DIRECTORY "${_run_dir}")
    endif()
endfunction()

function(ogl_ws_deploy_module_dlls target_name)
    if(NOT WIN32)
        return()
    endif()

    set(_copy_script "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake")
    string(REPLACE ";" "\\;" _MODULE_DLL_ROOTS_ESC "${OGL_WS_MODULE_DLL_ROOTS}")

    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=$<IF:$<CONFIG:Debug>,debug/bin,bin>
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${_copy_script}"
            COMMENT "Copy runtime DLLs from modules"
        )
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_binsub "debug/bin")
        else()
            set(_binsub "bin")
        endif()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=${_binsub}
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${_copy_script}"
            COMMENT "Copy runtime DLLs from modules"
        )
    endif()
endfunction()

function(ogl_ws_link_assimp target_name)
    set(_assimp_root "${OGL_WS_ASSIMP_ROOT}")
    set(_assimp_lib_release "${_assimp_root}/lib/assimp-vc143-mt.lib")
    set(_assimp_lib_debug "${_assimp_root}/debug/lib/assimp-vc143-mtd.lib")

    if(NOT EXISTS "${_assimp_root}/include/assimp/Importer.hpp")
        message(FATAL_ERROR "Assimp headers not found under ${_assimp_root}")
    endif()
    if(NOT EXISTS "${_assimp_lib_release}")
        message(FATAL_ERROR "Assimp import library not found: ${_assimp_lib_release}")
    endif()

    target_include_directories(${target_name} PRIVATE "${_assimp_root}/include")
    if(EXISTS "${_assimp_lib_debug}")
        target_link_libraries(${target_name} PRIVATE
            "$<$<CONFIG:Debug>:${_assimp_lib_debug}>"
            "$<$<NOT:$<CONFIG:Debug>>:${_assimp_lib_release}>"
        )
    else()
        target_link_libraries(${target_name} PRIVATE "${_assimp_lib_release}")
    endif()
    ogl_ws_deploy_module_dlls(${target_name})
endfunction()

function(ogl_ws_deploy_glfw_dll target_name)
    if(GLFW3_DLL AND EXISTS "${GLFW3_DLL}")
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GLFW3_DLL}"
                "$<TARGET_FILE_DIR:${target_name}>"
            COMMENT "Deploy GLFW DLL next to ${target_name}"
        )
    endif()
endfunction()
