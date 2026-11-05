// OpenGL ES 3.0 everywhere: Emscripten maps it onto WebGL2, natively glad loads it.
#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <glad/gles2.h>
#endif

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
