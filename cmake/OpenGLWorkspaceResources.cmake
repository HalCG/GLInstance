# Copy static project resources (models, shaders, etc.) next to the executable.

function(ogl_ws_deploy_static_resources target_name)
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:${target_name}>/resources"
        COMMENT "Deploy static resources for ${target_name}"
    )
endfunction()
