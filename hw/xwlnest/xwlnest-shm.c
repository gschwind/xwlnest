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

#ifdef HAVE_CONFIG_H
#include <dix-config.h>
#endif

#include "xwlnest-shm.h"

#ifndef HAVE_MKOSTEMP
static int
set_cloexec_or_close(int fd)
{
    long flags;

    if (fd == -1)
        return -1;

    flags = fcntl(fd, F_GETFD);
    if (flags == -1)
        goto err;

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        goto err;

    return fd;

 err:
    close(fd);
    return -1;
}
#endif

static int
create_tmpfile_cloexec(char *tmpname)
{
    int fd;

#ifdef HAVE_MKOSTEMP
    fd = mkostemp(tmpname, O_CLOEXEC);
    if (fd >= 0)
        unlink(tmpname);
#else
    fd = mkstemp(tmpname);
    if (fd >= 0) {
        fd = set_cloexec_or_close(fd);
        unlink(tmpname);
    }
#endif

    return os_move_fd(fd);
}

/*
 * Create a new, unique, anonymous file of the given size, and
 * return the file descriptor for it. The file descriptor is set
 * CLOEXEC. The file is immediately suitable for mmap()'ing
 * the given size at offset zero.
 *
 * The file should not have a permanent backing store like a disk,
 * but may have if XDG_RUNTIME_DIR is not properly implemented in OS.
 *
 * The file name is deleted from the file system.
 *
 * The file is suitable for buffer sharing between processes by
 * transmitting the file descriptor over Unix sockets using the
 * SCM_RIGHTS methods.
 *
 * If the C library implements posix_fallocate(), it is used to
 * guarantee that disk space is available for the file at the
 * given size. If disk space is insufficent, errno is set to ENOSPC.
 * If posix_fallocate() is not supported, program may receive
 * SIGBUS on accessing mmap()'ed file contents instead.
 */
static int
os_create_anonymous_file(off_t size)
{
    static const char template[] = "/xwayland-shared-XXXXXX";
    const char *path;
    char *name;
    int fd;
    int ret;

    path = getenv("XDG_RUNTIME_DIR");
    if (!path) {
        errno = ENOENT;
        return -1;
    }

    name = malloc(strlen(path) + sizeof(template));
    if (!name)
        return -1;

    strcpy(name, path);
    strcat(name, template);

    fd = create_tmpfile_cloexec(name);

    free(name);

    if (fd < 0)
        return -1;

#ifdef HAVE_POSIX_FALLOCATE
    do {
        ret = posix_fallocate(fd, 0, size);
    } while (ret == EINTR);

    if (ret != 0) {
        close(fd);
        errno = ret;
        return -1;
    }
#else
    do {
        ret = ftruncate(fd, size);
    } while (ret == -1 && errno == EINTR);

    if (ret < 0) {
        close(fd);
        return -1;
    }
#endif

    return fd;
}

static uint32_t
shm_format_for_depth(int depth)
{
    switch (depth) {
    case 32:
        return WL_SHM_FORMAT_ARGB8888;
    case 24:
    default:
        return WL_SHM_FORMAT_XRGB8888;
#ifdef WL_SHM_FORMAT_RGB565
    case 16:
        /* XXX: Check run-time protocol version too */
        return WL_SHM_FORMAT_RGB565;
#endif
    }
}

/* NOTE: the depth must be 24 or 32 */
struct xwl_pixmap *
xwlnest_shm_create_pixmap(ScreenPtr screen, int width, int height, int depth)
{
    struct xwl_pixmap *xwl_pixmap;
    size_t size, stride;

    xwl_pixmap = malloc(sizeof *xwl_pixmap);
    if (xwl_pixmap == NULL)
        goto err_destroy_pixmap;

    xwl_pixmap->pixmap = fbCreatePixmap(screen, 0, 0, depth, 0);
    if (!xwl_pixmap->pixmap)
        return NULL;

    stride = PixmapBytePad(width, depth);
    size = stride * height;
    xwl_pixmap->buffer = NULL;
    xwl_pixmap->size = size;
    xwl_pixmap->fd = os_create_anonymous_file(size);
    if (xwl_pixmap->fd < 0)
        goto err_free_xwl_pixmap;

    xwl_pixmap->data = mmap(NULL, size, PROT_READ | PROT_WRITE,
                                  MAP_SHARED, xwl_pixmap->fd, 0);
    if (xwl_pixmap->data == MAP_FAILED)
        goto err_close_fd;

    if (!(*screen->ModifyPixmapHeader) (xwl_pixmap->pixmap, width, height, depth,
                                        BitsPerPixel(depth),
                                        stride, xwl_pixmap->data))
        goto err_munmap;

    return xwl_pixmap;

 err_munmap:
    munmap(xwl_pixmap->data, size);
 err_close_fd:
    close(xwl_pixmap->fd);
 err_free_xwl_pixmap:
    free(xwl_pixmap);
 err_destroy_pixmap:

    return NULL;
}

Bool
xwlnest_shm_destroy_pixmap(struct xwl_pixmap *xwl_pixmap)
{
    Bool ret = fbDestroyPixmap(xwl_pixmap->pixmap);

    if (xwl_pixmap) {
        if (xwl_pixmap->buffer)
            wl_buffer_destroy(xwl_pixmap->buffer);
        munmap(xwl_pixmap->data, xwl_pixmap->size);
        close(xwl_pixmap->fd);
        free(xwl_pixmap);
    }

    return ret;
}

struct wl_buffer *
xwlnest_shm_pixmap_get_wl_buffer(struct wl_shm *shm, struct xwl_pixmap *xwl_pixmap)
{
    struct wl_shm_pool *pool;
    uint32_t format;

    if (xwl_pixmap->buffer)
        return xwl_pixmap->buffer;

    pool = wl_shm_create_pool(shm, xwl_pixmap->fd, xwl_pixmap->size);

    format = shm_format_for_depth(xwl_pixmap->pixmap->drawable.depth);
    xwl_pixmap->buffer = wl_shm_pool_create_buffer(pool, 0,
            xwl_pixmap->pixmap->drawable.width,
            xwl_pixmap->pixmap->drawable.height,
            xwl_pixmap->pixmap->devKind, format);

    wl_shm_pool_destroy(pool);

    return xwl_pixmap->buffer;
}
