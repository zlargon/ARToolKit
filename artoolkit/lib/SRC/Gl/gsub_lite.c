/*
 *	gsub_lite.c
 *
 *	Graphics Subroutines (Lite) for ARToolKit.
 *
 *	Copyright (c) 2003-2005 Philip Lamb (PRL) phil@eden.net.nz. All rights reserved.
 *	
 *	Rev		Date		Who		Changes
 *  2.7.0   2003-08-13  PRL     Complete rewrite to ARToolKit-2.65 gsub.c API.
 *  2.7.1   2004-03-03  PRL		Avoid defining BOOL if already defined
 *	2.7.1	2004-03-03	PRL		Don't enable lighting if it was not enabled.
 *	2.7.2	2004-04-27	PRL		Added headerdoc markup. See http://developer.apple.com/darwin/projects/headerdoc/
 *	2.7.3	2004-07-02	PRL		Much more object-orientated through use of ARGL_CONTEXT_SETTINGS type.
 *	2.7.4	2004-07-14	PRL		Added gluCheckExtension hack for GLU versions pre-1.3.
 *	2.7.5	2004-07-15	PRL		Added arglDispImageStateful(); removed extraneous glPixelStorei(GL_UNPACK_IMAGE_HEIGHT,...) calls.
 *	2.7.6	2005-02-18	PRL		Go back to using int rather than BOOL, to avoid conflict with Objective-C.
 *	2.7.7	2005-07-26	PRL		Added cleanup routines for texture stuff.
 *	2.7.8	2005-07-29	PRL		Added distortion compensation enabling/disabling.
 *	2.7.9	2005-08-15	PRL		Added complete support for runtime selection of pixel format and rectangle/power-of-2 textures.
 *
 */
/*
 * 
 * This file is part of ARToolKit.
 * 
 * ARToolKit is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * ARToolKit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with ARToolKit; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 */

// ============================================================================
//	Private includes.
// ============================================================================
#include <AR/gsub_lite.h>

#include <stdio.h>		// fprintf(), stderr
#include <string.h>		// strchr(), strstr(), strlen()
#ifndef __APPLE__
#  include <GL/glu.h>
#  ifdef GL_VERSION_1_2
#    include <GL/glext.h>
#  endif
#else
#  include <OpenGL/glu.h>
#  include <OpenGL/glext.h>
#endif

// ============================================================================
//	Private types and defines.
// ============================================================================
#ifdef _MSC_VER
#  pragma warning (disable:4068)	// Disable MSVC warnings about unknown pragmas.
#endif

// Make sure that required OpenGL constant definitions are available at compile-time.
// N.B. These should not be used unless the renderer indicates (at run-time) that it supports them.

// Define constants for extensions which became core in OpenGL 1.2
#ifndef GL_VERSION_1_2
#  if GL_EXT_bgra
#    define GL_BGR							GL_BGR_EXT
#    define GL_BGRA							GL_BGRA_EXT
#  else
#    define GL_BGR							0x80E0
#    define GL_BGRA							0x80E1
#  endif
#  ifndef GL_APPLE_packed_pixels
#    define GL_UNSIGNED_INT_8_8_8_8_REV		0x8367
#  endif
#  if GL_SGIS_texture_edge_clamp
#    define GL_CLAMP_TO_EDGE				GL_CLAMP_TO_EDGE_SGIS
#  else
#    define GL_CLAMP_TO_EDGE				0x812F
#  endif
#endif

// Define constants for extensions (not yet core).
#ifndef GL_APPLE_ycbcr_422
#  define GL_YCBCR_422_APPLE				0x85B9
#  define GL_UNSIGNED_SHORT_8_8_APPLE		0x85BA
#  define GL_UNSIGNED_SHORT_8_8_REV_APPLE	0x85BB
#endif
#ifndef GL_EXT_abgr
#  define GL_ABGR_EXT						0x8000
#endif
#if GL_NV_texture_rectangle
#  define GL_TEXTURE_RECTANGLE				GL_TEXTURE_RECTANGLE_NV
#  define GL_PROXY_TEXTURE_RECTANGLE		GL_PROXY_TEXTURE_RECTANGLE_NV
#  define GL_MAX_RECTANGLE_TEXTURE_SIZE		GL_MAX_RECTANGLE_TEXTURE_SIZE_NV
#elif GL_EXT_texture_rectangle
#  define GL_TEXTURE_RECTANGLE				GL_TEXTURE_RECTANGLE_EXT
#  define GL_PROXY_TEXTURE_RECTANGLE		GL_PROXY_TEXTURE_RECTANGLE_EXT
#  define GL_MAX_RECTANGLE_TEXTURE_SIZE		GL_MAX_RECTANGLE_TEXTURE_SIZE_EXT
#else
#  define GL_TEXTURE_RECTANGLE				0x84F5
#  define GL_PROXY_TEXTURE_RECTANGLE		0x84F7
#  define GL_MAX_RECTANGLE_TEXTURE_SIZE		0x84F8
#endif

//#define ARGL_DEBUG

struct _ARGL_CONTEXT_SETTINGS {
	int		texturePow2CapabilitiesChecked;
	GLuint	texturePow2;
	GLuint	listPow2;
	int		initedPow2;
	int		textureRectangleCapabilitiesChecked;
	GLuint	textureRectangle;
	GLuint	listRectangle;
	int		initedRectangle;
	int		initPlease;		// Set to TRUE to request re-init of texture etc.
	int		asInited_texmapScaleFactor;
	float	asInited_zoom;
	int		asInited_xsize;
	int		asInited_ysize;
	GLsizei	texturePow2SizeX;
	GLsizei	texturePow2SizeY;
	GLenum	texturePow2WrapMode;
	int		disableDistortionCompensation;
	GLenum	pixIntFormat;
	GLenum	pixFormat;
	GLenum	pixType;
	GLenum	pixSize;
};
typedef struct _ARGL_CONTEXT_SETTINGS ARGL_CONTEXT_SETTINGS;

// ============================================================================
//	Public globals.
// ============================================================================

// It'd be nice if we could wrap these in accessor functions!
int	arglDrawMode   = DEFAULT_DRAW_MODE;
int	arglTexmapMode = DEFAULT_DRAW_TEXTURE_IMAGE;
#ifdef AR_OPENGL_TEXTURE_RECTANGLE
int arglTexRectangle = TRUE;
#else
int arglTexRectangle = FALSE;
#endif // AR_OPENGL_TEXTURE_RECTANGLE

// These items relate to Apple's fast texture transfer support.
//#define ARGL_USE_TEXTURE_RANGE	// Commented out due to conflicts with GL_APPLE_ycbcr_422 extension.
#if defined(__APPLE__) && defined(APPLE_TEXTURE_FAST_TRANSFER)
int arglAppleClientStorage = TRUE; // TRUE | FALSE .
#  ifdef ARGL_USE_TEXTURE_RANGE
int arglAppleTextureRange = TRUE; // TRUE | FALSE .
GLuint arglAppleTextureRangeStorageHint = GL_STORAGE_SHARED_APPLE; // GL_STORAGE_PRIVATE_APPLE | GL_STORAGE_SHARED_APPLE | GL_STORAGE_CACHED_APPLE .
#  else
int arglAppleTextureRange = FALSE; // TRUE | FALSE .
GLuint arglAppleTextureRangeStorageHint = GL_STORAGE_PRIVATE_APPLE; // GL_STORAGE_PRIVATE_APPLE | GL_STORAGE_SHARED_APPLE | GL_STORAGE_CACHED_APPLE .
#  endif // ARGL_USE_TEXTURE_RANGE
#endif // __APPLE__ && APPLE_TEXTURE_FAST_TRANSFER

// ============================================================================
//	Private globals.
// ============================================================================


#pragma mark -
// ============================================================================
//	Private functions.
// ============================================================================

//
// Convert a camera parameter structure into an OpenGL projection matrix.
//
static void arglConvGLcpara(ARParam *param, double focalmin, double focalmax, double m[16])
{
    double   icpara[3][4];
    double   trans[3][4];
    double   p[3][3], q[4][4];
    int      i, j;

    if(arParamDecompMat(param->mat, icpara, trans) < 0) {
        fprintf(stderr, "arglConvGLcpara(): arParamDecompMat() indicated parameter error.\n");
        return;
    }

    for(i = 0; i < 3; i++) {
        for(j = 0; j < 3; j++) {
            p[i][j] = icpara[i][j] / icpara[2][2];
        }
    }
    q[0][0] = (2.0 * p[0][0] / param->xsize);
    q[0][1] = (2.0 * p[0][1] / param->xsize);
    q[0][2] = ((2.0 * p[0][2] / param->xsize)  - 1.0);
    q[0][3] = 0.0;

    q[1][0] = 0.0;
    q[1][1] = (2.0 * p[1][1] / param->ysize);
    q[1][2] = ((2.0 * p[1][2] / param->ysize) - 1.0);
    q[1][3] = 0.0;

    q[2][0] = 0.0;
    q[2][1] = 0.0;
    q[2][2] = (focalmax + focalmin)/(focalmax - focalmin);
    q[2][3] = -2.0 * focalmax * focalmin / (focalmax - focalmin);

    q[3][0] = 0.0;
    q[3][1] = 0.0;
    q[3][2] = 1.0;
    q[3][3] = 0.0;

    for(i = 0; i < 4; i++) {
        for(j = 0; j < 3; j++) {
            m[i+j*4] = q[i][0] * trans[0][j]
			+ q[i][1] * trans[1][j]
			+ q[i][2] * trans[2][j];
        }
        m[i+3*4] = q[i][0] * trans[0][3]
		+ q[i][1] * trans[1][3]
		+ q[i][2] * trans[2][3]
		+ q[i][3];
    }
}

//
//  Provide a gluCheckExtension() function, since some platforms don't have GLU version 1.3 or later.
//
GLboolean arglGluCheckExtension(const GLubyte* extName, const GLubyte *extString)
{
	const GLubyte *start;
	GLubyte *where, *terminator;
	
	// Extension names should not have spaces.
	where = (GLubyte *)strchr((const char *)extName, ' ');
	if (where || *extName == '\0')
		return GL_FALSE;
	// It takes a bit of care to be fool-proof about parsing the
	//	OpenGL extensions string. Don't be fooled by sub-strings, etc.
	start = extString;
	for (;;) {
		where = (GLubyte *) strstr((const char *)start, (const char *)extName);
		if (!where)
			break;
		terminator = where + strlen((const char *)extName);
		if (where == start || *(where - 1) == ' ')
			if (*terminator == ' ' || *terminator == '\0')
				return GL_TRUE;
		start = terminator;
	}
	return GL_FALSE;
}

//
//  Checks for the presence of an OpenGL capability by version or extension.
//  Reports whether the current OpenGL driver's OpenGL implementation version
//  meets or exceeds a minimum value passed in in minVersion (represented as a binary-coded
//  decimal i.e. version 1.0 is represented as 0x0100). If minVersion is zero, the
//  version test will always fail. Alternately, the test is satisfied if an OpenGL extension
//  identifier passed in as a character string
//  is non-NULL, and is found in the current driver's list of supported extensions.
//  Returns: TRUE If either of the tests passes, or FALSE if both fail.
//
static int arglGLCapabilityCheck(const unsigned short minVersion, const unsigned char *extension)
{
	const GLubyte * strRenderer;
	const GLubyte * strVersion;
	const GLubyte * strVendor;
	const GLubyte * strExtensions;
	short j, shiftVal;
	unsigned short version = 0; // binary-coded decimal gl version (ie. 1.4 is 0x0140).
	
	strRenderer = glGetString(GL_RENDERER);
	strVendor = glGetString(GL_VENDOR);
	strVersion = glGetString(GL_VERSION);
	j = 0;
	shiftVal = 8;
	// Construct BCD version.
	while (((strVersion[j] <= '9') && (strVersion[j] >= '0')) || (strVersion[j] == '.')) { // Get only basic version info (until first non-digit or non-.)
		if ((strVersion[j] <= '9') && (strVersion[j] >= '0')) {
			version += (strVersion[j] - '0') << shiftVal;
			shiftVal -= 4;
		}
		j++;
	}
	strExtensions = glGetString(GL_EXTENSIONS);
	
	if (0 < minVersion && version >= minVersion) return (TRUE);
	if (extension && arglGluCheckExtension(extension, strExtensions)) return (TRUE);
	return (FALSE);
}

static int arglDispImageTexRectangleCapabilitiesCheck(const ARParam *cparam, ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	GLint textureRectangleSizeMax;
	GLint format;

    if (!arglGLCapabilityCheck(0, (unsigned char *)"GL_NV_texture_rectangle")) {
		if (!arglGLCapabilityCheck(0, (unsigned char *)"GL_EXT_texture_rectangle")) { // Alternate name.
			return (FALSE);
		}
	}
    glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE, &textureRectangleSizeMax);
	if (cparam->xsize > textureRectangleSizeMax || cparam->ysize > textureRectangleSizeMax) {
		return (FALSE);
	}
	
	// Now check that the renderer can accomodate a texture of this size.
	glTexImage2D(GL_PROXY_TEXTURE_RECTANGLE, 0, contextSettings->pixIntFormat, cparam->xsize, cparam->ysize, 0, contextSettings->pixFormat, contextSettings->pixType, NULL);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_RECTANGLE, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	if (!format) {
		return (FALSE);
	}
	
	contextSettings->textureRectangleCapabilitiesChecked = TRUE;	

	return (TRUE);
}

static int arglCleanupTexRectangle(ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	if (!contextSettings->initedRectangle) return (FALSE);
	
	glDeleteTextures(1, &(contextSettings->textureRectangle));
	glDeleteLists(contextSettings->listRectangle, 1);
	contextSettings->textureRectangleCapabilitiesChecked = FALSE;
	contextSettings->initedRectangle = FALSE;
	return (TRUE);
}

//
// Blit an image to the screen using OpenGL rectangle texturing.
//
static void arglDispImageTexRectangle(ARUint8 *image, const ARParam *cparam, const float zoom, ARGL_CONTEXT_SETTINGS_REF contextSettings, const int texmapScaleFactor)
{
	float	px, py, py_prev;
    double	x1, x2, y1, y2;
    float	xx1, xx2, yy1, yy2;
	int		i, j;
	
    if(!contextSettings->initedRectangle || contextSettings->initPlease) {
		
		contextSettings->initPlease = FALSE;
		// Delete previous texture and list, unless this is our first time here.
		if (contextSettings->initedRectangle) arglCleanupTexRectangle(contextSettings);
		
		// If we have not done so, check texturing capabilities. If they have already been
		// checked, and we got to here, then obviously the capabilities were insufficient,
		// so just return without doing anything.
		if (!contextSettings->textureRectangleCapabilitiesChecked) {
			contextSettings->textureRectangleCapabilitiesChecked = TRUE;
			if (!arglDispImageTexRectangleCapabilitiesCheck(cparam, contextSettings)) {
				fprintf(stderr, "argl error: Your OpenGL implementation and/or hardware's texturing capabilities are insufficient to support rectangle textures.\n");
				return;
			}
		} else {
			return;
		}
		
		// Set up the rectangle texture object.
		glGenTextures(1, &(contextSettings->textureRectangle));
		glBindTexture(GL_TEXTURE_RECTANGLE, contextSettings->textureRectangle);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

#ifdef APPLE_TEXTURE_FAST_TRANSFER
#  ifdef ARGL_USE_TEXTURE_RANGE
		if (arglAppleTextureRange) {
			glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE, cparam->xsize * cparam->ysize * contextSettings->pixSize, image);
			glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_STORAGE_HINT_APPLE, arglAppleTextureRangeStorageHint);
		} else {
			glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE, 0, NULL);
			glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_PRIVATE_APPLE);
		}
#endif // ARGL_USE_TEXTURE_RANGE
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, arglAppleClientStorage);
#endif // APPLE_TEXTURE_FAST_TRANSFER
		
		// Specify the texture to OpenGL.
		if (texmapScaleFactor == 2) {
			// If texmapScaleFactor is 2, pretend lines in the source image are
			// twice as long as they are; glTexImage2D will read only the first
			// half of each line, effectively discarding every second line in the source image.
			glPixelStorei(GL_UNPACK_ROW_LENGTH, cparam->xsize*texmapScaleFactor);
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Our image data is tightly packed.
		glTexImage2D(GL_TEXTURE_RECTANGLE, 0, contextSettings->pixIntFormat, cparam->xsize, cparam->ysize/texmapScaleFactor, 0, contextSettings->pixFormat, contextSettings->pixType, image);
		if (texmapScaleFactor == 2) {
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		}
		
		// Set up the surface which we will texture upon.
		contextSettings->listRectangle = glGenLists(1);
		glNewList(contextSettings->listRectangle, GL_COMPILE);
		glEnable(GL_TEXTURE_RECTANGLE);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		
		if (contextSettings->disableDistortionCompensation) {
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, (float)(cparam->ysize/texmapScaleFactor)); glVertex2f(0.0f, 0.0f);
			glTexCoord2f((float)(cparam->xsize), (float)(cparam->ysize/texmapScaleFactor)); glVertex2f(cparam->xsize * zoom, 0.0f);
			glTexCoord2f((float)(cparam->xsize), 0.0f); glVertex2f(cparam->xsize * zoom, cparam->ysize * zoom);
			glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, cparam->ysize * zoom);
			glEnd();
		} else {
			py_prev = 0.0f;
			for(j = 1; j <= 20; j++) {	// Do 20 rows.
				py = py_prev;
				py_prev = cparam->ysize * j / 20.0f;
				
				glBegin(GL_QUAD_STRIP);
				for(i = 0; i <= 20; i++) {	// Draw 21 pairs of vertices per row to make 20 columns.
					px = cparam->xsize * i / 20.0f;
					
					arParamObserv2Ideal(cparam->dist_factor, (double)px, (double)py, &x1, &y1);
					arParamObserv2Ideal(cparam->dist_factor, (double)px, (double)py_prev, &x2, &y2);
					
					xx1 = (float)x1 * zoom;
					yy1 = (cparam->ysize - (float)y1) * zoom;
					xx2 = (float)x2 * zoom;
					yy2 = (cparam->ysize - (float)y2) * zoom;
					
					glTexCoord2f(px, py/texmapScaleFactor); glVertex2f(xx1, yy1);
					glTexCoord2f(px, py_prev/texmapScaleFactor); glVertex2f(xx2, yy2);
				}
				glEnd();
			}			
		}
		glDisable(GL_TEXTURE_RECTANGLE);
		glEndList();

		contextSettings->asInited_ysize = cparam->ysize;
		contextSettings->asInited_xsize = cparam->xsize;
		contextSettings->asInited_zoom = zoom;
        contextSettings->asInited_texmapScaleFactor = texmapScaleFactor;
        contextSettings->initedRectangle = TRUE;
    }
	
    glBindTexture(GL_TEXTURE_RECTANGLE, contextSettings->textureRectangle);
#ifdef APPLE_TEXTURE_FAST_TRANSFER
#  ifdef ARGL_USE_TEXTURE_RANGE
	if (arglAppleTextureRange) {
		glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE, cparam->xsize * cparam->ysize * contextSettings->pixSize, image);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_STORAGE_HINT_APPLE, arglAppleTextureRangeStorageHint);
	} else {
		glTextureRangeAPPLE(GL_TEXTURE_RECTANGLE, 0, NULL);
		glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_PRIVATE_APPLE);
	}
#endif // ARGL_USE_TEXTURE_RANGE
	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, arglAppleClientStorage);
#endif // APPLE_TEXTURE_FAST_TRANSFER
	if (texmapScaleFactor == 2) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, cparam->xsize*texmapScaleFactor);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_RECTANGLE, 0, 0, 0, cparam->xsize, cparam->ysize/texmapScaleFactor, contextSettings->pixFormat, contextSettings->pixType, image);
	glCallList(contextSettings->listRectangle);
	if (texmapScaleFactor == 2) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
    glBindTexture(GL_TEXTURE_RECTANGLE, 0);	
}

static int arglDispImageTexPow2CapabilitiesCheck(const ARParam *cparam, ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	GLint format;
	GLint texture1SizeMax;

	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture1SizeMax);
	if (cparam->xsize > texture1SizeMax || cparam->ysize > texture1SizeMax) {
		return (FALSE);
	}
	
	// Work out how big textures needs to be.
	contextSettings->texturePow2SizeX = contextSettings->texturePow2SizeY = 1;
	while (contextSettings->texturePow2SizeX < cparam->xsize) {
		contextSettings->texturePow2SizeX *= 2;
		if (contextSettings->texturePow2SizeX > texture1SizeMax) {
			return (FALSE); // Too big to handle.
		}
	}
	while (contextSettings->texturePow2SizeY < cparam->ysize) {
		contextSettings->texturePow2SizeY *= 2;
		if (contextSettings->texturePow2SizeY > texture1SizeMax) {
			return (FALSE); // Too big to handle.
		}
	}
	
	// Now check that the renderer can accomodate a texture of this size.
#ifdef APPLE_TEXTURE_FAST_TRANSFER
	// Can't use client storage or texture range.
#  ifdef ARGL_USE_TEXTURE_RANGE
	glTextureRangeAPPLE(GL_TEXTURE_2D, 0, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_PRIVATE_APPLE);
#  endif // ARGL_USE_TEXTURE_RANGE
	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, FALSE);
#endif // APPLE_TEXTURE_FAST_TRANSFER
	glTexImage2D(GL_PROXY_TEXTURE_2D, 0, contextSettings->pixIntFormat, contextSettings->texturePow2SizeX, contextSettings->texturePow2SizeY, 0, contextSettings->pixFormat, contextSettings->pixType, NULL);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	if (!format) {
		return (FALSE);
	}
	
	// Decide whether we can use GL_CLAMP_TO_EDGE.
	if (arglGLCapabilityCheck(0x0120, (unsigned char *)"GL_SGIS_texture_edge_clamp")) {
		contextSettings->texturePow2WrapMode = GL_CLAMP_TO_EDGE;
	} else {
		contextSettings->texturePow2WrapMode = GL_REPEAT;
	}
	
	contextSettings->texturePow2CapabilitiesChecked = TRUE;
	
	return (TRUE);
}

static int arglCleanupTexPow2(ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	if (!contextSettings->initedPow2) return (FALSE);
	
	glDeleteTextures(1, &(contextSettings->texturePow2));
	glDeleteLists(contextSettings->listPow2, 1);
	contextSettings->texturePow2CapabilitiesChecked = FALSE;
	contextSettings->initedPow2 = FALSE;
	return (TRUE);
}

//
// Blit an image to the screen using OpenGL power-of-two texturing.
//
static void arglDispImageTexPow2(ARUint8 *image, const ARParam *cparam, const float zoom, ARGL_CONTEXT_SETTINGS_REF contextSettings, const int texmapScaleFactor)
{
    float	tsx, tsy, tex, tey;
    float	px, py, qx, qy;
    double	x1, x2, x3, x4, y1, y2, y3, y4;
    float	xx1, xx2, xx3, xx4, yy1, yy2, yy3, yy4;
    int		i, j;

    if(!contextSettings->initedPow2 || contextSettings->initPlease) {

		contextSettings->initPlease = FALSE;
		// Delete previous texture and list, unless this is our first time here.
		if (contextSettings->initedPow2) arglCleanupTexPow2(contextSettings);

		// If we have not done so, check texturing capabilities. If they have already been
		// checked, and we got to here, then obviously the capabilities were insufficient,
		// so just return without doing anything.
		if (!contextSettings->texturePow2CapabilitiesChecked) {
			contextSettings->texturePow2CapabilitiesChecked = TRUE;
			if (!arglDispImageTexPow2CapabilitiesCheck(cparam, contextSettings)) {
				fprintf(stderr, "argl error: Your OpenGL implementation and/or hardware's texturing capabilities are insufficient.\n");
				return;
			}
		} else {
			return;
		}

		// Set up the texture object.
		glGenTextures(1, &(contextSettings->texturePow2));
		glBindTexture(GL_TEXTURE_2D, contextSettings->texturePow2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, contextSettings->texturePow2WrapMode);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, contextSettings->texturePow2WrapMode);

#ifdef APPLE_TEXTURE_FAST_TRANSFER
#  ifdef ARGL_USE_TEXTURE_RANGE
		// Can't use client storage or texture range.
		glTextureRangeAPPLE(GL_TEXTURE_2D, 0, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_PRIVATE_APPLE);
#  endif // ARGL_USE_TEXTURE_RANGE
		glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, FALSE);
#endif // APPLE_TEXTURE_FAST_TRANSFER

		// Request OpenGL allocate memory for a power-of-two texture of the appropriate size.
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, contextSettings->pixIntFormat, contextSettings->texturePow2SizeX, contextSettings->texturePow2SizeY, 0, contextSettings->pixFormat, contextSettings->pixType, NULL);
				
		// Set up the surface which we will texture upon.
		contextSettings->listPow2 = glGenLists(1);
		glNewList(contextSettings->listPow2, GL_COMPILE); // NB Texture not specified yet so don't execute.
		glEnable(GL_TEXTURE_2D);
		glMatrixMode(GL_TEXTURE);
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);

		if (contextSettings->disableDistortionCompensation) {
			glBegin(GL_QUADS);
			glTexCoord2f(0.0f, (float)cparam->ysize/(float)contextSettings->texturePow2SizeY);
			glVertex2f(0.0f, 0.0f);
			glTexCoord2f((float)cparam->xsize/(float)contextSettings->texturePow2SizeX, (float)cparam->ysize/(float)contextSettings->texturePow2SizeY);
			glVertex2f((float)cparam->xsize * zoom, 0.0f);
			glTexCoord2f((float)cparam->xsize/(float)contextSettings->texturePow2SizeX, 0.0f);
			glVertex2f((float)cparam->xsize * zoom, (float)cparam->ysize * zoom);
			glTexCoord2f(0.0f, 0.0f);
			glVertex2f(0.0f, (float)cparam->ysize * zoom);
			glEnd();
		} else {
			qy = 0.0f;
			tey = 0.0f;
			for(j = 1; j <= 20; j++) {	// Do 20 rows.
				py = qy;
				tsy = tey;
				qy = cparam->ysize * j / 20.0f;
				tey = qy / contextSettings->texturePow2SizeY;
				
				qx = 0.0f;
				tex = 0.0f;
				for(i = 1; i <= 20; i++) {	// Draw 20 columns.
					px = qx;
					tsx = tex;
					qx = cparam->xsize * i / 20.0f;
					tex = qx / contextSettings->texturePow2SizeX;
					
					arParamObserv2Ideal(cparam->dist_factor, (double)px, (double)py, &x1, &y1);
					arParamObserv2Ideal(cparam->dist_factor, (double)qx, (double)py, &x2, &y2);
					arParamObserv2Ideal(cparam->dist_factor, (double)qx, (double)qy, &x3, &y3);
					arParamObserv2Ideal(cparam->dist_factor, (double)px, (double)qy, &x4, &y4);
					
					xx1 = (float)x1 * zoom;
					yy1 = (cparam->ysize - (float)y1) * zoom;
					xx2 = (float)x2 * zoom;
					yy2 = (cparam->ysize - (float)y2) * zoom;
					xx3 = (float)x3 * zoom;
					yy3 = (cparam->ysize - (float)y3) * zoom;
					xx4 = (float)x4 * zoom;
					yy4 = (cparam->ysize - (float)y4) * zoom;
					
					glBegin(GL_QUADS);
					glTexCoord2f(tsx, tsy); glVertex2f(xx1, yy1);
					glTexCoord2f(tex, tsy); glVertex2f(xx2, yy2);
					glTexCoord2f(tex, tey); glVertex2f(xx3, yy3);
					glTexCoord2f(tsx, tey); glVertex2f(xx4, yy4);
					glEnd();
				} // columns.
			} // rows.
		}
		glDisable(GL_TEXTURE_2D);
        glEndList();

		contextSettings->asInited_ysize = cparam->ysize;
		contextSettings->asInited_xsize = cparam->xsize;
		contextSettings->asInited_zoom = zoom;
        contextSettings->asInited_texmapScaleFactor = texmapScaleFactor;
		contextSettings->initedPow2 = TRUE;
	}

    glBindTexture(GL_TEXTURE_2D, contextSettings->texturePow2);
#ifdef APPLE_TEXTURE_FAST_TRANSFER
#  ifdef ARGL_USE_TEXTURE_RANGE
	// Can't use client storage or texture range.
	glTextureRangeAPPLE(GL_TEXTURE_2D, 0, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_STORAGE_HINT_APPLE, GL_STORAGE_PRIVATE_APPLE);
#endif // ARGL_USE_TEXTURE_RANGE
	glPixelStorei(GL_UNPACK_CLIENT_STORAGE_APPLE, FALSE);
#endif // APPLE_TEXTURE_FAST_TRANSFER
	if (texmapScaleFactor == 2) {
		// If texmapScaleFactor is 2, pretend lines in the source image are
		// twice as long as they are; glTexImage2D will read only the first
		// half of each line, effectively discarding every second line in the source image.
		glPixelStorei(GL_UNPACK_ROW_LENGTH, cparam->xsize*texmapScaleFactor);
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cparam->xsize, cparam->ysize/texmapScaleFactor, contextSettings->pixFormat, contextSettings->pixType, image);
	glCallList(contextSettings->listPow2);
	if (texmapScaleFactor == 2) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
    glBindTexture(GL_TEXTURE_2D, 0);
}

#pragma mark -
// ============================================================================
//	Public functions.
// ============================================================================

ARGL_CONTEXT_SETTINGS_REF arglSetupForCurrentContext(void)
{
	ARGL_CONTEXT_SETTINGS_REF contextSettings;
	
	contextSettings = (ARGL_CONTEXT_SETTINGS_REF)calloc(1, sizeof(ARGL_CONTEXT_SETTINGS));
	// Use default pixel format handed to us by <AR/config.h>.
#if defined(AR_PIX_FORMAT_RGBA)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_RGBA);
#elif defined(AR_PIX_FORMAT_ABGR)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_ABGR);
#elif defined(AR_PIX_FORMAT_BGRA)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_BGRA);
#elif defined(AR_PIX_FORMAT_ARGB)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_ARGB);
#elif defined(AR_PIX_FORMAT_RGB)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_RGB);
#elif defined(AR_PIX_FORMAT_BGR)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_BGR);
#elif defined(AR_PIX_FORMAT_2vuy)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_2vuy);
#elif defined(AR_PIX_FORMAT_yuvs)
	arglPixelFormatSet(contextSettings, ARGL_PIX_FORMAT_yuvs);
#else
#  error Unknown pixel format defined in config.h.
#endif
	return (contextSettings);
}

void arglCleanup(ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	arglCleanupTexRectangle(contextSettings);
	arglCleanupTexPow2(contextSettings);
	free(contextSettings);
}

void arglCameraFrustum(const ARParam *cparam, const double focalmin, const double focalmax, GLdouble m_projection[16])
{
    int		i;
	ARParam	cparam_copy;

    cparam_copy = *cparam;
	for (i = 0; i < 4; i++) {
        cparam_copy.mat[1][i] = (cparam_copy.ysize - 1)*(cparam_copy.mat[2][i]) - cparam_copy.mat[1][i];
    }
    arglConvGLcpara(&cparam_copy, focalmin, focalmax, m_projection);
}

void arglCameraView(double para[3][4], GLdouble m_modelview[16], double scale)
{
    int     i, j;

    for(j = 0; j < 3; j++) {
        for(i = 0; i < 4; i++) {
            m_modelview[i*4+j] = para[j][i];
        }
    }
    m_modelview[0*4+3] = m_modelview[1*4+3] = m_modelview[2*4+3] = 0.0;
    m_modelview[3*4+3] = 1.0;
	if (scale != 0.0) {
		m_modelview[12] *= scale;
		m_modelview[13] *= scale;
		m_modelview[14] *= scale;
	}
}

void arglDispImage(ARUint8 *image, const ARParam *cparam, const double zoom, ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	GLint texEnvModeSave;	
	GLboolean lightingSave;
	GLboolean depthTestSave;
#ifdef ARGL_DEBUG
	GLenum			err;
	const GLubyte	*errs;
#endif // ARGL_DEBUG

	if (!image) return;

	// Prepare an orthographic projection, set camera position for 2D drawing, and save GL state.
	glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &texEnvModeSave); // Save GL texture environment mode.
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	lightingSave = glIsEnabled(GL_LIGHTING);			// Save enabled state of lighting.
	if (lightingSave == GL_TRUE) glDisable(GL_LIGHTING);
	depthTestSave = glIsEnabled(GL_DEPTH_TEST);		// Save enabled state of depth test.
	if (depthTestSave == GL_TRUE) glDisable(GL_DEPTH_TEST);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	gluOrtho2D(0, cparam->xsize, 0, cparam->ysize);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();		
	
	arglDispImageStateful(image, cparam, zoom, contextSettings);

	// Restore previous projection, camera position, and GL state.
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	if (depthTestSave == GL_TRUE) glEnable(GL_DEPTH_TEST);			// Restore enabled state of depth test.
	if (lightingSave == GL_TRUE) glEnable(GL_LIGHTING);			// Restore enabled state of lighting.
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, texEnvModeSave); // Restore GL texture environment mode.
	
#ifdef ARGL_DEBUG
	// Report any errors we generated.
	while ((err = glGetError()) != GL_NO_ERROR) {
		errs = gluErrorString(err);	// fetch error code
		fprintf(stderr, "GL error: %s (%i)\n", errs, (int)err);	// write err code and number to stderr
	}
#endif // ARGL_DEBUG
	
}

void arglDispImageStateful(ARUint8 *image, const ARParam *cparam, const double zoom, ARGL_CONTEXT_SETTINGS_REF contextSettings)
{
	float zoomf;
	int texmapScaleFactor;
	
	zoomf = (float)zoom;
	texmapScaleFactor = arglTexmapMode + 1;
	if (arglDrawMode == AR_DRAW_BY_GL_DRAW_PIXELS) {
		glDisable(GL_TEXTURE_2D);
		glPixelZoom(zoomf, -zoomf);
		glRasterPos2f(0.0f, cparam->ysize * zoomf);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glDrawPixels(cparam->xsize, cparam->ysize, contextSettings->pixFormat, contextSettings->pixType, image);
	} else {
		// Check whether any settings in globals/parameters have changed.
		// N.B. We don't check cparam->dist_factor[], but this is unlikely to change!
		if ((texmapScaleFactor != contextSettings->asInited_texmapScaleFactor) ||
			(zoomf != contextSettings->asInited_zoom) ||
			(cparam->xsize != contextSettings->asInited_xsize) ||
			(cparam->ysize != contextSettings->asInited_ysize)) {
			contextSettings->initPlease = TRUE;
		}
		
		if (arglTexRectangle) {
			arglDispImageTexRectangle(image, cparam, zoomf, contextSettings, texmapScaleFactor);
		} else {
			arglDispImageTexPow2(image, cparam, zoomf, contextSettings, texmapScaleFactor);
		}
	}	
}

int arglDistortionCompensationSet(ARGL_CONTEXT_SETTINGS_REF contextSettings, int enable)
{
	if (!contextSettings) return (FALSE);
	contextSettings->disableDistortionCompensation = !enable;
	contextSettings->initPlease = TRUE;
	return (TRUE);
}

int arglDistortionCompensationGet(ARGL_CONTEXT_SETTINGS_REF contextSettings, int *enable)
{
	if (!contextSettings) return (FALSE);
	*enable = !contextSettings->disableDistortionCompensation;
	return (TRUE);
}

int arglPixelFormatSet(ARGL_CONTEXT_SETTINGS_REF contextSettings, ARGL_PIX_FORMAT format)
{
	if (!contextSettings) return (FALSE);
	switch (format) {
		case ARGL_PIX_FORMAT_RGBA:
			contextSettings->pixIntFormat = GL_RGBA;
			contextSettings->pixFormat = GL_RGBA;
			contextSettings->pixType = GL_UNSIGNED_BYTE;
			contextSettings->pixSize = 4;
			break;
		case ARGL_PIX_FORMAT_ABGR:	// SGI.
			contextSettings->pixIntFormat = GL_RGBA;
			contextSettings->pixFormat = GL_ABGR_EXT;
			contextSettings->pixType = GL_UNSIGNED_BYTE;
			contextSettings->pixSize = 4;
			break;
		case ARGL_PIX_FORMAT_BGRA:	// Windows.
			contextSettings->pixIntFormat = GL_RGBA;
			contextSettings->pixFormat = GL_BGRA;
			contextSettings->pixType = GL_UNSIGNED_BYTE;
			contextSettings->pixSize = 4;
			break;
		case ARGL_PIX_FORMAT_ARGB:	// Mac.
			contextSettings->pixIntFormat = GL_RGBA;
			contextSettings->pixFormat = GL_BGRA;
			contextSettings->pixType = GL_UNSIGNED_INT_8_8_8_8_REV;
			contextSettings->pixSize = 4;
			break;
		case ARGL_PIX_FORMAT_RGB:
			contextSettings->pixIntFormat = GL_RGB;
			contextSettings->pixFormat = GL_RGB;
			contextSettings->pixType = GL_UNSIGNED_BYTE;
			contextSettings->pixSize = 3;
			break;
		case ARGL_PIX_FORMAT_BGR:
			contextSettings->pixIntFormat = GL_RGB;
			contextSettings->pixFormat = GL_BGR;
			contextSettings->pixType = GL_UNSIGNED_BYTE;
			contextSettings->pixSize = 3;
			break;
		case ARGL_PIX_FORMAT_2vuy:
			contextSettings->pixIntFormat = GL_RGB;
			contextSettings->pixFormat = GL_YCBCR_422_APPLE;
			contextSettings->pixType = GL_UNSIGNED_SHORT_8_8_REV_APPLE;
			contextSettings->pixSize = 2;
			break;
		case ARGL_PIX_FORMAT_yuvs:
			contextSettings->pixIntFormat = GL_RGB;
			contextSettings->pixFormat = GL_YCBCR_422_APPLE;
			contextSettings->pixType = GL_UNSIGNED_SHORT_8_8_APPLE;
			contextSettings->pixSize = 2;
			break;
		default:
			return (FALSE);
			break;
	}
	contextSettings->initPlease = TRUE;
	return (TRUE);
}

int arglPixelFormatGet(ARGL_CONTEXT_SETTINGS_REF contextSettings, ARGL_PIX_FORMAT *format, int *size)
{
	if (!contextSettings) return (FALSE);
	switch (contextSettings->pixFormat) {
		case GL_RGBA:
			*format = ARGL_PIX_FORMAT_RGBA;
			*size = 4;
			break;
		case GL_ABGR_EXT:
			*format = ARGL_PIX_FORMAT_ABGR;
			*size = 4;
			break;
		case GL_BGRA:
			if (contextSettings->pixType == GL_UNSIGNED_BYTE) *format = ARGL_PIX_FORMAT_BGRA;
			else *format = ARGL_PIX_FORMAT_ARGB;
			*size = 4;
			break;
		case GL_RGB:
			*format = ARGL_PIX_FORMAT_RGB;
			*size = 3;
			break;
		case GL_BGR:
			*format = ARGL_PIX_FORMAT_BGR;
			*size = 3;
			break;
		case GL_YCBCR_422_APPLE:
			if (contextSettings->pixType == GL_UNSIGNED_SHORT_8_8_REV_APPLE) *format = ARGL_PIX_FORMAT_2vuy;
			else *format = ARGL_PIX_FORMAT_yuvs;
			*size = 2;
			break;
		default:
			return (FALSE);
			break;
	}
	return (TRUE);
}
