/*
 * Copyright (C) 2017 João H. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef __MALI_H__
#define __MALI_H__

#include <stdint.h>

#define fourcc_code(a, b, c, d) ((__u32)(a) | ((__u32)(b) << 8) | \
				 ((__u32)(c) << 16) | ((__u32)(d) << 24))

#define DRM_FORMAT_ARGB8888	fourcc_code('A', 'R', '2', '4')
#define DRM_FORMAT_XRGB8888	fourcc_code('X', 'R', '2', '4')


#define MALI_ALIGN(val, align)  (((val) + (align) - 1) & ~((align) - 1))
#define MALI_FORMAT_ARGB8888       (0x10bb60a)

typedef struct fbdev_window_s
{
	unsigned short width;
	unsigned short height;
} fbdev_window_s;

typedef struct mali_plane {
    unsigned long stride;
    unsigned long size;
    unsigned long offset;
} mali_plane;

typedef struct mali_pixmap {
    int width, height; 

    mali_plane planes[3];

    uint64_t format; //see 0x004e3c28...
    int handles[3]; //seems to just be fds, see 0x004ec14c...
    struct
    {
        uint32_t format; // drm_fourcc
        uint64_t modifier; // afbc etc
        uint32_t dataspace; // colorspace definitions e.g. bt709, srgb, etc
    } drm_fourcc; // apparently an alternative to setting the format field?
                  // set .format = 0 and fill this if available on your blob.
} mali_pixmap;

#endif /* __MALI_H__ */