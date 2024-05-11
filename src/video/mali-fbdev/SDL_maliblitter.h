#include "../../SDL_internal.h"

#ifndef _SDL_maliblitter_h
#define _SDL_maliblitter_h

#if SDL_VIDEO_OPENGL_EGL

#include "../SDL_sysvideo.h"
#include "../SDL_egl_c.h"

#include "SDL_egl.h"
#include "SDL_opengl.h"

typedef struct MALI_Blitter {
    /* OpenGL Surface and Context */
    _THIS;
    void *gles2_obj, *egl_obj;
    EGLSurface *egl_surface;
    EGLDisplay *egl_display;
    SDL_GLContext *gl_context;
    SDL_Window *window;
    EGLConfig config;
    GLuint frag, vert, prog, vbo, vao;
    GLint loc_aVertCoord, loc_aTexCoord, loc_uFBOtex, loc_uProj, loc_uTexSize, loc_uScale;
    GLsizei viewport_width, viewport_height;
    GLint plane_width, plane_height, plane_pitch;
    float mat_projection[4][4];
    float vert_buffer_data[4][4];

    // Triple buffering thread
    SDL_mutex *mutex;
    SDL_cond *cond;
    SDL_Thread *thread;
    int thread_stop;
    int rotation;
    int next;
    int scaler;
    int was_initialized;

    void *user_data;

    #define SDL_PROC(ret,func,params) ret (APIENTRY *func) params;
    #include "SDL_maliblitter_egl_funcs.h"
    #include "SDL_maliblitter_gles_funcs.h"
    #undef SDL_PROC
} MALI_Blitter;

extern int MALI_InitBlitterContext(_THIS, MALI_Blitter *blitter, SDL_WindowData *windata, NativeWindowType nw, int rotation);
extern int MALI_BlitterThread(void *data);
void MALI_BlitterInit(_THIS, MALI_Blitter *blitter);
extern void MALI_BlitterReconfigure(_THIS, SDL_Window *window, MALI_Blitter *blitter);
extern void MALI_BlitterRelease(_THIS, SDL_Window *window, MALI_Blitter *blitter);
extern void MALI_BlitterQuit(MALI_Blitter *blitter);

#endif /* SDL_VIDEO_OPENGL_EGL */

#endif /* _SDL_maliblitter_h */
