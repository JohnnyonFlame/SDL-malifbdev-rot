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
#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL

#include "SDL_malivideo.h"
#include "SDL_maliopengles.h"
#include "SDL_maliblitter.h"

/* EGL implementation of SDL OpenGL support */
void MALI_GLES_DefaultProfileConfig(_THIS, int *mask, int *major, int *minor)
{
    /* if SDL was _also_ built with the Raspberry Pi driver (so we're
       definitely a Pi device), default to GLES2. */
    *mask = SDL_GL_CONTEXT_PROFILE_ES;
    *major = 2;
    *minor = 0;
}

int
MALI_GLES_LoadLibrary(_THIS, const char *path)
{
   /* Delay loading this until the very end. */
   return 0;
}

int MALI_GLES_SwapWindow(_THIS, SDL_Window * window)
{
   int r;
   unsigned int prev;
   EGLSurface surf;
   SDL_WindowData *windowdata;
   SDL_DisplayData *displaydata = SDL_GetDisplayDriverData(0);
   MALI_Blitter *blitter = displaydata->blitter;

   if (blitter == NULL)
      return SDL_EGL_SwapBuffers(_this, ((SDL_WindowData *)window->driverdata)->egl_surface);

   windowdata = (SDL_WindowData*)_this->windows->driverdata;
   windowdata->glFlush();

   SDL_LockMutex(blitter->mutex);

   // First create the necessary fence
   windowdata->surface[windowdata->back_buffer].egl_fence = _this->egl_data->eglCreateSyncKHR(_this->egl_data->egl_display, EGL_SYNC_FENCE_KHR, NULL);

   // Flip back and front buffers
   prev = windowdata->queued_buffer;
   windowdata->queued_buffer = windowdata->back_buffer;
   windowdata->back_buffer = prev;

   // Do we have anything left over from the previous frame?
   if (windowdata->surface[windowdata->back_buffer].egl_fence != EGL_NO_SYNC) {
      _this->egl_data->eglDestroySyncKHR(
         _this->egl_data->egl_display,
         windowdata->surface[windowdata->back_buffer].egl_fence
      );

      windowdata->surface[windowdata->back_buffer].egl_fence = EGL_NO_SYNC;
   }

   // Done, update back buffer surfaces
   surf = windowdata->surface[windowdata->back_buffer].egl_surface;
   windowdata->egl_surface = surf;
   r = _this->egl_data->eglMakeCurrent(_this->egl_data->egl_display, surf, surf, _this->current_glctx);

   SDL_CondSignal(blitter->cond);
   SDL_UnlockMutex(blitter->mutex);

   return (r == EGL_TRUE) ? 0 : SDL_EGL_SetError("Failed to set current surface.", "eglMakeCurrent");
}

SDL_EGL_MakeCurrent_impl(MALI)
SDL_EGL_CreateContext_impl(MALI)

#endif /* SDL_VIDEO_DRIVER_MALI && SDL_VIDEO_OPENGL_EGL */

/* vi: set ts=4 sw=4 expandtab: */

