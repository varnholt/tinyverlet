cmake_minimum_required(VERSION 3.21)

project(
   tinyverlet
   VERSION 1.0.0
   DESCRIPTION "Verlet integration with distance constraints, C++23, single header"
   HOMEPAGE_URL "https://github.com/varnholt/tinyverlet"
   LANGUAGES CXX
)

option(TINYVERLET_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(TINYVERLET_BUILD_TESTBED "Build the OpenGL ES 3 / WebGL2 testbed (fetches GLFW and Dear ImGui)" OFF)
option(TINYVERLET_INSTALL "Generate the install target" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

if(EMSCRIPTEN)
   # tinyverlet reports bad input with exceptions; Emscripten disables them by default
   add_compile_options(-fexceptions)
   add_link_options(-fexceptions)
endif()

add_library(tinyverlet INTERFACE)
add_library(tinyverlet::tinyverlet ALIAS tinyverlet)

target_include_directories(
   tinyverlet INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(tinyverlet INTERFACE cxx_std_23)

if(TINYVERLET_BUILD_TESTS OR TINYVERLET_BUILD_TESTBED)
   add_library(tinyverlet_warnings INTERFACE)
   if(MSVC)
      target_compile_options(tinyverlet_warnings INTERFACE /W4 /WX /permissive- /utf-8)
   else()
      target_compile_options(tinyverlet_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
   endif()
endif()

if(TINYVERLET_BUILD_TESTBED)
   add_subdirectory(testbed)
endif()

if(TINYVERLET_BUILD_TESTS)
   enable_testing()
   add_subdirectory(tests)
endif()

if(TINYVERLET_INSTALL)
   include(CMakePackageConfigHelpers)

   install(TARGETS tinyverlet EXPORT tinyverletTargets)
   install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
   install(
      EXPORT tinyverletTargets
      NAMESPACE tinyverlet::
      FILE tinyverletConfig.cmake
      DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinyverlet
   )

   write_basic_package_version_file(
      ${PROJECT_BINARY_DIR}/tinyverletConfigVersion.cmake COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT
   )
   install(FILES ${PROJECT_BINARY_DIR}/tinyverletConfigVersion.cmake DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/tinyverlet)
endif()
