/*
 * Copyright © 2014 Intel Corporation
 * Copyright © 2012 Collabora, Ltd.
 * Copyright © 2016 Benoit Gschwind
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the
 * copyright holders not be used in advertising or publicity
 * pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

#ifndef HW_XWLNEST_XWLNEST_SHM_H_
#define HW_XWLNEST_XWLNEST_SHM_H_

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include <fb.h>
#include <input.h>
#include <dix.h>
#include <randrstr.h>
#include <exevents.h>

#include <wayland-client.h>

struct xwl_pixmap {
    struct wl_buffer *buffer;
    int fd;
    void *data;
    size_t size;

    /* this pixmap can be used to do standard Xorg draw */
    PixmapPtr pixmap;
};

struct xwl_pixmap *
xwlnest_shm_create_pixmap(ScreenPtr screen, int width, int height, int depth);

struct wl_buffer *
xwlnest_shm_pixmap_get_wl_buffer(struct wl_shm *shm, struct xwl_pixmap *xwl_pixmap);

Bool
xwlnest_shm_destroy_pixmap(struct xwl_pixmap *xwl_pixmap);

#endif /* HW_XWLNEST_XWLNEST_SHM_H_ */
