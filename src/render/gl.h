// SPDX-License-Identifier: GPL-3.0-or-later
// OpenGL 3.3 core. Sur PS5 les prototypes viennent du SDK ps5-opengl (lien statique) ;
// sur Linux de la libGL de Mesa. Dans les deux cas on appelle les fonctions directement.
#pragma once

#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
#include <GL/gl.h>
#include <GL/glext.h>
