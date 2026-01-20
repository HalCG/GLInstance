project(OpenGL_PBR C CXX)

set(PBR_SRC
    main.cpp
    src/Shader.cpp
    src/StaticMesh.cpp
    src/FrameBuffer.cpp
    src/IblPrecomputation.cpp
    src/PbrApp.cpp
)

set(PBR_HEADERS
    include/Camera.hpp
    include/Shader.hpp
    include/StaticMesh.hpp
    include/FrameBuffer.hpp
    include/IblPrecomputation.hpp
    include/PbrApp.hpp
)

add_executable(${PROJECT_NAME} ${PBR_SRC} ${PBR_HEADERS})
ogl_ws_set_run_output(${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${OGL_WS_GLM_INCLUDE_DIR}"
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glad
    ${GLFW3_LIBRARY}
    stb_image
    imgui
    OpenGL::GL
)

ogl_ws_deploy_runtime(${PROJECT_NAME} "")
ogl_ws_deploy_glfw_dll(${PROJECT_NAME})
