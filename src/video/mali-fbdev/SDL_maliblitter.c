#include "../../SDL_internal.h"

#if SDL_VIDEO_OPENGL_EGL

#include "SDL.h"
#include "SDL_egl.h"
#include "SDL_opengl.h"

#include "SDL_malivideo.h"
#include "SDL_maliblitter.h"

#define MAX_CONFIGS 128

/* used to simplify code */
typedef struct mat4 {
    GLfloat v[16];
} mat4;

static GLchar* blit_vert_fmt =
"#version 100\n"
"varying vec2 vTexCoord;\n"
"attribute vec2 aVertCoord;\n"
"attribute vec2 aTexCoord;\n"
"uniform mat4 uProj;\n"
"uniform vec2 uTexSize;\n"
"void main() {\n"
"   %s\n"
"   %s\n"
"   gl_Position = uProj * vec4(aVertCoord, 0.0, 1.0);\n"
"}";

static GLchar* blit_frag_standard =
"#version 100\n"
"precision mediump float;\n"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"uniform vec2 uTexSize;\n"
"uniform vec2 uScale;\n"
"void main() {\n"
"   gl_FragColor = texture2D(uFBOTex, vTexCoord);\n"
"}\n";

// Ported from TheMaister's sharp-bilinear-simple.slang
static GLchar* blit_frag_bilinear_simple =
"#version 100\n"
"precision mediump float;\n"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"uniform vec2 uTexSize;\n"
"uniform vec2 uScale;\n"
"void main() {\n"
"   vec2 texel_floored = floor(vTexCoord);\n"
"   vec2 s = fract(vTexCoord);\n"
"   vec2 region_range = 0.5 - 0.5 / uScale;\n"
"   vec2 center_dist = s - 0.5;\n"
"   vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * uScale + 0.5;\n"
"   vec2 mod_texel = texel_floored + f;\n"
"   gl_FragColor = texture2D(uFBOTex, mod_texel / uTexSize);\n"
"}\n";

// Ported from Iquilez
static GLchar* blit_frag_quilez =
"#version 100\n"
"precision highp float;\n"
"varying vec2 vTexCoord;\n"
"uniform sampler2D uFBOTex;\n"
"uniform vec2 uTexSize;\n"
"uniform vec2 uScale;\n"
"void main() {\n"
"   vec2 p = vTexCoord + 0.5;\n"
"   vec2 i = floor(p);\n"
"   vec2 f = p - i;\n"
"   f = f*f*f*(f*(f*6.0-15.0)+10.0);\n"
"   p = i + f;\n"
"   p = (p - 0.5)/uTexSize;\n"
"   gl_FragColor = texture2D( uFBOTex, p );\n"
"}\n";

int MALI_Blitter_CreateContext(_THIS, MALI_Blitter *blitter, NativeWindowType nw)
{
    /* max 14 values plus terminator. */
    EGLint screen_attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };

    EGLint window_attribs[] = {
        EGL_NONE,
    };

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
	    EGL_NONE
    };

    EGLConfig configs[MAX_CONFIGS];
    EGLint config_chosen, config_count;

    if (!_this->egl_data) {
        SDL_SetError("EGL not initialized");
        return 0;
    }
    
    blitter->egl_display = _this->egl_data->egl_display;
    if (blitter->eglGetConfigs(blitter->egl_display, configs, MAX_CONFIGS, &config_count) == EGL_FALSE) {
        SDL_EGL_SetError("mali-fbdev: No compatible EGL configs", "eglGetConfigs");
        return 0;
    }

    if (!blitter->eglChooseConfig(blitter->egl_display, screen_attribs, configs, MAX_CONFIGS, &config_chosen))
    {
        SDL_EGL_SetError("mali-fbdev: Failed to choose an EGL config", "eglChooseConfig");
        return 0;
    }

    blitter->gl_context = blitter->eglCreateContext(blitter->egl_display,
                                      configs[0],
                                      EGL_NO_CONTEXT, context_attribs);
    if (blitter->gl_context == EGL_NO_CONTEXT) {
        SDL_EGL_SetError("mali-fbdev: Could not create EGL context", "eglCreateContext");
        return 0;
    }

    blitter->egl_surface = blitter->eglCreateWindowSurface(blitter->egl_display, configs[0], nw, window_attribs);
    if (blitter->egl_surface == EGL_NO_SURFACE) {
        SDL_EGL_SetError("mali-fbdev: failed to create window surface", "eglCreateContext");
        return 0;
    }

    return 1;
}

static void
get_aspect_correct_coords(int viewport[2], int plane[2], int rotation, GLfloat vert[4][4], GLfloat scale[2])
{
    /* FIXME: Sorry for the spaghetti! */
    float aspect_plane, aspect_viewport, ratio_x, ratio_y;
    int shift_x, shift_y, temp;

    // when sideways, invert plane coords
    if (rotation & 1) {
        temp = plane[0];
        plane[0] = plane[1];
        plane[1] = temp;
    }

    // Choose which edge to touch
    aspect_plane = (float)plane[0] / plane[1];
    aspect_viewport = (float)viewport[0] / viewport[1];

    if (aspect_viewport > aspect_plane) {
        // viewport wider than plane
        ratio_x = plane[0] * (float)((float)viewport[1] / plane[1]);
        ratio_y = viewport[1];
        shift_x = (viewport[0] - ratio_x) / 2.0f;
        shift_y = 0;
    } else {
        // plane wider than viewport
        ratio_x = viewport[0];
        ratio_y = plane[1] * (float)((float)viewport[0] / plane[0]);
        shift_x = 0;
        shift_y = (viewport[1] - ratio_y) / 2.0f;
    }

    // Instead of normalized UVs, use full texture size.
    vert[0][2] = (int)(0.0f * plane[0]); vert[0][3] = (int)(0.0f * plane[1]);
    vert[1][2] = (int)(0.0f * plane[0]); vert[1][3] = (int)(1.0f * plane[1]);
    vert[2][2] = (int)(1.0f * plane[0]); vert[2][3] = (int)(0.0f * plane[1]);
    vert[3][2] = (int)(1.0f * plane[0]); vert[3][3] = (int)(1.0f * plane[1]);

    // Get aspect corrected sizes within pixel boundaries
    vert[0][0] = (int)(0.0f * ratio_x) + shift_x; vert[0][1] = (int)(0.0f * ratio_y) + shift_y;
    vert[1][0] = (int)(0.0f * ratio_x) + shift_x; vert[1][1] = (int)(1.0f * ratio_y) + shift_y;
    vert[2][0] = (int)(1.0f * ratio_x) + shift_x; vert[2][1] = (int)(0.0f * ratio_y) + shift_y;
    vert[3][0] = (int)(1.0f * ratio_x) + shift_x; vert[3][1] = (int)(1.0f * ratio_y) + shift_y;

    // Get scale, for filtering.
    scale[0] = ratio_x / plane[0];
    scale[1] = ratio_y / plane[1];
}

static
void mat_ortho(float left, float right, float bottom, float top, float Result[4][4])
{
    *(mat4*)Result = (mat4){{[0 ... 15] = 0}};
    Result[0][0] = 2.0f / (right - left);
    Result[1][1] = 2.0f / (top - bottom);
    Result[2][2] = -1.0f;
    Result[3][0] = - (right + left) / (right - left);
    Result[3][1] = - (top + bottom) / (top - bottom);
    Result[3][3] = 1.0f;
}

static void
MALI_Blitter_GetTexture(_THIS, MALI_Blitter *blitter, MALI_EGL_Surface *surf)
{
    /* Define attributes of the EGLImage that will import our dmabuf file descriptor */
    EGLint attribute_list[] = {
        EGL_WIDTH, blitter->plane_width,
        EGL_HEIGHT, blitter->plane_height,
        EGL_LINUX_DRM_FOURCC_EXT, DRM_FORMAT_XRGB8888,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, blitter->plane_pitch,
        EGL_DMA_BUF_PLANE0_FD_EXT, surf->dmabuf_fd,
        EGL_NONE
    };

    /* Now create the EGLImage object. */
    surf->egl_image = blitter->eglCreateImageKHR(blitter->egl_display,
        EGL_NO_CONTEXT,
        EGL_LINUX_DMA_BUF_EXT,
        (EGLClientBuffer)NULL,
        attribute_list);
    if (surf->egl_image == EGL_NO_IMAGE_KHR) {
        SDL_EGL_SetError("Failed to create Blitter EGL Image", "eglCreateImageKHR");
        return;
    }

    /* Create a texture to host our image */
    blitter->glGenTextures(1, &surf->texture);
    blitter->glActiveTexture(GL_TEXTURE0);
    blitter->glBindTexture(GL_TEXTURE_2D, surf->texture);
    blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    blitter->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* And populate our texture with the EGLImage */
    blitter->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, surf->egl_image);
}

int
MALI_InitBlitterContext(_THIS, MALI_Blitter *blitter, SDL_WindowData *windata, NativeWindowType nw, int rotation)
{
    char *use_hq_scaler;
    GLchar msg[2048] = {}, blit_vert[2048] = {};
    const GLchar *sources[2] = { blit_vert, blit_frag_standard };
    float scale[2];

    /*
     * SDL_HQ_SCALER: Selects one of the available scalers:
     * - 0: Linear filtering
     * - 1: Sharp Bilinear Simple
     * - 2: Quilez
     */
    if ((use_hq_scaler = SDL_getenv("SDL_HQ_SCALER")) != NULL && *use_hq_scaler != '0') {
        switch (*use_hq_scaler) {
            case '1': sources[1] = blit_frag_bilinear_simple; break;
            case '2': sources[1] = blit_frag_quilez; break;
            default: use_hq_scaler = NULL; break;
        }
    } else {
        use_hq_scaler = NULL;
    }

    /* Bail out early if we're already initialized. */
    if (blitter->initted) {
        return 1;
    }

    /* The blitter thread needs to have an OpenGL ES 2.0 context available! */
    if (!MALI_Blitter_CreateContext(_this, blitter, nw)) {
        return 0;
    }
    
    if (!blitter->eglMakeCurrent(blitter->egl_display,
        blitter->egl_surface,
        blitter->egl_surface,
        blitter->gl_context))
    {
        SDL_EGL_SetError("Unable to make blitter EGL context current", "eglMakeCurrent");
        return 0;
    }

    /* Setup vertex shader coord orientation */
    SDL_snprintf(blit_vert, sizeof(blit_vert), blit_vert_fmt,
        /* rotation */
        (rotation == 0) ? "vTexCoord = aTexCoord;" :
        (rotation == 1) ? "vTexCoord = vec2(aTexCoord.y, -aTexCoord.x);" :
        (rotation == 2) ? "vTexCoord = vec2(-aTexCoord.x, -aTexCoord.y);" :
        (rotation == 3) ? "vTexCoord = vec2(-aTexCoord.y, aTexCoord.x);" :
        "#error Orientation out of scope",
        /* scalers */
        (use_hq_scaler) ? "vTexCoord = vTexCoord;"
                        : "vTexCoord = vTexCoord / uTexSize;");

    /* Compile vertex shader */
    blitter->vert = blitter->glCreateShader(GL_VERTEX_SHADER);
    blitter->glShaderSource(blitter->vert, 1, &sources[0], NULL);
    blitter->glCompileShader(blitter->vert);
    blitter->glGetShaderInfoLog(blitter->vert, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Blitter Vertex Shader Info: %s\n", msg);

    /* Compile the fragment shader */
    blitter->frag = blitter->glCreateShader(GL_FRAGMENT_SHADER);
    blitter->glShaderSource(blitter->frag, 1, &sources[1], NULL);
    blitter->glCompileShader(blitter->frag);
    blitter->glGetShaderInfoLog(blitter->frag, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Blitter Fragment Shader Info: %s\n", msg);

    blitter->prog = blitter->glCreateProgram();
    blitter->glAttachShader(blitter->prog, blitter->vert);
    blitter->glAttachShader(blitter->prog, blitter->frag);

    blitter->glLinkProgram(blitter->prog);
    blitter->loc_aVertCoord = blitter->glGetAttribLocation(blitter->prog, "aVertCoord");
    blitter->loc_aTexCoord = blitter->glGetAttribLocation(blitter->prog, "aTexCoord");
    blitter->loc_uFBOtex = blitter->glGetUniformLocation(blitter->prog, "uFBOTex");
    blitter->loc_uProj = blitter->glGetUniformLocation(blitter->prog, "uProj");
    blitter->loc_uTexSize = blitter->glGetUniformLocation(blitter->prog, "uTexSize");
    blitter->loc_uScale = blitter->glGetUniformLocation(blitter->prog, "uScale");

    blitter->glGetProgramInfoLog(blitter->prog, sizeof(msg), NULL, msg);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "Blitter Program Info: %s\n", msg);

    /* Setup programs */
    blitter->glUseProgram(blitter->prog);
    blitter->glUniform1i(blitter->loc_uFBOtex, 0);

    /* Prepare projection and aspect corrected bounds */
    mat_ortho(0, blitter->viewport_width, 0, blitter->viewport_height, blitter->mat_projection);
    get_aspect_correct_coords(
        (int [2]){blitter->viewport_width, blitter->viewport_height},
        (int [2]){blitter->plane_width, blitter->plane_height},
        rotation,
        blitter->vert_buffer_data,
        scale
    );

    /* Setup viewport, projection, scale, texture size */
    blitter->glViewport(0, 0, blitter->viewport_width, blitter->viewport_height);
    blitter->glUniformMatrix4fv(blitter->loc_uProj, 1, 0, (GLfloat*)blitter->mat_projection);
    blitter->glUniform2f(blitter->loc_uScale, scale[0], scale[1]);
    blitter->glUniform2f(blitter->loc_uTexSize, blitter->plane_width, blitter->plane_height);

    /* Generate buffers */
    blitter->glGenBuffers(1, &blitter->vbo);
    blitter->glGenVertexArraysOES(1, &blitter->vao);

    /* Populate buffers */
    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindBuffer(GL_ARRAY_BUFFER, blitter->vbo);
    blitter->glEnableVertexAttribArray(blitter->loc_aVertCoord);
    blitter->glEnableVertexAttribArray(blitter->loc_aTexCoord);
    blitter->glVertexAttribPointer(blitter->loc_aVertCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(0 * sizeof(float)));
    blitter->glVertexAttribPointer(blitter->loc_aTexCoord, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    blitter->glBufferData(GL_ARRAY_BUFFER, sizeof(blitter->vert_buffer_data), blitter->vert_buffer_data, GL_STATIC_DRAW);
    
    for (int i = 0; i < SDL_arraysize(windata->surface); i++) {
        MALI_Blitter_GetTexture(_this, blitter, &windata->surface[i]);
    }

    blitter->initted = 1;
    return 1;
}

void
MALI_DeinitBlitterContext(_THIS, MALI_Blitter *blitter)
{
    int i;
    SDL_Window *window;
    SDL_WindowData *windata;

    /* Delete all texture and related egl objects */
    if (blitter->window) {
        window = blitter->window;
        windata = (SDL_WindowData *)window->driverdata;

        blitter->glBindTexture(GL_TEXTURE_2D, 0);
        for (i = 0; i < SDL_arraysize(windata->surface); i++) {
            blitter->glDeleteTextures(1, &windata->surface[i].texture);
            blitter->eglDestroyImageKHR(blitter->egl_display, windata->surface[i].egl_image);
        }
    }

    /* Tear down egl */
    blitter->eglMakeCurrent(blitter->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    blitter->eglDestroySurface(blitter->egl_display, blitter->egl_surface);
    blitter->eglDestroyContext(blitter->egl_display, blitter->gl_context);
    blitter->eglReleaseThread();

    blitter->window = NULL;
    blitter->initted = 0;
    SDL_LogInfo(SDL_LOG_CATEGORY_VIDEO, "MALI_BlitterThread: Released thread.\n");
}

void
MALI_Blitter_Blit(_THIS, MALI_Blitter *blitter, GLuint texture)
{
    /* Simple quad rendering. */
    blitter->glBindVertexArrayOES(blitter->vao);
    blitter->glBindTexture(GL_TEXTURE_2D, texture);
    blitter->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

static void MALI_Blitter_LoadFuncs(MALI_Blitter *blitter)
{
    int fail = 0;
    blitter->egl_obj = SDL_LoadObject("libEGL.so");
    blitter->gles2_obj = SDL_LoadObject("libGLESv2.so");
    if (!blitter->egl_obj || !blitter->gles2_obj) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed loading one or more dynamic libraries (%p %p).", blitter->gles2_obj, blitter->egl_obj);
        SDL_Quit();
    }

    if ((blitter->eglGetProcAddress = SDL_LoadFunction(blitter->egl_obj, "eglGetProcAddress")) == NULL) {
        SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Could not locate eglGetProcAddress.");
        SDL_Quit();
    }

    /* Attempt to initialize necessary functions */
    #define SDL_PROC(ret,func,params) \
        blitter->func = blitter->eglGetProcAddress(#func); \
        if (blitter->func == NULL) \
            blitter->func = SDL_LoadFunction(blitter->egl_obj, #func); \
        if (blitter->func == NULL) \
        { \
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed loading \"%s\".", #func); \
            fail = 1; \
        }
    #include "SDL_maliblitter_egl_funcs.h"
    #include "SDL_maliblitter_gles_funcs.h"
    #undef SDL_PROC

    if (fail) {
        SDL_Quit();
    }
}

int MALI_BlitterThread(void *data)
{
    int prevSwapInterval = -1;
    MALI_Blitter *blitter = (MALI_Blitter*)data;
    _THIS = blitter->_this;
    SDL_Window *window;
    SDL_WindowData *windata;
    SDL_VideoDisplay *display;
    SDL_DisplayData *dispdata = SDL_GetDisplayDriverData(0);
    unsigned int page;
    MALI_EGL_Surface *current_surface;
    
    MALI_Blitter_LoadFuncs(blitter);

    /* Signal triplebuf available */
    SDL_LockMutex(blitter->mutex);
    SDL_CondSignal(blitter->cond);

    for (;;) {
        SDL_CondWait(blitter->cond, blitter->mutex);        

        // A thread stop can be either due to reconfigure requested, or due to
        // SDL teardown, in both cases, we will destroy some resources.
        if (blitter->thread_stop != 0) {
            if (blitter->initted) {
                MALI_DeinitBlitterContext(_this, blitter);
            }

            // Signal 2 means we want to quit.
            if (blitter->thread_stop == 2) {
                break;
            }

            blitter->thread_stop = 0;
            continue;
        }

        window = blitter->window;
        windata = (SDL_WindowData *)window->driverdata;
        display = SDL_GetDisplayForWindow(window);
        dispdata = (SDL_DisplayData *)display->driverdata;

        /* Initialize blitter on the first out frame we have */
        if (!MALI_InitBlitterContext(_this, blitter, windata, (NativeWindowType)&dispdata->native_display, blitter->rotation))
        {
            SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "Failed to initialize blitter thread");
            SDL_Quit();
        }

        if (prevSwapInterval != _this->egl_data->egl_swapinterval) {
            blitter->eglSwapInterval(blitter->egl_display, _this->egl_data->egl_swapinterval);
            prevSwapInterval = _this->egl_data->egl_swapinterval;
        }

        /* Flip the most recent back buffer with the front buffer */
        page = windata->queued_buffer;
        windata->queued_buffer = windata->front_buffer;
        windata->front_buffer = page;

        /* select surface to wait and blit */
        current_surface = &windata->surface[windata->queued_buffer];

        /* wait for fence and flip display */
        if (blitter->eglClientWaitSyncKHR(
            blitter->egl_display,
            current_surface->egl_fence, 
            EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 
            EGL_FOREVER_NV))
        {
            /* Discarding previous data... */
            blitter->glClear(GL_COLOR_BUFFER_BIT);
            blitter->glClearColor(0.0, 0.0, 0.0, 1.0);

            /* Perform blitting */
            MALI_Blitter_Blit(_this, blitter, current_surface->texture);

            /* Perform the final buffer swap. */
            if (!(blitter->eglSwapBuffers(blitter->egl_display, blitter->egl_surface))) {
                SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "eglSwapBuffers failed");
                return 0;
            }
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "Sync %p failed.", current_surface->egl_fence);
        }
    }    

    /* Signal thread done */
    SDL_UnlockMutex(blitter->mutex);
    return 0;
}

void MALI_BlitterInit(_THIS, MALI_Blitter *blitter)
{
    if (!blitter)
        return;
    
    blitter->thread_stop = 1;
    blitter->mutex = SDL_CreateMutex();
    blitter->cond = SDL_CreateCond();
    blitter->thread = SDL_CreateThread(MALI_BlitterThread, "MALI_BlitterThread", blitter);
}

void MALI_BlitterReconfigure(_THIS, SDL_Window *window, MALI_Blitter *blitter)
{
    SDL_VideoDisplay *display = SDL_GetDisplayForWindow(window);
    SDL_DisplayData *dispdata = (SDL_DisplayData *)display->driverdata;

    if (!blitter)
        return;

    /* Flag a reconfigure request */
    SDL_LockMutex(blitter->mutex);
    blitter->window = window;
    blitter->egl_display = _this->egl_data->egl_display;
    blitter->viewport_width = dispdata->native_display.width,
    blitter->viewport_height = dispdata->native_display.height,
    blitter->plane_width = window->w;
    blitter->plane_height = window->h;
    blitter->plane_pitch = dispdata->stride;
    blitter->rotation = dispdata->rotation;
    blitter->thread_stop = 1;

    /* Signal thread in order to perform stop */
    SDL_CondSignal(blitter->cond);
    SDL_UnlockMutex(blitter->mutex);
}

void MALI_BlitterQuit(MALI_Blitter *blitter)
{
    if (blitter == NULL)
        return;

    /* Flag a stop request */
    SDL_LockMutex(blitter->mutex);
    blitter->thread_stop = 2;

    /* Signal thread in order to perform stop */
    SDL_CondSignal(blitter->cond);
    SDL_UnlockMutex(blitter->mutex);

    /* Wait and perform teardown */
    SDL_WaitThread(blitter->thread, NULL);
    blitter->thread = NULL;
    SDL_DestroyMutex(blitter->mutex);
    SDL_DestroyCond(blitter->cond);
}

#endif /* SDL_VIDEO_OPENGL_EGL */