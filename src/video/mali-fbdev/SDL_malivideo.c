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

#if SDL_VIDEO_DRIVER_MALI

/* SDL internals */
#include "../../events/SDL_events_c.h"
#include "../SDL_sysvideo.h"
#include "SDL_events.h"
#include "SDL_loadso.h"
#include "SDL_syswm.h"
#include "SDL_hints.h"
#include "SDL_version.h"

#ifdef SDL_INPUT_LINUXEV
#include "../../core/linux/SDL_evdev.h"
#endif

#include "SDL_maliopengles.h"
#include "SDL_malivideo.h"
#include "SDL_maliblitter.h"


//static int
//MALI_Available(void)
//{
//    return 1;
//}

static void
MALI_Destroy(SDL_VideoDevice * device)
{
    if (device->driverdata != NULL) {
        SDL_free(device->driverdata);
        device->driverdata = NULL;
    }
}

static SDL_VideoDevice *
MALI_Create()
{
    SDL_VideoDevice *device;

    /* Initialize SDL_VideoDevice structure */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (device == NULL) {
        SDL_OutOfMemory();
        return NULL;
    }

    device->driverdata = NULL;

    /* Setup amount of available displays and current display */
    device->num_displays = 0;

    /* Set device free function */
    device->free = MALI_Destroy;

    /* Setup all functions which we can handle */
    device->VideoInit = MALI_VideoInit;
    device->VideoQuit = MALI_VideoQuit;
    device->GetDisplayModes = MALI_GetDisplayModes;
    device->SetDisplayMode = MALI_SetDisplayMode;
    device->CreateSDLWindow = MALI_CreateWindow;
    device->SetWindowTitle = MALI_SetWindowTitle;
    device->SetWindowPosition = MALI_SetWindowPosition;
    device->SetWindowSize = MALI_SetWindowSize;
    device->SetWindowFullscreen = MALI_SetWindowFullscreen;
    device->ShowWindow = MALI_ShowWindow;
    device->HideWindow = MALI_HideWindow;
    device->DestroyWindow = MALI_DestroyWindow;
    device->GetWindowWMInfo = MALI_GetWindowWMInfo;

    device->GL_LoadLibrary = MALI_GLES_LoadLibrary;
    device->GL_GetProcAddress = MALI_GLES_GetProcAddress;
    device->GL_UnloadLibrary = MALI_GLES_UnloadLibrary;
    device->GL_CreateContext = MALI_GLES_CreateContext;
    device->GL_MakeCurrent = MALI_GLES_MakeCurrent;
    device->GL_SetSwapInterval = MALI_GLES_SetSwapInterval;
    device->GL_GetSwapInterval = MALI_GLES_GetSwapInterval;
    device->GL_SwapWindow = MALI_GLES_SwapWindow;
    device->GL_DeleteContext = MALI_GLES_DeleteContext;
    device->GL_DefaultProfileConfig = MALI_GLES_DefaultProfileConfig;

    device->PumpEvents = MALI_PumpEvents;

    return device;
}

VideoBootStrap MALI_bootstrap = {
    "mali",
    "Mali EGL Video Driver",
    //    MALI_Available,
    MALI_Create
};

/*****************************************************************************/
/* SDL Video and Display initialization/handling functions                   */
/*****************************************************************************/

int
MALI_VideoInit(_THIS)
{
    const char *blitter_status = NULL, *rotation = NULL;
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;
    SDL_DisplayData *data;
    struct fb_var_screeninfo vinfo;
    int fd;

    data = (SDL_DisplayData *) SDL_calloc(1, sizeof(SDL_DisplayData));
    if (data == NULL) {
        return SDL_OutOfMemory();
    }

    fd = open("/dev/fb0", O_RDWR, 0);
    if (fd < 0) {
        return SDL_SetError("mali-fbdev: Could not open framebuffer device");
    }

    data->ion_fd = open("/dev/ion", O_RDWR, 0);
    if (data->ion_fd < 0) {
        return SDL_SetError("mali-fbdev: Could not open ion device");
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &vinfo) < 0) {
        MALI_VideoQuit(_this);
        return SDL_SetError("mali-fbdev: Could not get framebuffer information");
    }
    /* Enable triple buffering */
    /*
    vinfo.yres_virtual = vinfo.yres * 3;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, vinfo) == -1) {
        printf("mali-fbdev: Error setting VSCREENINFO\n");
    }
    */
    close(fd);
    //    system("setterm -cursor off");

    data->native_display.width = vinfo.xres;
    data->native_display.height = vinfo.yres;

    /* If the device seems to be portrait mode, set default as rotated. */
    data->rotation = (vinfo.xres < vinfo.yres) ? 1 : 0;

    rotation = SDL_GetHint("SDL_ROTATION");
    blitter_status = SDL_GetHint("SDL_BLITTER_DISABLED");
    if (rotation != NULL)
        data->rotation = SDL_atoi(rotation);

    if (!blitter_status || blitter_status[0] != '1') {
        data->blitter = SDL_calloc(1, sizeof(MALI_Blitter));
        data->blitter->_this = _this;
        MALI_BlitterInit(_this, data->blitter);
    } else {
        data->blitter = NULL;
        data->rotation = 0; // no rotation when the blitter is off!
    }

    SDL_zero(current_mode);
    /* Flip the reported dimensions when rotated. */
    if ((data->rotation & 1) == 0) {
        current_mode.w = vinfo.xres;
        current_mode.h = vinfo.yres;
    } else {
        current_mode.w = vinfo.yres;
        current_mode.h = vinfo.xres;
    }
    /* FIXME: Is there a way to tell the actual refresh rate? */
    current_mode.refresh_rate = 60;
    /* 32 bpp for default */
    //current_mode.format = SDL_PIXELFORMAT_ABGR8888;
    current_mode.format = SDL_PIXELFORMAT_RGBX8888;

    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = data;

    SDL_AddVideoDisplay(&display, SDL_FALSE);
#ifdef SDL_INPUT_LINUXEV
    if (SDL_EVDEV_Init() < 0) {
        return -1;
    }
#endif

    return 0;
}

void
MALI_VideoQuit(_THIS)
{
    /* Clear the framebuffer and ser cursor on again */
    //    int fd = open("/dev/tty", O_RDWR);
    //    ioctl(fd, VT_ACTIVATE, 5);
    //    ioctl(fd, VT_ACTIVATE, 1);
    //    close(fd);
    //    system("setterm -cursor on");

#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Quit();
#endif

}

void
MALI_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
{
    /* Only one display mode available, the current one */
    SDL_AddDisplayMode(display, &display->current_mode);
}

int
MALI_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
{
    return 0;
}

static EGLSurface *
MALI_EGL_InitPixmapSurfaces(_THIS, SDL_Window *window)
{
    struct ion_fd_data ion_data;
    struct ion_allocation_data allocation_data;
    SDL_DisplayData *displaydata;
    SDL_WindowData *windowdata; 
    int i, io, width, height;

    windowdata = window->driverdata;
    displaydata = SDL_GetDisplayDriverData(0);

    width = window->w;
    height = window->h;

    _this->egl_data->egl_surfacetype = EGL_PIXMAP_BIT;
    if (SDL_EGL_ChooseConfig(_this) != 0) {
        SDL_SetError("mali-fbdev: Unable to find a suitable EGL config");
        return EGL_NO_SURFACE;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Creating Pixmap (%dx%d) buffers", width, height);
    windowdata->back_buffer = 0;
    windowdata->queued_buffer = 1;
    windowdata->front_buffer = 2;

    // Populate pixmap definitions
    displaydata->stride = MALI_ALIGN(width * 4, 64);
    for (i = 0; i < 3; i++) {
        MALI_EGL_Surface *surf = &windowdata->surface[i];
        surf->pixmap = (mali_pixmap){
            .width = width,
            .height = height,
            .planes[0] = (mali_plane){
                .stride = displaydata->stride,
                .size = displaydata->stride * height,
                .offset = 0 },
            .planes[1] = (mali_plane){},
            .planes[2] = (mali_plane){},
            .format = 0,
            .handles = { -1, -1, -1 },
            .drm_fourcc = {
                .dataspace = 0,
                .format = DRM_FORMAT_ARGB8888,
                .modifier = 0
            }
        };

        /* Allocate framebuffer data */
        allocation_data = (struct ion_allocation_data){
            .len = surf->pixmap.planes[0].size,
            .heap_id_mask = (1 << ION_HEAP_TYPE_DMA),
            .flags = 1 << ION_FLAG_CACHED
        };

        io = ioctl(displaydata->ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (io != 0) {
            SDL_SetError("mali-fbdev: Unable to create backing ION buffers");
            return EGL_NO_SURFACE;
        }

        /* Export DMA_BUF handle for the framebuffer */
        ion_data = (struct ion_fd_data){
            .handle = allocation_data.handle
        };

        io = ioctl(displaydata->ion_fd, ION_IOC_SHARE, &ion_data);
        if (io != 0) {
            SDL_SetError("mali-fbdev: Failure exporting ION buffer handle");
            return EGL_NO_SURFACE;
        }

        /* Recall fd and handle for teardown later */
        surf->dmabuf_handle = allocation_data.handle;
        surf->dmabuf_fd = ion_data.fd;
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Created ION buffer %d (fd: %d)\n", surf->dmabuf_handle, surf->dmabuf_fd);

        /* Create Pixmap Surface using DMA_BUF framebuffer fd */
        surf->pixmap.handles[0] = ion_data.fd;

        surf->pixmap_handle = displaydata->egl_create_pixmap_ID_mapping(&surf->pixmap);
        SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "mali-fbdev: Created pixmap handle %p\n", (void *)surf->pixmap_handle);
        if ((int)surf->pixmap_handle < 0) {
            SDL_EGL_SetError("mali-fbdev: Unable to create EGL window surface", "egl_create_pixmap_ID_mapping");
            return EGL_NO_SURFACE;
        }

        surf->egl_surface = _this->egl_data->eglCreatePixmapSurface(
            _this->egl_data->egl_display,
            _this->egl_data->egl_config,
            surf->pixmap_handle, NULL);
        if (surf->egl_surface == EGL_NO_SURFACE) {
            SDL_EGL_SetError("mali-fbdev: Unable to create EGL window surface", "eglCreatePixmapSurface");
            return EGL_NO_SURFACE;
        }
    }

    /* Reconfigure the blitter now. */
    MALI_BlitterReconfigure(_this, window, displaydata->blitter);

    /* Done. */
    return windowdata->surface[windowdata->back_buffer].egl_surface;
}

static void
MALI_EGL_DeinitPixmapSurfaces(_THIS, SDL_Window *window)
{
    SDL_WindowData *data;
    SDL_DisplayData *displaydata;
    EGLSurface current_surface;
    EGLContext current_context;

    data = window->driverdata;
    displaydata = SDL_GetDisplayDriverData(0);
    if (!displaydata->blitter)
        return;
    
    // Tear down the device resources first
    MALI_BlitterRelease(_this, window, displaydata->blitter);

    SDL_LockMutex(displaydata->blitter->mutex);

    // Disable current surface
    current_context = (EGLContext)SDL_GL_GetCurrentContext();
    current_surface = _this->egl_data->eglGetCurrentSurface(EGL_DRAW);

    for (int i = 0; i < SDL_arraysize(data->surface); i++) {
        struct ion_handle_data handle_data;
        if (data->surface[i].dmabuf_fd < 0)
            continue;

        if ((current_surface != EGL_NO_SURFACE) && (data->surface[i].egl_surface == current_surface)) {
            SDL_EGL_MakeCurrent(_this, EGL_NO_SURFACE, current_context);
            current_surface = EGL_NO_SURFACE;
        }

        SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "MALI_DestroyWindow: Destroying surface %d.", i);
        _this->egl_data->eglDestroySurface(_this->egl_data->egl_display, data->surface[i].egl_surface);
        displaydata->egl_destroy_pixmap_ID_mapping(data->surface[i].pixmap_handle);
        close(data->surface[i].dmabuf_fd);

        handle_data = (struct ion_handle_data){
            .handle = data->surface[i].dmabuf_handle
        };

        ioctl(displaydata->ion_fd, ION_IOC_FREE, &handle_data);
        data->surface[i].dmabuf_fd = -1;
    }

    SDL_UnlockMutex(displaydata->blitter->mutex);
}

int
MALI_CreateWindow(_THIS, SDL_Window * window)
{
    SDL_WindowData *windowdata;
    SDL_VideoDisplay *display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData *displaydata;

    displaydata = SDL_GetDisplayDriverData(0);

    /* Allocate window internal data */
    windowdata = (SDL_WindowData *)SDL_calloc(1, sizeof(SDL_WindowData));
    if (windowdata == NULL) {
        return SDL_OutOfMemory();
    }

    /* Setup driver data for this window */
    window->driverdata = windowdata;

    /* Use the entire screen when the blitter isn't enabled or the selected
       resolution doesn't make any sense. */
    if ((displaydata->blitter == NULL) || (window->w < 32 || window->h < 32)) {
        SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED,
                            display->current_mode.w, display->current_mode.h);
    }

    /* OpenGL ES is the law here */
    window->flags |= SDL_WINDOW_OPENGL;
    if (!_this->egl_data) {
        if (SDL_EGL_LoadLibrary(_this, NULL, EGL_DEFAULT_DISPLAY, 0) < 0) {
            /* Try again with OpenGL ES 2.0 */
            _this->gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
            _this->gl_config.major_version = 2;
            _this->gl_config.minor_version = 0;
            if (SDL_EGL_LoadLibrary(_this, NULL, EGL_DEFAULT_DISPLAY, 0) < 0) {
                return SDL_SetError("Can't load EGL/GL library on window creation.");
            }
        }

        _this->gl_config.driver_loaded = 1;
    }

    /* If the blitter is required, we will manually create the EGL Surface resources using the ION allocator
       and some reverse engineered mali internals */
    if (displaydata->blitter) {
        displaydata->egl_create_pixmap_ID_mapping = SDL_EGL_GetProcAddress(_this, "egl_create_pixmap_ID_mapping");
        displaydata->egl_destroy_pixmap_ID_mapping = SDL_EGL_GetProcAddress(_this, "egl_destroy_pixmap_ID_mapping");
        if (!displaydata->egl_create_pixmap_ID_mapping || !displaydata->egl_destroy_pixmap_ID_mapping) {
            MALI_VideoQuit(_this);
            return SDL_SetError("mali-fbdev: Can't find mali pixmap entrypoints");
        }

        windowdata->egl_surface = MALI_EGL_InitPixmapSurfaces(_this, window);    
    } else {
        windowdata->egl_surface = SDL_EGL_CreateSurface(_this, (NativeWindowType) &displaydata->native_display);
    }

    if (windowdata->egl_surface == EGL_NO_SURFACE) {
        MALI_VideoQuit(_this);
        return SDL_SetError("mali-fbdev: Can't create EGL window surface");
    }

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    /* Window has been successfully created */
    return 0;
}

void MALI_DestroyWindow(_THIS, SDL_Window *window)
{
    SDL_WindowData *data;
    data = window->driverdata;
    
    if (data) {
        MALI_EGL_DeinitPixmapSurfaces(_this, window);
        if (data->egl_surface != EGL_NO_SURFACE) {
            SDL_EGL_DestroySurface(_this, data->egl_surface);
            data->egl_surface = EGL_NO_SURFACE;
        }
        SDL_free(data);
    }
    window->driverdata = NULL;
}

void
MALI_SetWindowTitle(_THIS, SDL_Window * window)
{
}

void
MALI_SetWindowPosition(_THIS, SDL_Window * window)
{
}

void
MALI_SetWindowSize(_THIS, SDL_Window * window)
{
    SDL_WindowData *windowdata;
    SDL_VideoDisplay *display;
    SDL_DisplayData *displaydata;

    windowdata = window->driverdata;
    display = SDL_GetDisplayForWindow(window);
    displaydata = display->driverdata;

    /*
     * Switch to a fullscreen resolution whenever:
     * - We are not using the blitter
     * - A fullscreen was requested
     * - The window resolution requested doesn't make any sense
     */
    if ((displaydata->blitter == NULL)
        || (window->w < 32 || window->h < 32)
        || ((window->flags & SDL_WINDOW_FULLSCREEN) == SDL_WINDOW_FULLSCREEN)) {
        SDL_SendWindowEvent(window, SDL_WINDOWEVENT_RESIZED,
                            display->current_mode.w, display->current_mode.h);
    }

    /*
     * If we're using the blitter, we might need to signal for a surface reconfiguration
     * if the dimensions of our surface changed.
     */
    if (displaydata->blitter) {
        if ((displaydata->blitter->plane_width == window->w)
         && (displaydata->blitter->plane_height == window->h))
            return;

        MALI_EGL_DeinitPixmapSurfaces(_this, window);
        windowdata->egl_surface = MALI_EGL_InitPixmapSurfaces(_this, window);
    }
}

void
MALI_SetWindowFullscreen(_THIS, SDL_Window *window, SDL_VideoDisplay *display, SDL_bool fullscreen)
{
    MALI_SetWindowSize(_this, window);
}

void
MALI_ShowWindow(_THIS, SDL_Window * window)
{
}

void
MALI_HideWindow(_THIS, SDL_Window * window)
{
}

int
MALI_GLES_SetSwapInterval(_THIS, int interval)
{
    if (!_this->egl_data)
        return 0;

    _this->egl_data->egl_swapinterval = interval != 0;
    return 0;
}

int
MALI_GLES_GetSwapInterval(_THIS)
{
    if (!_this->egl_data)
        return 0;

    return _this->egl_data->egl_swapinterval;
}

/*****************************************************************************/
/* SDL Window Manager function                                               */
/*****************************************************************************/
SDL_bool
MALI_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
{
    if (info->version.major <= SDL_MAJOR_VERSION) {
        return SDL_TRUE;
    } else {
        SDL_SetError("application not compiled with SDL %d.%d\n",
                     SDL_MAJOR_VERSION, SDL_MINOR_VERSION);
    }

    /* Failed to get window manager information */
    return SDL_FALSE;
}

/*****************************************************************************/
/* SDL event functions                                                       */
/*****************************************************************************/
void MALI_PumpEvents(_THIS)
{
#ifdef SDL_INPUT_LINUXEV
    SDL_EVDEV_Poll();
#endif
}

#endif /* SDL_VIDEO_DRIVER_MALI */

/* vi: set ts=4 sw=4 expandtab: */

