/*
 * gl_api.h -- the slice of OpenGL 3.3 core the NV2A renderer uses.
 *
 * Declared here rather than taken from a loader library: the renderer has to
 * build against Linux Mesa, Windows drivers and the Switch's Mesa port alike,
 * and a table of ~70 entry points filled from SDL_GL_GetProcAddress is less
 * to carry than a generated loader per platform.
 */
#ifndef NV2A_GL_API_H
#define NV2A_GL_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  define GLAPIENTRY __stdcall
#else
#  define GLAPIENTRY
#endif

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef int           GLint;
typedef int           GLsizei;
typedef unsigned int  GLuint;
typedef float         GLfloat;
typedef double        GLdouble;
typedef char          GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t     GLsizeiptr;
typedef ptrdiff_t     GLintptr;

#define GL_FALSE                          0
#define GL_NO_ERROR                       0
#define GL_MAP_WRITE_BIT                  0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT       0x0004
#define GL_MAP_UNSYNCHRONIZED_BIT         0x0020
#define GL_TRUE                           1
#define GL_NONE                           0
#define GL_ZERO                           0
#define GL_ONE                            1
#define GL_TRIANGLES                      0x0004
#define GL_NEVER                          0x0200
#define GL_ALWAYS                         0x0207
#define GL_FRONT                          0x0404
#define GL_BACK                           0x0405
#define GL_FRONT_AND_BACK                 0x0408
#define GL_CW                             0x0900
#define GL_CCW                            0x0901
#define GL_CULL_FACE                      0x0B44
#define GL_DEPTH_TEST                     0x0B71
#define GL_STENCIL_TEST                   0x0B90
#define GL_BLEND                          0x0BE2
#define GL_SCISSOR_TEST                   0x0C11
#define GL_VIEWPORT                       0x0BA2
#define GL_UNPACK_ALIGNMENT               0x0CF5
#define GL_UNPACK_ROW_LENGTH              0x0CF2
#define GL_PACK_ALIGNMENT                 0x0D05
#define GL_TEXTURE_2D                     0x0DE1
#define GL_UNSIGNED_BYTE                  0x1401
#define GL_UNSIGNED_SHORT                 0x1403
#define GL_UNSIGNED_INT                   0x1405
#define GL_SHORT                          0x1402
#define GL_FLOAT                          0x1406
#define GL_RGBA                           0x1908
#define GL_KEEP                           0x1E00
#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_NEAREST                        0x2600
#define GL_LINEAR                         0x2601
#define GL_LINEAR_MIPMAP_LINEAR           0x2703
#define GL_TEXTURE_MAG_FILTER             0x2800
#define GL_TEXTURE_MIN_FILTER             0x2801
#define GL_TEXTURE_WRAP_S                 0x2802
#define GL_TEXTURE_WRAP_T                 0x2803
#define GL_REPEAT                         0x2901
#define GL_COLOR_BUFFER_BIT               0x00004000
#define GL_DEPTH_BUFFER_BIT               0x00000100
#define GL_STENCIL_BUFFER_BIT             0x00000400
#define GL_FUNC_ADD                       0x8006
#define GL_BGRA                           0x80E1
#define GL_CLAMP_TO_BORDER                0x812D
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_MIRRORED_REPEAT                0x8370
#define GL_TEXTURE0                       0x84C0
#define GL_DEPTH_STENCIL                  0x84F9
#define GL_UNSIGNED_INT_8_8_8_8_REV       0x8367
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_STREAM_DRAW                    0x88E0
#define GL_DEPTH24_STENCIL8               0x88F0
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_VERTEX_SHADER                  0x8B31
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_READ_FRAMEBUFFER               0x8CA8
#define GL_DRAW_FRAMEBUFFER               0x8CA9
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_COLOR_ATTACHMENT0              0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT       0x821A
#define GL_FRAMEBUFFER                    0x8D40
#define GL_RENDERBUFFER                   0x8D41
#define GL_RGBA8                          0x8058
#define GL_INCR                           0x1E02
#define GL_DECR                           0x1E03
#define GL_INVERT                         0x150A
#define GL_REPLACE                        0x1E01

/* X(return type, name, parameter list) */
#define NV2A_GL_FUNCS(X) \
    X(const GLubyte *, glGetString, (GLenum)) \
    X(GLenum, glGetError, (void)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glGetIntegerv, (GLenum, GLint *)) \
    X(void, glClearStencil, (GLint)) \
    X(void, glClear, (GLbitfield)) \
    X(void, glEnable, (GLenum)) \
    X(void, glDisable, (GLenum)) \
    X(void, glBlendFunc, (GLenum, GLenum)) \
    X(void, glBlendEquation, (GLenum)) \
    X(void, glBlendColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glDepthFunc, (GLenum)) \
    X(void, glDepthMask, (GLboolean)) \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, glStencilFunc, (GLenum, GLint, GLuint)) \
    X(void, glStencilOp, (GLenum, GLenum, GLenum)) \
    X(void, glStencilMask, (GLuint)) \
    X(void, glCullFace, (GLenum)) \
    X(void, glFrontFace, (GLenum)) \
    X(void, glPixelStorei, (GLenum, GLint)) \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(void, glFinish, (void)) \
    X(void, glGenTextures, (GLsizei, GLuint *)) \
    X(void, glDeleteTextures, (GLsizei, const GLuint *)) \
    X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void *)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void, glDeleteFramebuffers, (GLsizei, const GLuint *)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glGenRenderbuffers, (GLsizei, GLuint *)) \
    X(void, glDeleteRenderbuffers, (GLsizei, const GLuint *)) \
    X(void, glBindRenderbuffer, (GLenum, GLuint)) \
    X(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glDeleteShader, (GLuint)) \
    X(void, glDeleteProgram, (GLuint)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glUseProgram, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform1iv, (GLint, GLsizei, const GLint *)) \
    X(void, glUniform1f, (GLint, GLfloat)) \
    X(void, glUniform2fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glUniform4fv, (GLint, GLsizei, const GLfloat *)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
X(void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
X(GLboolean, glUnmapBuffer, (GLenum)) \
    X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) \
    X(void, glEnableVertexAttribArray, (GLuint)) \
    X(void, glDisableVertexAttribArray, (GLuint)) \
    X(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *)) \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei))

/* Depth entry points differ: desktop GL takes doubles, OpenGL ES floats. */
#define NV2A_GL_FUNCS_DESKTOP(X) \
    X(void, glClearDepth, (GLdouble)) \
    X(void, glDepthRange, (GLdouble, GLdouble))
#define NV2A_GL_FUNCS_ES(X) \
    X(void, glClearDepthf, (GLfloat)) \
    X(void, glDepthRangef, (GLfloat, GLfloat))

#define NV2A_GL_DECLARE(ret, name, args) \
    typedef ret (GLAPIENTRY *PFN_##name) args; extern PFN_##name p_##name;
NV2A_GL_FUNCS(NV2A_GL_DECLARE)
NV2A_GL_FUNCS_DESKTOP(NV2A_GL_DECLARE)
NV2A_GL_FUNCS_ES(NV2A_GL_DECLARE)
#undef NV2A_GL_DECLARE

/* Nonzero when the context is OpenGL ES 3.x (Android): set by the renderer
 * before nv2a_gl_load, which then resolves the ES variants. */
extern int nv2a_gl_is_es;

/* Format of BGRA texels. Desktop GL has GL_BGRA; ES 3 has no BGRA upload, so
 * the bytes go in as RGBA and the texture is swizzled (R<->B) when sampled. */
#define NV2A_FMT_BGRA            (nv2a_gl_is_es ? GL_RGBA : GL_BGRA)
#define GL_TEXTURE_SWIZZLE_R              0x8E42
#define GL_TEXTURE_SWIZZLE_B              0x8E44
#define GL_RED                            0x1903
#define GL_BLUE                           0x1905

/* Resolve every entry point. Returns the number that could not be found
 * (0 on success); `getproc` is SDL_GL_GetProcAddress or equivalent. */
int nv2a_gl_load(void *(*getproc)(const char *name));

/* The renderer calls through these names. */
#define glGetString              p_glGetString
#define glGetError               p_glGetError
#define glViewport               p_glViewport
#define glScissor                p_glScissor
#define glClearColor             p_glClearColor
#define glGetIntegerv            p_glGetIntegerv
static inline void glClearDepth(double d)
{ if (nv2a_gl_is_es) p_glClearDepthf((GLfloat)d); else p_glClearDepth(d); }
#define glClearStencil           p_glClearStencil
#define glClear                  p_glClear
#define glEnable                 p_glEnable
#define glDisable                p_glDisable
#define glBlendFunc              p_glBlendFunc
#define glBlendEquation          p_glBlendEquation
#define glBlendColor             p_glBlendColor
#define glDepthFunc              p_glDepthFunc
#define glDepthMask              p_glDepthMask
static inline void glDepthRange(double n, double f)
{ if (nv2a_gl_is_es) p_glDepthRangef((GLfloat)n, (GLfloat)f); else p_glDepthRange(n, f); }
#define glColorMask              p_glColorMask
#define glStencilFunc            p_glStencilFunc
#define glStencilOp              p_glStencilOp
#define glStencilMask            p_glStencilMask
#define glCullFace               p_glCullFace
#define glFrontFace              p_glFrontFace
#define glPixelStorei            p_glPixelStorei
#define glReadPixels             p_glReadPixels
#define glFinish                 p_glFinish
#define glGenTextures            p_glGenTextures
#define glDeleteTextures         p_glDeleteTextures
#define glBindTexture            p_glBindTexture
#define glActiveTexture          p_glActiveTexture
#define glTexImage2D             p_glTexImage2D
#define glCompressedTexImage2D   p_glCompressedTexImage2D
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#define glTexSubImage2D          p_glTexSubImage2D
#define glTexParameteri          p_glTexParameteri
#define glGenFramebuffers        p_glGenFramebuffers
#define glDeleteFramebuffers     p_glDeleteFramebuffers
#define glBindFramebuffer        p_glBindFramebuffer
#define glFramebufferTexture2D   p_glFramebufferTexture2D
#define glFramebufferRenderbuffer p_glFramebufferRenderbuffer
#define glCheckFramebufferStatus p_glCheckFramebufferStatus
#define glBlitFramebuffer        p_glBlitFramebuffer
#define glGenRenderbuffers       p_glGenRenderbuffers
#define glDeleteRenderbuffers    p_glDeleteRenderbuffers
#define glBindRenderbuffer       p_glBindRenderbuffer
#define glRenderbufferStorage    p_glRenderbufferStorage
#define glCreateShader           p_glCreateShader
#define glDeleteShader           p_glDeleteShader
#define glDeleteProgram          p_glDeleteProgram
#define glShaderSource           p_glShaderSource
#define glCompileShader          p_glCompileShader
#define glGetShaderiv            p_glGetShaderiv
#define glGetShaderInfoLog       p_glGetShaderInfoLog
#define glCreateProgram          p_glCreateProgram
#define glAttachShader           p_glAttachShader
#define glBindAttribLocation     p_glBindAttribLocation
#define glLinkProgram            p_glLinkProgram
#define glGetProgramiv           p_glGetProgramiv
#define glGetProgramInfoLog      p_glGetProgramInfoLog
#define glUseProgram             p_glUseProgram
#define glGetUniformLocation     p_glGetUniformLocation
#define glUniform1i              p_glUniform1i
#define glUniform1iv             p_glUniform1iv
#define glUniform1f              p_glUniform1f
#define glUniform2fv             p_glUniform2fv
#define glUniform4fv             p_glUniform4fv
#define glGenVertexArrays        p_glGenVertexArrays
#define glBindVertexArray        p_glBindVertexArray
#define glGenBuffers             p_glGenBuffers
#define glBindBuffer             p_glBindBuffer
#define glBufferData             p_glBufferData
#define glBufferSubData          p_glBufferSubData
#define glMapBufferRange         p_glMapBufferRange
#define glUnmapBuffer            p_glUnmapBuffer
#define glVertexAttribPointer    p_glVertexAttribPointer
#define glEnableVertexAttribArray p_glEnableVertexAttribArray
#define glDisableVertexAttribArray p_glDisableVertexAttribArray
#define glVertexAttrib4f         p_glVertexAttrib4f
#define glDrawElements           p_glDrawElements
#define glDrawArrays             p_glDrawArrays

#endif /* NV2A_GL_API_H */
