/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2014 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/

#ifndef _SDL_malivideo_h
#define _SDL_malivideo_h

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"

#include "SDL_egl.h"
#include "SDL_opengl.h"
#include "mali.h"

#include <EGL/egl.h>
#include <linux/vt.h>
#include <linux/fb.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

#include "mali.h"
#include "ion.h"

typedef struct SDL_DisplayData
{
    int ion_fd;
    struct MALI_Blitter *blitter;
    fbdev_window_s native_display;
    int rotation;
    unsigned long stride;
    unsigned long w_align;
    unsigned long h_align;
    NativePixmapType (*egl_create_pixmap_ID_mapping)(mali_pixmap *);
    NativePixmapType (*egl_destroy_pixmap_ID_mapping)(int id);
} SDL_DisplayData;

typedef struct MALI_EGL_Surface
{
    // A pixmap is backed by multiple ION allocated backbuffers, EGL fences, etc.
    EGLImageKHR egl_image;
    GLuint texture;
    EGLSyncKHR egl_fence;
    EGLSurface egl_surface;
    NativePixmapType pixmap_handle;
    mali_pixmap pixmap;
    int dmabuf_fd;
    int dmabuf_handle;
} MALI_EGL_Surface;

typedef struct SDL_WindowData
{
    EGLSurface egl_surface;
    int back_buffer;
    int queued_buffer;
    int front_buffer;

    MALI_EGL_Surface surface[3];
} SDL_WindowData;

/****************************************************************************/
/* SDL_VideoDevice functions declaration                                    */
/****************************************************************************/

/* Display and window functions */
int MALI_VideoInit(_THIS);
void MALI_VideoQuit(_THIS);
void MALI_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
int MALI_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode);
int MALI_CreateWindow(_THIS, SDL_Window * window);
void MALI_SetWindowTitle(_THIS, SDL_Window * window);
void MALI_SetWindowPosition(_THIS, SDL_Window * window);
void MALI_SetWindowSize(_THIS, SDL_Window * window);
void MALI_SetWindowFullscreen(_THIS, SDL_Window * window, SDL_VideoDisplay * display, SDL_bool fullscreen);
void MALI_ShowWindow(_THIS, SDL_Window * window);
void MALI_HideWindow(_THIS, SDL_Window * window);
void MALI_DestroyWindow(_THIS, SDL_Window * window);
int MALI_GLES_SetSwapInterval(_THIS, int interval);
int MALI_GLES_GetSwapInterval(_THIS);

/* Window manager function */
SDL_bool MALI_GetWindowWMInfo(_THIS, SDL_Window * window,
                             struct SDL_SysWMinfo *info);

/* Event functions */
void MALI_PumpEvents(_THIS);

#endif /* _SDL_malivideo_h */

/* vi: set ts=4 sw=4 expandtab: */

