# Interactive viewer on OpenGL ES 3.0, so the same code runs natively (GLFW + glad)
# and in the browser on WebGL2 (Emscripten):
#
#   cmake -S . -B build -DTINYSUBDIV_BUILD_TESTBED=ON
#   emcmake cmake -S . -B build-web -DTINYSUBDIV_BUILD_TESTBED=ON

enable_language(C)
include(FetchContent)

FetchContent_Declare(
   imgui
   URL https://github.com/ocornut/imgui/archive/refs/tags/v1.93.0.zip
   URL_HASH SHA256=e21907fd8dd5ea59aa00538632a02ad0a3ededb4ddc533b94702331fbcd6c470
   DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# third-party code, compiled without our warning flags
add_library(
   tinyverlet_testbed_third_party STATIC
   third_party/stb/stb_image_write.c
   ${imgui_SOURCE_DIR}/imgui.cpp
   ${imgui_SOURCE_DIR}/imgui_draw.cpp
   ${imgui_SOURCE_DIR}/imgui_tables.cpp
   ${imgui_SOURCE_DIR}/imgui_widgets.cpp
   ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
   ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_compile_features(tinyverlet_testbed_third_party PUBLIC cxx_std_23)
target_compile_definitions(tinyverlet_testbed_third_party PUBLIC IMGUI_IMPL_OPENGL_ES3)
target_include_directories(
   tinyverlet_testbed_third_party SYSTEM PUBLIC third_party/stb ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends
                                               
)

add_executable(tinyverlet_testbed main.cpp renderer.cpp scenes.cpp ui.cpp)
target_link_libraries(tinyverlet_testbed PRIVATE tinyverlet::tinyverlet tinyverlet_warnings tinyverlet_testbed_third_party)

if(EMSCRIPTEN)
   target_link_options(
      tinyverlet_testbed_third_party PUBLIC -sUSE_GLFW=3 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2
      -sALLOW_MEMORY_GROWTH=1
   )
   set_target_properties(tinyverlet_testbed PROPERTIES SUFFIX ".html" LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
   target_link_options(tinyverlet_testbed PRIVATE --shell-file ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
else()
   set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
   set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
   set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
   set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

   FetchContent_Declare(
      glfw
      URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
      URL_HASH SHA256=b5ec004b2712fd08e8861dc271428f048775200a2df719ccf575143ba749a3e9
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
   )
   FetchContent_MakeAvailable(glfw)

   # glad loads OpenGL ES 3.0; the shim lets ImGui's ES3 backend include <GLES3/gl3.h>
   target_sources(tinyverlet_testbed_third_party PRIVATE third_party/glad/src/gles2.c)
   target_include_directories(
      tinyverlet_testbed_third_party SYSTEM PUBLIC third_party/glad/include third_party/gles3_shim
   )
   target_link_libraries(tinyverlet_testbed_third_party PUBLIC glfw)
endif()
