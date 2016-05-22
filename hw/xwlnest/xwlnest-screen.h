/*
 * Copyright © 2014 Intel Corporation
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
#ifndef HW_XWLNEST_XWLNEST_SCREEN_H_
#define HW_XWLNEST_XWLNEST_SCREEN_H_

#ifdef HAVE_DIX_CONFIG_H
#include <dix-config.h>
#endif

#if defined(WIN32)
#include <X11/Xwinsock.h>
#endif
#include <stdio.h>
#include <X11/X.h>
#include <X11/Xproto.h>
#include <X11/Xos.h>
#include "scrnintstr.h"
#include "servermd.h"
#define PSZ 8
#include "fb.h"
#include "colormapst.h"
#include "gcstruct.h"
#include "input.h"
#include "mipointer.h"
#include "micmap.h"
#include <sys/types.h>
#ifdef HAVE_MMAP
#include <sys/mman.h>
#ifndef MAP_FILE
#define MAP_FILE 0
#endif
#endif                          /* HAVE_MMAP */
#include <sys/stat.h>
#include <errno.h>
#ifndef WIN32
#include <sys/param.h>
#endif
#include <X11/XWDFile.h>
#ifdef HAS_SHM
#include <sys/ipc.h>
#include <sys/shm.h>
#endif                          /* HAS_SHM */
#include "dix.h"
#include "miline.h"
#include "glx_extinit.h"
#include "randrstr.h"

#include <wayland-client.h>
#include "xwlnest-shm.h"

#define VFB_DEFAULT_WIDTH      1280
#define VFB_DEFAULT_HEIGHT      720
#define VFB_DEFAULT_DEPTH        24
#define VFB_DEFAULT_WHITEPIXEL    1
#define VFB_DEFAULT_BLACKPIXEL    0
#define VFB_DEFAULT_LINEBIAS      0
#define XWD_WINDOW_NAME_LEN      60

struct xwlnest_screen {
    int width;
    int paddedBytesWidth;
    int paddedWidth;
    int height;
    int depth;
    int bitsPerPixel;
    int sizeInBytes;
    int ncolors;

    Pixel blackPixel;
    Pixel whitePixel;
    unsigned int lineBias;

    ScreenPtr pScreen;

    RealizeWindowProcPtr RealizeWindow;
    CloseScreenProcPtr closeScreen;

    int wayland_fd;
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_registry *input_registry;

    struct xwl_pixmap *pixmap; // shared pixmap with wayland.

    int prepare_read;
    int has_damage;

    /* output window data */
    struct wl_surface *surface;
    struct wl_shell_surface *shell_surface;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_shell *shell;
    struct wl_callback *frame_callback;

    /* inputs related data */
    uint32_t serial;
    struct xorg_list seat_list;

    DamagePtr damage;
    PixmapPtr output_pixmap;

};

struct xwl_touch {
    //struct xwl_window *window;
    int32_t id;
    int x, y;
    struct xorg_list link_touch;
};

struct xwl_seat {
    DeviceIntPtr pointer;
    DeviceIntPtr keyboard;
    DeviceIntPtr touch;
    struct xwlnest_screen * xwl_screen;
    struct wl_seat *seat;
    struct wl_pointer *wl_pointer;
    struct wl_keyboard *wl_keyboard;
    struct wl_touch *wl_touch;
    struct wl_array keys;
    int has_focus_window;
    uint32_t id;
    uint32_t pointer_enter_serial;
    struct xorg_list link;
    CursorPtr x_cursor;
    struct wl_surface *cursor;
    struct wl_callback *cursor_frame_cb;
    Bool cursor_needs_update;

    struct xorg_list touches;

    size_t keymap_size;
    char *keymap;
    int has_keyboard_focus;
};

extern int vfbNumScreens;
extern struct xwlnest_screen *vfbScreens;

void
xwl_seat_set_cursor(struct xwl_seat *xwl_seat);

void
xwl_seat_destroy(struct xwl_seat *xwl_seat);

Bool
xwl_screen_init_cursor(struct xwlnest_screen * xwl_screen);

#endif /* HW_XWLNEST_XWLNEST_SCREEN_H_ */
