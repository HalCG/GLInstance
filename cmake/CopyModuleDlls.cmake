# 将 modules 下某配置目录中的全部 .dll 复制到 exe 目录
# 用法: cmake -Droots="dir1;dir2" -Dbinsub=debug/bin -Doutdir=... -P CopyModuleDlls.cmake
if(NOT roots OR NOT DEFINED binsub OR NOT outdir)
    message(FATAL_ERROR "CopyModuleDlls.cmake: need -Droots, -Dbinsub, -Doutdir")
endif()

foreach(_root IN LISTS roots)
    if(binsub AND NOT binsub STREQUAL ".")
        set(_search_dir "${_root}/${binsub}")
    else()
        set(_search_dir "${_root}")
    endif()
    if(NOT IS_DIRECTORY "${_search_dir}")
        continue()
    endif()
    file(GLOB _dlls "${_search_dir}/*.dll")
    foreach(_dll IN LISTS _dlls)
        get_filename_component(_name "${_dll}" NAME)
        message(STATUS "Copy runtime DLL: ${_name} -> ${outdir}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}" "${outdir}"
            RESULT_VARIABLE _copy_result
        )
        if(_copy_result)
            message(WARNING "Failed to copy ${_dll} (code ${_copy_result})")
        endif()
    endforeach()
endforeach()
