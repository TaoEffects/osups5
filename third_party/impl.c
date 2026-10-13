/* Instanciation des bibliothèques à en-tête unique (compilées en C). */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_NO_STDIO_OVERRIDE
#include "stb/stb_image.h"

#define DR_MP3_IMPLEMENTATION
#include "dr_libs/dr_mp3.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_libs/dr_wav.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb/stb_vorbis.c"
