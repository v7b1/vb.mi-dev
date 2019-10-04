// jit.gl.h

#pragma once


#ifdef MAC_VERSION
	#define JIT_GL_NSGL
	#include <OpenGL/gl.h>
	#include <OpenGL/glu.h>
	#include <OpenGL/OpenGL.h>

	#define glGetProcAddress nsglGetProcAddress
	#include "jit.glext.h"
#endif	// MAC_VERSION


#ifdef WIN_VERSION
	#include <windows.h>
	#include "gl\gl.h"
	#include "gl\glu.h"
	#include "jit.wglext.h"
	#include "jit.glext.h"
	#define glGetProcAddress wglGetProcAddress
#endif

