# 仅当源文件为 .dll 时复制到目标目录（静态 .lib 跳过）
if(NOT file OR NOT dir)
    message(FATAL_ERROR "CopyIfDll.cmake: missing -Dfile or -Ddir")
endif()
get_filename_component(_ext "${file}" EXT)
if(_ext STREQUAL ".dll")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${file}" "${dir}"
    )
endif()
