// The single translation unit that compiles the PNG decoder.
//
// `stb_image` is a public domain header library: the implementation is
// enabled by defining `STB_IMAGE_IMPLEMENTATION` in exactly one file, which
// is this one. Everything else includes the header generated at configure
// time and calls `stbi_load` without ever seeing these lines.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
