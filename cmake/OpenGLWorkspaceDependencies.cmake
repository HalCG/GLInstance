# Shared third-party dependencies for all OpenGL workspace subprojects.
# Included from the root CMakeLists.txt only.

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${OGL_WS_MODULES_DIR}/cmake")
find_package(GLFW3 REQUIRED)
find_package(OpenGL REQUIRED)

set(OGL_WS_GLM_INCLUDE_DIR "${OGL_WS_MODULES_DIR}/glm-1.0.1")
if(NOT EXISTS "${OGL_WS_GLM_INCLUDE_DIR}/glm/glm.hpp")
    message(FATAL_ERROR
        "GLM not found at ${OGL_WS_GLM_INCLUDE_DIR}\n"
        "Ensure modules/glm-1.0.1 is available under ${OGL_WS_MODULES_DIR}."
    )
endif()

# ---------------------------------------------------------------------------
# Assimp
# ---------------------------------------------------------------------------
set(ASSIMP_ROOT "${OGL_WS_MODULES_DIR}/assimp_x64-windows")
set(OGL_WS_ASSIMP_ROOT "${ASSIMP_ROOT}")
set(_assimp_from_modules FALSE)

if(EXISTS "${ASSIMP_ROOT}/include/assimp/Importer.hpp")
    find_library(ASSIMP_LIBRARY_RELEASE
        NAMES assimp-vc143-mt assimp-vc142-mt assimp
        PATHS "${ASSIMP_ROOT}/lib"
        NO_DEFAULT_PATH
    )
    find_library(ASSIMP_LIBRARY_DEBUG
        NAMES assimp-vc143-mtd assimp-vc142-mtd assimp
        PATHS "${ASSIMP_ROOT}/debug/lib"
        NO_DEFAULT_PATH
    )

    if(ASSIMP_LIBRARY_RELEASE OR ASSIMP_LIBRARY_DEBUG)
        find_file(ASSIMP_DLL_RELEASE
            NAMES assimp-vc143-mt.dll assimp-vc142-mt.dll assimp.dll
            PATHS "${ASSIMP_ROOT}/bin"
            NO_DEFAULT_PATH
        )
        find_file(ASSIMP_DLL_DEBUG
            NAMES assimp-vc143-mtd.dll assimp-vc142-mtd.dll
            PATHS "${ASSIMP_ROOT}/debug/bin"
            NO_DEFAULT_PATH
        )

        if(NOT TARGET assimp::assimp)
            add_library(assimp::assimp SHARED IMPORTED GLOBAL)
            set_target_properties(assimp::assimp PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${ASSIMP_ROOT}/include"
            )
            if(ASSIMP_LIBRARY_RELEASE)
                set_target_properties(assimp::assimp PROPERTIES
                    IMPORTED_IMPLIB_RELEASE "${ASSIMP_LIBRARY_RELEASE}"
                    IMPORTED_LOCATION_RELEASE "${ASSIMP_DLL_RELEASE}"
                )
            endif()
            if(ASSIMP_LIBRARY_DEBUG)
                set_target_properties(assimp::assimp PROPERTIES
                    IMPORTED_IMPLIB_DEBUG "${ASSIMP_LIBRARY_DEBUG}"
                    IMPORTED_LOCATION_DEBUG "${ASSIMP_DLL_DEBUG}"
                )
            endif()
        endif()
        set(_assimp_from_modules TRUE)
    endif()
endif()

if(NOT _assimp_from_modules)
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(assimp CONFIG REQUIRED)
    else()
        message(FATAL_ERROR
            "Assimp not found under ${ASSIMP_ROOT}/lib (and debug/lib).\n"
            "Either copy assimp .lib/.dll into modules/assimp_x64-windows, "
            "or set VCPKG_ROOT and install assimp via vcpkg."
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Polyclipping (Depth Peeling / assimp dependency)
# ---------------------------------------------------------------------------
set(POLYCLIPPING_ROOT "${OGL_WS_MODULES_DIR}/polyclipping_x64-windows")
set(_polyclipping_from_modules FALSE)

find_library(POLYCLIPPING_LIBRARY_RELEASE
    NAMES polyclipping
    PATHS "${POLYCLIPPING_ROOT}/lib"
    NO_DEFAULT_PATH
)
find_library(POLYCLIPPING_LIBRARY_DEBUG
    NAMES polyclipping
    PATHS "${POLYCLIPPING_ROOT}/debug/lib"
    NO_DEFAULT_PATH
)

if(POLYCLIPPING_LIBRARY_RELEASE OR POLYCLIPPING_LIBRARY_DEBUG)
    if(NOT TARGET polyclipping::polyclipping)
        add_library(polyclipping::polyclipping STATIC IMPORTED GLOBAL)
        set_target_properties(polyclipping::polyclipping PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${POLYCLIPPING_ROOT}/include"
        )
        if(POLYCLIPPING_LIBRARY_RELEASE)
            set_target_properties(polyclipping::polyclipping PROPERTIES
                IMPORTED_LOCATION_RELEASE "${POLYCLIPPING_LIBRARY_RELEASE}"
            )
        endif()
        if(POLYCLIPPING_LIBRARY_DEBUG)
            set_target_properties(polyclipping::polyclipping PROPERTIES
                IMPORTED_LOCATION_DEBUG "${POLYCLIPPING_LIBRARY_DEBUG}"
            )
        endif()
    endif()
    set(_polyclipping_from_modules TRUE)
endif()

if(NOT _polyclipping_from_modules AND NOT TARGET polyclipping::polyclipping)
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(polyclipping CONFIG REQUIRED)
    else()
        message(STATUS "Polyclipping not found in modules (optional unless linking assimp static chain)")
    endif()
endif()

# ---------------------------------------------------------------------------
# Pugixml (Stochastic Transparency)
# ---------------------------------------------------------------------------
set(PUGIXML_ROOT "${OGL_WS_MODULES_DIR}/pugixml_x64-windows")
set(_pugixml_from_modules FALSE)

find_library(PUGIXML_LIBRARY_RELEASE
    NAMES pugixml
    PATHS "${PUGIXML_ROOT}/lib"
    NO_DEFAULT_PATH
)
find_library(PUGIXML_LIBRARY_DEBUG
    NAMES pugixml
    PATHS "${PUGIXML_ROOT}/debug/lib"
    NO_DEFAULT_PATH
)

if(PUGIXML_LIBRARY_RELEASE OR PUGIXML_LIBRARY_DEBUG)
    find_file(PUGIXML_DLL_RELEASE
        NAMES pugixml.dll
        PATHS "${PUGIXML_ROOT}/bin"
        NO_DEFAULT_PATH
    )
    find_file(PUGIXML_DLL_DEBUG
        NAMES pugixml.dll
        PATHS "${PUGIXML_ROOT}/debug/bin"
        NO_DEFAULT_PATH
    )

    if(NOT TARGET pugixml::pugixml)
        add_library(pugixml::pugixml SHARED IMPORTED GLOBAL)
        set_target_properties(pugixml::pugixml PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${PUGIXML_ROOT}/include"
        )
        if(PUGIXML_LIBRARY_RELEASE)
            set_target_properties(pugixml::pugixml PROPERTIES
                IMPORTED_IMPLIB_RELEASE "${PUGIXML_LIBRARY_RELEASE}"
                IMPORTED_LOCATION_RELEASE "${PUGIXML_DLL_RELEASE}"
            )
        endif()
        if(PUGIXML_LIBRARY_DEBUG)
            set_target_properties(pugixml::pugixml PROPERTIES
                IMPORTED_IMPLIB_DEBUG "${PUGIXML_LIBRARY_DEBUG}"
                IMPORTED_LOCATION_DEBUG "${PUGIXML_DLL_DEBUG}"
            )
        endif()
    endif()
    set(_pugixml_from_modules TRUE)
endif()

if(NOT _pugixml_from_modules AND NOT TARGET pugixml::pugixml)
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VCPKG_ROOT}/installed/x64-windows")
        find_package(pugixml CONFIG QUIET)
    endif()
endif()

# ---------------------------------------------------------------------------
# Static libraries (built once for all subprojects)
# ---------------------------------------------------------------------------
if(NOT TARGET glad)
    add_library(glad STATIC "${OGL_WS_MODULES_DIR}/glad4.6/src/glad.c")
    target_include_directories(glad PUBLIC "${OGL_WS_MODULES_DIR}/glad4.6/include")
endif()

if(NOT TARGET stb_image)
    add_library(stb_image STATIC "${OGL_WS_MODULES_DIR}/stb_image/stb_image_wrap.cpp")
    target_include_directories(stb_image PUBLIC "${OGL_WS_MODULES_DIR}/stb_image")
endif()

if(NOT TARGET imgui)
    add_library(imgui STATIC
        "${OGL_WS_MODULES_DIR}/imgui/imgui.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/imgui_draw.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/imgui_tables.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/imgui_widgets.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/backends/imgui_impl_glfw.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/backends/imgui_impl_opengl3.cpp"
        "${OGL_WS_MODULES_DIR}/imgui/backends/imgui_impl_win32.cpp"
    )
    target_include_directories(imgui PUBLIC
        "${OGL_WS_MODULES_DIR}/imgui"
        "${OGL_WS_MODULES_DIR}/imgui/backends"
        "${GLFW3_INCLUDE_DIR}"
    )
    target_link_libraries(imgui PUBLIC ${GLFW3_LIBRARY} glad OpenGL::GL)
endif()

# DLL roots used by ogl_ws_deploy_runtime()
set(OGL_WS_MODULE_DLL_ROOTS
    "${ASSIMP_ROOT}"
    "${OGL_WS_MODULES_DIR}/pugixml_x64-windows"
    "${OGL_WS_MODULES_DIR}/minizip_x64-windows"
    "${OGL_WS_MODULES_DIR}/jhasse-poly2tri_x64-windows"
    "${OGL_WS_MODULES_DIR}/zlib_x64-windows"
)
