cmake_minimum_required(VERSION 3.10)

project(OpenGL_Anti_Aliasing LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OpenGL_Anti_Aliasing
    main.cpp
    src/AntiAliasingApp.cpp
    src/AATestScene.cpp
    src/SceneRenderer.cpp
    src/Framebuffer.cpp
    src/PostProcess.cpp
    src/TaaPass.cpp
    src/PerfStats.cpp
)
ogl_ws_set_run_output(OpenGL_Anti_Aliasing)

target_include_directories(OpenGL_Anti_Aliasing PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GLFW3_INCLUDE_DIR}
    ${OGL_WS_GLM_INCLUDE_DIR}
    "${OGL_WS_MODULES_DIR}/stb_image"
)

target_link_libraries(OpenGL_Anti_Aliasing PRIVATE
    ${GLFW3_LIBRARY}
    glad
    stb_image
    imgui
    OpenGL::GL
)
ogl_ws_link_assimp(OpenGL_Anti_Aliasing)

include("${CMAKE_SOURCE_DIR}/cmake/OpenGLWorkspaceResources.cmake")
ogl_ws_deploy_static_resources(OpenGL_Anti_Aliasing)
