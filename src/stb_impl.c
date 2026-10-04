/* stb_impl.c - de implementaties van stb_image (PNG lezen: texture packs) en stb_image_write (PNG schrijven:
 * --dumptex, afdrukken buiten Windows); public domain, zie het eind van src/stb/stb_image*.h */
#include "plat.h"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"
