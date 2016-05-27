/*

Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

#include "xwlnest-screen.h"

#ifdef XF86VIDMODE
#include <X11/extensions/xf86vmproto.h>
_X_EXPORT Bool noXFree86VidModeExtension;
#endif

int vfbNumScreens = 0;
struct xwlnest_screen *vfbScreens = NULL;

static struct xwlnest_screen defaultScreenInfo = {
    .width = VFB_DEFAULT_WIDTH,
    .height = VFB_DEFAULT_HEIGHT,
    .depth = VFB_DEFAULT_DEPTH,
    .blackPixel = VFB_DEFAULT_BLACKPIXEL,
    .whitePixel = VFB_DEFAULT_WHITEPIXEL,
    .lineBias = VFB_DEFAULT_LINEBIAS,
};

typedef enum { NORMAL_MEMORY_FB } fbMemType;

static void
vfbDestroyOutputWindow(struct xwlnest_screen * pvfb);

static int
vfbBitsPerPixel(int depth)
{
    if (depth == 1)
        return 1;
    else if (depth <= 8)
        return 8;
    else if (depth <= 16)
        return 16;
    else
        return 32;
}

void
ddxGiveUp(enum ExitCode error)
{
    // TODO
}

void
AbortDDX(enum ExitCode error)
{
    ddxGiveUp(error);
}

#ifdef __APPLE__
void
DarwinHandleGUI(int argc, char *argv[])
{
}
#endif

void
OsVendorInit(void)
{
}

void
OsVendorFatalError(const char *f, va_list args)
{
}

#if defined(DDXBEFORERESET)
void
ddxBeforeReset(void)
{
    return;
}
#endif

void
ddxUseMsg(void)
{
    ErrorF("-screen WxH            set screen's width and height\n");
    ErrorF("-linebias n            adjust thin line pixelization\n");
    ErrorF("-blackpixel n          pixel value for black\n");
    ErrorF("-whitepixel n          pixel value for white\n");
}

int
ddxProcessArgument(int argc, char *argv[], int i)
{
    static Bool firstTime = TRUE;
    static int lastScreen = -1;
    struct xwlnest_screen *currentScreen;

    if (firstTime) {
        firstTime = FALSE;
    }

    if (lastScreen == -1)
        currentScreen = &defaultScreenInfo;
    else
        currentScreen = &vfbScreens[lastScreen];

#define CHECK_FOR_REQUIRED_ARGUMENTS(num) \
    if (((i + num) >= argc) || (!argv[i + num])) {                      \
      ErrorF("Required argument to %s not specified\n", argv[i]);       \
      UseMsg();                                                         \
      FatalError("Required argument to %s not specified\n", argv[i]);   \
    }

    if (strcmp(argv[i], "-screen") == 0) {      /* -screen WxH */
        int screenNum = 0;

        CHECK_FOR_REQUIRED_ARGUMENTS(1);
        screenNum = 0;
        /* The protocol only has a CARD8 for number of screens in the
           connection setup block, so don't allow more than that. */
        if ((screenNum < 0) || (screenNum >= 255)) {
            ErrorF("Invalid screen number %d\n", screenNum);
            UseMsg();
            FatalError("Invalid screen number %d passed to -screen\n",
                       screenNum);
        }

        if (vfbNumScreens <= screenNum) {
            vfbScreens =
                reallocarray(vfbScreens, screenNum + 1, sizeof(*vfbScreens));
            if (!vfbScreens)
                FatalError("Not enough memory for screen %d\n", screenNum);
            for (; vfbNumScreens <= screenNum; ++vfbNumScreens)
                vfbScreens[vfbNumScreens] = defaultScreenInfo;
        }

        if (2 != sscanf(argv[i + 1], "%dx%d",
                        &vfbScreens[screenNum].width,
                        &vfbScreens[screenNum].height)) {
            ErrorF("Invalid screen configuration %s\n", argv[i + 2]);
            UseMsg();
            FatalError("Invalid screen configuration %s for -screen %d\n",
                       argv[i + 1], screenNum);
        }

        vfbScreens[screenNum].depth = 24;

        lastScreen = screenNum;
        return 2;
    }

    if (strcmp(argv[i], "-blackpixel") == 0) {  /* -blackpixel n */
        CHECK_FOR_REQUIRED_ARGUMENTS(1);
        currentScreen->blackPixel = atoi(argv[++i]);
        return 2;
    }

    if (strcmp(argv[i], "-whitepixel") == 0) {  /* -whitepixel n */
        CHECK_FOR_REQUIRED_ARGUMENTS(1);
        currentScreen->whitePixel = atoi(argv[++i]);
        return 2;
    }

    if (strcmp(argv[i], "-linebias") == 0) {    /* -linebias n */
        CHECK_FOR_REQUIRED_ARGUMENTS(1);
        currentScreen->lineBias = atoi(argv[++i]);
        return 2;
    }

    return 0;
}

static Bool
vfbSaveScreen(ScreenPtr pScreen, int on)
{
    return TRUE;
}

static Bool
vfbCloseScreen(ScreenPtr pScreen)
{
    struct xwlnest_screen * pvfb = &vfbScreens[pScreen->myNum];

    RemoveNotifyFd(pvfb->wayland_fd);

    if(pvfb->damage) {
        //DamageDestroy(pvfb->damage);
        pvfb->damage = NULL;
    }

    if(pvfb->pixmap) {
        vfbDestroyOutputWindow(pvfb);
    }

    wl_display_disconnect(pvfb->display);

    if(pvfb->output_pixmap) {
        free(pvfb->output_pixmap->devPrivate.ptr);
        pvfb->output_pixmap = NULL;
    }

    pScreen->CloseScreen = pvfb->closeScreen;

    /*
     * fb overwrites miCloseScreen, so do this here
     */
    if (pScreen->devPrivate)
        (*pScreen->DestroyPixmap) (pScreen->devPrivate);
    pScreen->devPrivate = NULL;

    return pScreen->CloseScreen(pScreen);
}

static Bool
vfbRROutputValidateMode(ScreenPtr           pScreen,
                        RROutputPtr         output,
                        RRModePtr           mode)
{
    rrScrPriv(pScreen);

    if (pScrPriv->minWidth <= mode->mode.width &&
        pScrPriv->maxWidth >= mode->mode.width &&
        pScrPriv->minHeight <= mode->mode.height &&
        pScrPriv->maxHeight >= mode->mode.height)
        return TRUE;
    else
        return FALSE;
}

static Bool
vfbRRScreenSetSize(ScreenPtr  pScreen,
                   CARD16     width,
                   CARD16     height,
                   CARD32     mmWidth,
                   CARD32     mmHeight)
{
    // Prevent screen updates while we change things around
    SetRootClip(pScreen, ROOT_CLIP_NONE);

    pScreen->width = width;
    pScreen->height = height;
    pScreen->mmWidth = mmWidth;
    pScreen->mmHeight = mmHeight;

    // Restore the ability to update screen, now with new dimensions
    SetRootClip(pScreen, ROOT_CLIP_FULL);

    RRScreenSizeNotify (pScreen);
    RRTellChanged(pScreen);

    return TRUE;
}

static Bool
vfbRRCrtcSet(ScreenPtr pScreen,
             RRCrtcPtr crtc,
             RRModePtr mode,
             int       x,
             int       y,
             Rotation  rotation,
             int       numOutput,
             RROutputPtr *outputs)
{
  return RRCrtcNotify(crtc, mode, x, y, rotation, NULL, numOutput, outputs);
}

static Bool
vfbRRGetInfo(ScreenPtr pScreen, Rotation *rotations)
{
    return TRUE;
}

static Bool
vfbRandRInit(ScreenPtr pScreen)
{
    rrScrPrivPtr pScrPriv;
#if RANDR_12_INTERFACE
    RRModePtr  mode;
    RRCrtcPtr  crtc;
    RROutputPtr        output;
    xRRModeInfo modeInfo;
    char       name[64];
#endif

    if (!RRScreenInit (pScreen))
       return FALSE;
    pScrPriv = rrGetScrPriv(pScreen);
    pScrPriv->rrGetInfo = vfbRRGetInfo;
#if RANDR_12_INTERFACE
    pScrPriv->rrCrtcSet = vfbRRCrtcSet;
    pScrPriv->rrScreenSetSize = vfbRRScreenSetSize;
    pScrPriv->rrOutputSetProperty = NULL;
#if RANDR_13_INTERFACE
    pScrPriv->rrOutputGetProperty = NULL;
#endif
    pScrPriv->rrOutputValidateMode = vfbRROutputValidateMode;
    pScrPriv->rrModeDestroy = NULL;

    RRScreenSetSizeRange (pScreen,
                         1, 1,
                         pScreen->width, pScreen->height);

    sprintf (name, "%dx%d", pScreen->width, pScreen->height);
    memset (&modeInfo, '\0', sizeof (modeInfo));
    modeInfo.width = pScreen->width;
    modeInfo.height = pScreen->height;
    modeInfo.nameLength = strlen (name);

    mode = RRModeGet (&modeInfo, name);
    if (!mode)
       return FALSE;

    crtc = RRCrtcCreate (pScreen, NULL);
    if (!crtc)
       return FALSE;

    output = RROutputCreate (pScreen, "screen", 6, NULL);
    if (!output)
       return FALSE;
    if (!RROutputSetClones (output, NULL, 0))
       return FALSE;
    if (!RROutputSetModes (output, &mode, 1, 0))
       return FALSE;
    if (!RROutputSetCrtcs (output, &crtc, 1))
       return FALSE;
    if (!RROutputSetConnection (output, RR_Connected))
       return FALSE;
    RRCrtcNotify (crtc, mode, 0, 0, RR_Rotate_0, NULL, 1, &output);
#endif
    return TRUE;
}


static void
shell_surface_ping(void *data,
                   struct wl_shell_surface *shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static void
shell_surface_configure(void *data,
                        struct wl_shell_surface *wl_shell_surface,
                        uint32_t edges, int32_t width, int32_t height)
{
}

static void
shell_surface_popup_done(void *data, struct wl_shell_surface *wl_shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    shell_surface_ping,
    shell_surface_configure,
    shell_surface_popup_done
};

static void
frame_callback(void *data,
               struct wl_callback *callback,
               uint32_t time)
{
    struct xwlnest_screen * pvfb = data;
    pvfb->frame_callback = NULL;
}

static const struct wl_callback_listener frame_listener = {
    frame_callback
};

static void
xwlnest_screen_post_damage(struct xwlnest_screen * pvfb)
{
    RegionPtr region;
    struct wl_buffer *buffer;

    if(!pvfb->damage)
        return;

    if(!pvfb->has_damage)
        return;

    /* If we're waiting on a frame callback from the server,
     * don't attach a new buffer. */
    if (pvfb->frame_callback)
        return;

    region = DamageRegion(pvfb->damage);
    if(!region)
        return;

    buffer = xwlnest_shm_pixmap_get_wl_buffer(pvfb->shm, pvfb->pixmap);
    if (buffer == NULL) {
        ErrorF("xwlnest_shm_pixmap_get_wl_buffer failed\n");
    }

    wl_surface_attach(pvfb->surface, buffer, 0, 0);
    {
        GCPtr pGC = GetScratchGC(pvfb->output_pixmap->drawable.depth, pvfb->pScreen);
        if (pGC) {
            BoxPtr pBox = RegionRects(region);
            int nBox = RegionNumRects(region);

            ChangeGCVal val;
            val.val = IncludeInferiors;
            ChangeGC(NullClient, pGC, GCSubwindowMode, &val);
            ValidateGC(&pvfb->pixmap->pixmap->drawable, pGC);

            while (nBox--) {
                int x = pBox->x1;
                int y = pBox->y1;
                int w = pBox->x2 - pBox->x1;
                int h = pBox->y2 - pBox->y1;

                wl_surface_damage(pvfb->surface, pvfb->border_left_size + x,
                        pvfb->border_top_size + y, w, h);
                (*pGC->ops->CopyArea)(&pvfb->output_pixmap->drawable,
                        &pvfb->pixmap->pixmap->drawable, pGC, x, y, w, h,
                        pvfb->border_left_size + x, pvfb->border_top_size + y);

                pBox++;
            }
            FreeScratchGC(pGC);
        }
    }

    pvfb->frame_callback = wl_surface_frame(pvfb->surface);
    wl_callback_add_listener(pvfb->frame_callback, &frame_listener, pvfb);

    wl_surface_commit(pvfb->surface);
    DamageEmpty(pvfb->damage);
    pvfb->has_damage = 0;


}

static void
xwlnest_paint_window_decoration(struct xwlnest_screen * pvfb) {
    xRectangle rect[4];
    ChangeGCVal val;

    GCPtr pGC = GetScratchGC(pvfb->output_pixmap->drawable.depth, pvfb->pScreen);
    if (!pGC) {
        return;
    }

    val.val = 0x00cccccc; // grey
    ChangeGC(NullClient, pGC, GCForeground, &val);
    ValidateGC(&pvfb->pixmap->pixmap->drawable, pGC);

    /* menu bar */
    rect[0].width = pvfb->output_window_width - 1;
    rect[0].height = pvfb->border_top_size - 1;
    rect[0].x = 0;
    rect[0].y = 0;

    /* bottom bar */
    rect[1].width = pvfb->output_window_width - 1;
    rect[1].height = pvfb->border_bottom_size - 1;
    rect[1].x = 0;
    rect[1].y = pvfb->output_window_height - pvfb->border_bottom_size;

    /* left border */
    rect[2].width = pvfb->border_left_size - 1;
    rect[2].height = pvfb->height - 1;
    rect[2].x = 0;
    rect[2].y = pvfb->border_top_size;

    /* right border */
    rect[3].width = pvfb->border_right_size - 1;
    rect[3].height = pvfb->height - 1;
    rect[3].x = pvfb->output_window_width - pvfb->border_right_size;
    rect[3].y = pvfb->border_top_size;

    (*pGC->ops->PolyFillRect)(&(pvfb->pixmap->pixmap->drawable), pGC, 4, rect);

    val.val = 0x00888888; // dark grey
    ChangeGC(NullClient, pGC, GCForeground, &val);
    ValidateGC(&pvfb->pixmap->pixmap->drawable, pGC);

    (*pGC->ops->PolyRectangle)(&(pvfb->pixmap->pixmap->drawable), pGC, 4, rect);

    FreeScratchGC(pGC);
}

static void
vfbCreateOutputWindow(struct xwlnest_screen * pvfb) {
    struct wl_buffer *buffer;
    struct wl_region *region;

    pvfb->surface = wl_compositor_create_surface(pvfb->compositor);
    if (pvfb->surface == NULL) {
        ErrorF("wl_display_create_surface failed\n");
    }

    wl_surface_set_user_data(pvfb->surface, pvfb);

    pvfb->shell_surface =
        wl_shell_get_shell_surface(pvfb->shell, pvfb->surface);
    if (pvfb->shell_surface == NULL) {
        ErrorF("Failed creating shell surface\n");
    }

    wl_shell_surface_add_listener(pvfb->shell_surface,
                                  &shell_surface_listener, pvfb);

    wl_shell_surface_set_toplevel(pvfb->shell_surface);

    region = wl_compositor_create_region(pvfb->compositor);
    if (region == NULL) {
        ErrorF("Failed creating region\n");
    }

    wl_region_add(region, 0, 0, pvfb->output_window_width,
            pvfb->output_window_height);
    wl_surface_set_opaque_region(pvfb->surface, region);
    wl_region_destroy(region);

    pvfb->pixmap = xwlnest_shm_create_pixmap(pvfb->pScreen,
            pvfb->output_window_width, pvfb->output_window_height, pvfb->depth);
    if(pvfb->pixmap == NULL) {
        ErrorF("xwlnest_shm_create_pixmap failed\n");
    }

    xwlnest_paint_window_decoration(pvfb);

    buffer = xwlnest_shm_pixmap_get_wl_buffer(pvfb->shm, pvfb->pixmap);
    if (buffer == NULL) {
        ErrorF("xwlnest_shm_pixmap_get_wl_buffer failed\n");
    }

    wl_surface_attach(pvfb->surface, buffer, 0, 0);
    wl_surface_damage(pvfb->surface, 0, 0, pvfb->output_window_width,
            pvfb->output_window_height);

    pvfb->frame_callback = wl_surface_frame(pvfb->surface);
    wl_callback_add_listener(pvfb->frame_callback, &frame_listener, pvfb);

    wl_surface_commit(pvfb->surface);

    pvfb->has_damage = 1;
    wl_display_flush(pvfb->display);
}

static void
vfbDestroyOutputWindow(struct xwlnest_screen * pvfb) {
    struct xwlnest_seat *xwl_seat, *next_xwl_seat;

    xorg_list_for_each_entry_safe(xwl_seat, next_xwl_seat,
                                  &pvfb->seat_list, link) {
        xorg_list_del(&xwl_seat->link);
        xwl_seat_destroy(xwl_seat);
    }

    wl_surface_attach(pvfb->surface, NULL, 0, 0);
    wl_surface_commit(pvfb->surface);

    wl_shell_surface_destroy(pvfb->shell_surface);
    pvfb->shell_surface = NULL;

    wl_surface_destroy(pvfb->surface);
    pvfb->surface = NULL;

    xwlnest_shm_destroy_pixmap(pvfb->pixmap);
    pvfb->pixmap = NULL;

    wl_display_flush(pvfb->display);

}

static void
registry_global(void *data, struct wl_registry *registry, uint32_t id,
                const char *interface, uint32_t version)
{
    struct xwlnest_screen * pvfb = data;

    LogWrite(0, "xwlnest::registry_global : %s (%d)\n", interface, version);

    if (strcmp(interface, "wl_compositor") == 0) {
        pvfb->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    }
    else if (strcmp(interface, "wl_shm") == 0) {
        pvfb->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    }
    else if (strcmp(interface, "wl_shell") == 0) {
        pvfb->shell =
            wl_registry_bind(registry, id, &wl_shell_interface, 1);
    }
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	LogWrite(0, "xwlnest::global_remove\n");
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    global_remove
};

static void
socket_handler(int fd, int ready, void *data)
{
    struct xwlnest_screen * pvfb = data;
    int ret;

    ret = wl_display_read_events(pvfb->display);
    if (ret == -1)
        FatalError("failed to dispatch Wayland events: %s\n", strerror(errno));

    pvfb->prepare_read = 0;

    ret = wl_display_dispatch_pending(pvfb->display);
    if (ret == -1)
        FatalError("failed to dispatch Wayland events: %s\n", strerror(errno));
}

static void
wakeup_handler(void *data, int err, void *pRead)
{
}

static void
block_handler(void *data, OSTimePtr pTimeout, void *pRead)
{
    struct xwlnest_screen * pvfb = data;
    int ret;

    xwlnest_screen_post_damage(pvfb);

    while (pvfb->prepare_read == 0 &&
           wl_display_prepare_read(pvfb->display) == -1) {
        ret = wl_display_dispatch_pending(pvfb->display);
        if (ret == -1)
            FatalError("failed to dispatch Wayland events: %s\n",
                       strerror(errno));
    }

    pvfb->prepare_read = 1;

    ret = wl_display_flush(pvfb->display);
    if (ret == -1)
        FatalError("failed to write to XWayland fd: %s\n", strerror(errno));
}

static void
damage_report(DamagePtr pDamage, RegionPtr pRegion, void *data)
{
    struct xwlnest_screen * pvfb = data;
    pvfb->has_damage = 1;
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static Bool
xwlnest_realize_window(WindowPtr window) {
    ScreenPtr pScreen = window->drawable.pScreen;
    struct xwlnest_screen * pvfb = &vfbScreens[pScreen->myNum];
    Bool ret;

    /* call this function only once */
    pScreen->RealizeWindow = pvfb->RealizeWindow;
    ret = (*pScreen->RealizeWindow) (window);

    LogWrite(0, "xwlnest_realize_window(%p, %d)\n", window, window->drawable.id);

    /* The first window is the rot window. */
    if (!pvfb->damage) {
        pvfb->damage = DamageCreate(damage_report, damage_destroy,
                DamageReportNonEmpty,
                FALSE, pScreen, pvfb);
        DamageRegister(&window->drawable, pvfb->damage);
        DamageSetReportAfterOp(pvfb->damage, TRUE);

        pvfb->output_pixmap = pvfb->pScreen->GetScreenPixmap(pvfb->pScreen);

        vfbCreateOutputWindow(pvfb);

    }

    return ret;
}

static Bool
vfbScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    struct xwlnest_screen * pvfb = &vfbScreens[pScreen->myNum];
    int dpix = monitorResolution, dpiy = monitorResolution;
    int ret;
    char *pbits;

    LogWrite(0, "vfbScreenInit (%d)\n", pScreen->myNum);

    if (dpix == 0)
        dpix = 100;

    if (dpiy == 0)
        dpiy = 100;

    pvfb->border_top_size = 30;
    pvfb->border_left_size = 10;
    pvfb->border_right_size = 10;
    pvfb->border_bottom_size = 10;

    pvfb->output_window_width = pvfb->border_left_size + pvfb->width +
            pvfb->border_right_size;

    pvfb->output_window_height = pvfb->border_top_size + pvfb->height +
            pvfb->border_bottom_size;

    pvfb->pScreen = pScreen;
    pvfb->paddedBytesWidth = PixmapBytePad(pvfb->width, pvfb->depth);
    pvfb->bitsPerPixel = vfbBitsPerPixel(pvfb->depth);
    if (pvfb->bitsPerPixel >= 8)
        pvfb->paddedWidth = pvfb->paddedBytesWidth / (pvfb->bitsPerPixel / 8);
    else
        pvfb->paddedWidth = pvfb->paddedBytesWidth * 8;

    pvfb->sizeInBytes = pvfb->paddedBytesWidth * pvfb->height;
    pbits = malloc(pvfb->sizeInBytes);

    if (!pbits)
        return FALSE;

    switch (pvfb->depth) {
    case 8:
        miSetVisualTypesAndMasks(8,
                                 ((1 << StaticGray) |
                                  (1 << GrayScale) |
                                  (1 << StaticColor) |
                                  (1 << PseudoColor) |
                                  (1 << TrueColor) |
                                  (1 << DirectColor)), 8, PseudoColor, 0, 0, 0);
        break;
    case 15:
        miSetVisualTypesAndMasks(15,
                                 ((1 << TrueColor) |
                                  (1 << DirectColor)),
                                 8, TrueColor, 0x7c00, 0x03e0, 0x001f);
        break;
    case 16:
        miSetVisualTypesAndMasks(16,
                                 ((1 << TrueColor) |
                                  (1 << DirectColor)),
                                 8, TrueColor, 0xf800, 0x07e0, 0x001f);
        break;
    case 24:
        miSetVisualTypesAndMasks(24,
                                 ((1 << TrueColor) |
                                  (1 << DirectColor)),
                                 8, TrueColor, 0xff0000, 0x00ff00, 0x0000ff);
        break;
    case 30:
        miSetVisualTypesAndMasks(30,
                                 ((1 << TrueColor) |
                                  (1 << DirectColor)),
                                 10, TrueColor, 0x3ff00000, 0x000ffc00,
                                 0x000003ff);
        break;
    default:
        return FALSE;
    }

    miSetPixmapDepths();

    ret = fbScreenInit(pScreen, pbits, pvfb->width, pvfb->height,
                       dpix, dpiy, pvfb->paddedWidth, pvfb->bitsPerPixel);
    if (!ret)
        return FALSE;

    fbPictureInit(pScreen, 0, 0);

    if (!vfbRandRInit(pScreen))
       return FALSE;

    pScreen->SaveScreen = vfbSaveScreen;

    //miDCInitialize(pScreen, &vfbPointerCursorFuncs);

    pScreen->blackPixel = pvfb->blackPixel;
    pScreen->whitePixel = pvfb->whitePixel;

    ret = fbCreateDefColormap(pScreen);

    miSetZeroLineBias(pScreen, pvfb->lineBias);
    
    if (!xwl_screen_init_cursor(pvfb))
        return FALSE;


    pvfb->RealizeWindow = pScreen->RealizeWindow;
    pScreen->RealizeWindow = xwlnest_realize_window;

    pvfb->closeScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = vfbCloseScreen;

    xorg_list_init(&pvfb->seat_list);

    pvfb->display = wl_display_connect(NULL);
    if (pvfb->display == NULL) {
        ErrorF("could not connect to wayland server\n");
        return FALSE;
    }

    pvfb->has_damage = 0;
    pvfb->registry = wl_display_get_registry(pvfb->display);
    wl_registry_add_listener(pvfb->registry,
                             &registry_listener, pvfb);
    ret = wl_display_roundtrip(pvfb->display);
    if (ret == -1) {
        ErrorF("could not connect to wayland server\n");
        return FALSE;
    }

    /* listen to wayland fd */
    pvfb->wayland_fd = wl_display_get_fd(pvfb->display);
    SetNotifyFd(pvfb->wayland_fd, socket_handler, X_NOTIFY_READ, pvfb);
    RegisterBlockAndWakeupHandlers(block_handler, wakeup_handler, pvfb);

    return ret;

}

static const ExtensionModule vfbExtensions[] = {
#ifdef GLXEXT
    { GlxExtensionInit, "GLX", &noGlxExtension },
#endif
#ifdef XF86VIDMODE
    { xwlVidModeExtensionInit, XF86VIDMODENAME, &noXFree86VidModeExtension },
#endif
};

static
void vfbExtensionInit(void)
{
    LoadExtensionList(vfbExtensions, ARRAY_SIZE(vfbExtensions), TRUE);
}

void
InitOutput(ScreenInfo * screen_info, int argc, char **argv)
{
    int i;
    int depths[] = { 1, 4, 8, 15, 16, 24, 32 };
    int bpp[] =    { 1, 8, 8, 16, 16, 32, 32 };

    if (serverGeneration == 1)
        vfbExtensionInit();

    for (i = 0; i < ARRAY_SIZE(depths); i++) {
        screen_info->formats[i].depth = depths[i];
        screen_info->formats[i].bitsPerPixel = bpp[i];
        screen_info->formats[i].scanlinePad = BITMAP_SCANLINE_PAD;
    }

    screen_info->imageByteOrder = IMAGE_BYTE_ORDER;
    screen_info->bitmapScanlineUnit = BITMAP_SCANLINE_UNIT;
    screen_info->bitmapScanlinePad = BITMAP_SCANLINE_PAD;
    screen_info->bitmapBitOrder = BITMAP_BIT_ORDER;
    screen_info->numPixmapFormats = ARRAY_SIZE(depths);

    /* initialize screens */

    if (vfbNumScreens < 1) {
        vfbScreens = &defaultScreenInfo;
        vfbNumScreens = 1;
    }
    for (i = 0; i < vfbNumScreens; i++) {
        if (-1 == AddScreen(vfbScreenInit, argc, argv)) {
            FatalError("Couldn't add screen %d", i);
        }
    }

}                               /* end InitOutput */
