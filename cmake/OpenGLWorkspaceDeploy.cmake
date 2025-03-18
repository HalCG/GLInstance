# Legacy deploy helper for independent subprojects (Nanite / PBR 等).
# 标准 Demo 请使用 ogl_ws_set_run_output + ogl_ws_link_assimp + ogl_ws_deploy_static_resources。

if(POLICY CMP0175)
    cmake_policy(SET CMP0175 OLD)
endif()

function(ogl_ws_deploy_runtime target_name resource_subdir)
    # Prefer per-project resources/ next to the calling CMakeLists.txt.
    set(_project_resources "${CMAKE_CURRENT_LIST_DIR}/resources")
    if(IS_DIRECTORY "${_project_resources}")
        set(_resource_src "${_project_resources}")
        set(_resource_label "${target_name}")
    else()
        set(_resource_src "${OGL_WS_RESOURCES_DIR}/${resource_subdir}")
        set(_resource_label "${resource_subdir}")
    endif()
    if(NOT IS_DIRECTORY "${_resource_src}")
        message(FATAL_ERROR
            "Resource directory not found: ${_resource_src}\n"
            "Expected layout: <project>/resources/ or resources/${resource_subdir}/..."
        )
    endif()

    # Resources are copied next to the exe; relative paths like ./resources/...
    # require the process CWD to be the executable directory (VS defaults to ProjectDir).
    set_target_properties(${target_name} PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${target_name}>"
    )

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${_resource_src}"
            "$<TARGET_FILE_DIR:${target_name}>/resources"
        COMMENT "Copy resources (${_resource_label}) next to ${target_name}"
    )

    if(NOT WIN32)
        return()
    endif()

    string(REPLACE ";" "\\;" _MODULE_DLL_ROOTS_ESC "${OGL_WS_MODULE_DLL_ROOTS}")

    if(CMAKE_CONFIGURATION_TYPES)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=debug/bin
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake"
            COMMENT "Copy Debug runtime DLLs from modules"
            CONFIGURATIONS Debug
        )
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=bin
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake"
            COMMENT "Copy Release runtime DLLs from modules"
            CONFIGURATIONS Release RelWithDebInfo MinSizeRel
        )
    else()
        if(CMAKE_BUILD_TYPE STREQUAL "Debug")
            set(_MODULE_BIN_SUBDIR "debug/bin")
        else()
            set(_MODULE_BIN_SUBDIR "bin")
        endif()
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                -Droots=${_MODULE_DLL_ROOTS_ESC}
                -Dbinsub=${_MODULE_BIN_SUBDIR}
                -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                -P "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake"
            COMMENT "Copy runtime DLLs from modules"
        )
    endif()

    if(DEFINED ENV{VCPKG_ROOT})
        set(_VCPKG_BIN "$ENV{VCPKG_ROOT}/installed/x64-windows/bin")
        set(_VCPKG_DEBUG_BIN "$ENV{VCPKG_ROOT}/installed/x64-windows/debug/bin")
        if(CMAKE_CONFIGURATION_TYPES)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -Droots="${_VCPKG_DEBUG_BIN}"
                    -Dbinsub=
                    -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                    -P "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake"
                CONFIGURATIONS Debug
            )
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND}
                    -Droots="${_VCPKG_BIN}"
                    -Dbinsub=
                    -Doutdir=$<TARGET_FILE_DIR:${target_name}>
                    -P "${OGL_WS_CMAKE_DIR}/CopyModuleDlls.cmake"
                CONFIGURATIONS Release RelWithDebInfo MinSizeRel
            )
        endif()
    endif()
endfunction()
