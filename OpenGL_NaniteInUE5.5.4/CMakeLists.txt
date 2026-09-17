project(OpenGL_NaniteInUE5.5.4 C CXX)

set(NANITE_SRC
    src/App/main.cpp
    src/Core/utils.cpp
    src/Core/utils.h
    src/Math/float4.cpp
    src/Math/float4.h
    src/Math/matrix4.cpp
    src/Math/matrix4.h
    src/Math/quaternion.cpp
    src/Math/quaternion.h
    src/Platform/oglcontext.cpp
    src/Platform/oglcontext.h
    src/Camera/trackball_camera.cpp
    src/Camera/trackball_camera.h
    src/Render/RenderPass.cpp
    src/Render/RenderPass.h
    src/Render/StaticMesh.cpp
    src/Render/StaticMesh.h
    src/Render/fullscreenquad.cpp
    src/Render/fullscreenquad.h
    src/Scene/scene.cpp
    src/Scene/scene.h
)

add_executable(${PROJECT_NAME} ${NANITE_SRC})

# Define Unicode character set matching Nanite.vcxproj
target_compile_definitions(${PROJECT_NAME} PRIVATE UNICODE _UNICODE)

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/App"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Math"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Platform"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Camera"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Render"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Scene"
    "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty"
)

# Independent ThirdParty dependencies: GLEW and system libs
set(GLEW_LIB "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/GLEW/lib/x64/glew32.lib")

target_link_libraries(${PROJECT_NAME} PRIVATE
    ${GLEW_LIB}
    OpenGL::GL
    winmm
)

# Debugger Working Directory set to target output folder
set_target_properties(${PROJECT_NAME} PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
)

# Post-build step: Copy Res/ folder and glew32.dll next to executable
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/Res"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/Res"
    COMMENT "Copying Res resources to $<TARGET_FILE_DIR:${PROJECT_NAME}>/Res"
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/GLEW/bin/x64/glew32.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/glew32.dll"
    COMMENT "Copying glew32.dll to output folder"
)
