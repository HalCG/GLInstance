cmake_minimum_required(VERSION 3.10)

project(OpenGLWorkspace LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

set(OGL_WS_ROOT "${CMAKE_SOURCE_DIR}")
set(OGL_WS_MODULES_DIR "${OGL_WS_ROOT}/modules")
set(OGL_WS_CMAKE_DIR "${OGL_WS_ROOT}/cmake")

include("${OGL_WS_CMAKE_DIR}/OpenGLWorkspaceDependencies.cmake")
include("${OGL_WS_CMAKE_DIR}/OpenGLWorkspaceOutput.cmake")
include("${OGL_WS_CMAKE_DIR}/OpenGLWorkspaceTargets.cmake")
include("${OGL_WS_CMAKE_DIR}/OpenGLWorkspaceDeploy.cmake")

# subprojects
add_subdirectory(OpenGL_Anti_Aliasing)
add_subdirectory(OpenGL_OIT_Depth_Peeling)
add_subdirectory(OpenGL_OIT_Stochastic_Transparency)
add_subdirectory(OpenGL_OIT_Linked_list)
