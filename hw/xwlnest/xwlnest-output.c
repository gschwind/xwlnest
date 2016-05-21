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

int vfbNumScreens = 0;
vfbScreenInfo *vfbScreens = NULL;

//static DevPrivateKeyRec vfb_root_window_private_key;

static vfbScreenInfo defaultScreenInfo = {
    .width = VFB_DEFAULT_WIDTH,
    .height = VFB_DEFAULT_HEIGHT,
    .depth = VFB_DEFAULT_DEPTH,
    .blackPixel = VFB_DEFAULT_BLACKPIXEL,
    .whitePixel = VFB_DEFAULT_WHITEPIXEL,
    .lineBias = VFB_DEFAULT_LINEBIAS,
};

typedef enum { NORMAL_MEMORY_FB } fbMemType;
static fbMemType fbmemtype = NORMAL_MEMORY_FB;
static char needswap = 0;

#define swapcopy16(_dst, _src) \
    if (needswap) { CARD16 _s = _src; cpswaps(_s, _dst); } \
    else _dst = _src;

#define swapcopy32(_dst, _src) \
    if (needswap) { CARD32 _s = _src; cpswapl(_s, _dst); } \
    else _dst = _src;

static void
vfbDestroyOutputWindow(vfbScreenInfoPtr pvfb);

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
    int i;

    /* clean up the framebuffers */

    switch (fbmemtype) {

    case NORMAL_MEMORY_FB:
        for (i = 0; i < vfbNumScreens; i++) {
            free(vfbScreens[i].pXWDHeader);
        }
        break;
    }
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

#ifdef HAS_SHM
    ErrorF("-shmem                 put framebuffers in shared memory\n");
#endif
}

int
ddxProcessArgument(int argc, char *argv[], int i)
{
    static Bool firstTime = TRUE;
    static int lastScreen = -1;
    vfbScreenInfo *currentScreen;

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

static void
vfbInstallColormap(ColormapPtr pmap)
{
    ColormapPtr oldpmap = GetInstalledmiColormap(pmap->pScreen);

    if (pmap != oldpmap) {
        int entries;
        XWDFileHeader *pXWDHeader;
        VisualPtr pVisual;
        Pixel *ppix;
        xrgb *prgb;
        xColorItem *defs;
        int i;

        miInstallColormap(pmap);

        entries = pmap->pVisual->ColormapEntries;
        pXWDHeader = vfbScreens[pmap->pScreen->myNum].pXWDHeader;
        pVisual = pmap->pVisual;

        swapcopy32(pXWDHeader->visual_class, pVisual->class);
        swapcopy32(pXWDHeader->red_mask, pVisual->redMask);
        swapcopy32(pXWDHeader->green_mask, pVisual->greenMask);
        swapcopy32(pXWDHeader->blue_mask, pVisual->blueMask);
        swapcopy32(pXWDHeader->bits_per_rgb, pVisual->bitsPerRGBValue);
        swapcopy32(pXWDHeader->colormap_entries, pVisual->ColormapEntries);

        ppix = xallocarray(entries, sizeof(Pixel));
        prgb = xallocarray(entries, sizeof(xrgb));
        defs = xallocarray(entries, sizeof(xColorItem));

        for (i = 0; i < entries; i++)
            ppix[i] = i;
        /* XXX truecolor */
        QueryColors(pmap, entries, ppix, prgb, serverClient);

        for (i = 0; i < entries; i++) { /* convert xrgbs to xColorItems */
            defs[i].pixel = ppix[i] & 0xff;     /* change pixel to index */
            defs[i].red = prgb[i].red;
            defs[i].green = prgb[i].green;
            defs[i].blue = prgb[i].blue;
            defs[i].flags = DoRed | DoGreen | DoBlue;
        }
        (*pmap->pScreen->StoreColors) (pmap, entries, defs);

        free(ppix);
        free(prgb);
        free(defs);
    }
}

static void
vfbStoreColors(ColormapPtr pmap, int ndef, xColorItem * pdefs)
{
    XWDColor *pXWDCmap;
    int i;

    if (pmap != GetInstalledmiColormap(pmap->pScreen)) {
        return;
    }

    pXWDCmap = vfbScreens[pmap->pScreen->myNum].pXWDCmap;

    if ((pmap->pVisual->class | DynamicClass) == DirectColor) {
        return;
    }

    for (i = 0; i < ndef; i++) {
        if (pdefs[i].flags & DoRed) {
            swapcopy16(pXWDCmap[pdefs[i].pixel].red, pdefs[i].red);
        }
        if (pdefs[i].flags & DoGreen) {
            swapcopy16(pXWDCmap[pdefs[i].pixel].green, pdefs[i].green);
        }
        if (pdefs[i].flags & DoBlue) {
            swapcopy16(pXWDCmap[pdefs[i].pixel].blue, pdefs[i].blue);
        }
    }
}

static Bool
vfbSaveScreen(ScreenPtr pScreen, int on)
{
    return TRUE;
}

static char *
vfbAllocateFramebufferMemory(vfbScreenInfoPtr pvfb)
{
    if (pvfb->pfbMemory)
        return pvfb->pfbMemory; /* already done */

    pvfb->sizeInBytes = pvfb->paddedBytesWidth * pvfb->height;

    /* Calculate how many entries in colormap.  This is rather bogus, because
     * the visuals haven't even been set up yet, but we need to know because we
     * have to allocate space in the file for the colormap.  The number 10
     * below comes from the MAX_PSEUDO_DEPTH define in cfbcmap.c.
     */

    if (pvfb->depth <= 10) {    /* single index colormaps */
        pvfb->ncolors = 1 << pvfb->depth;
    }
    else {                      /* decomposed colormaps */
        int nplanes_per_color_component = pvfb->depth / 3;

        if (pvfb->depth % 3)
            nplanes_per_color_component++;
        pvfb->ncolors = 1 << nplanes_per_color_component;
    }

    /* add extra bytes for XWDFileHeader, window name, and colormap */

    pvfb->sizeInBytes += SIZEOF(XWDheader) + XWD_WINDOW_NAME_LEN +
        pvfb->ncolors * SIZEOF(XWDColor);

    pvfb->pXWDHeader = NULL;
    switch (fbmemtype) {
    case NORMAL_MEMORY_FB:
        pvfb->pXWDHeader = (XWDFileHeader *) malloc(pvfb->sizeInBytes);
        break;
    }

    if (pvfb->pXWDHeader) {
        pvfb->pXWDCmap = (XWDColor *) ((char *) pvfb->pXWDHeader
                                       + SIZEOF(XWDheader) +
                                       XWD_WINDOW_NAME_LEN);
        pvfb->pfbMemory = (char *) (pvfb->pXWDCmap + pvfb->ncolors);

        return pvfb->pfbMemory;
    }
    else
        return NULL;
}

static void
vfbWriteXWDFileHeader(ScreenPtr pScreen)
{
    vfbScreenInfoPtr pvfb = &vfbScreens[pScreen->myNum];
    XWDFileHeader *pXWDHeader = pvfb->pXWDHeader;
    char hostname[XWD_WINDOW_NAME_LEN];
    unsigned long swaptest = 1;
    int i;

    needswap = *(char *) &swaptest;

    pXWDHeader->header_size =
        (char *) pvfb->pXWDCmap - (char *) pvfb->pXWDHeader;
    pXWDHeader->file_version = XWD_FILE_VERSION;

    pXWDHeader->pixmap_format = ZPixmap;
    pXWDHeader->pixmap_depth = pvfb->depth;
    pXWDHeader->pixmap_height = pXWDHeader->window_height = pvfb->height;
    pXWDHeader->xoffset = 0;
    pXWDHeader->byte_order = IMAGE_BYTE_ORDER;
    pXWDHeader->bitmap_bit_order = BITMAP_BIT_ORDER;
#ifndef INTERNAL_VS_EXTERNAL_PADDING
    pXWDHeader->pixmap_width = pXWDHeader->window_width = pvfb->width;
    pXWDHeader->bitmap_unit = BITMAP_SCANLINE_UNIT;
    pXWDHeader->bitmap_pad = BITMAP_SCANLINE_PAD;
#else
    pXWDHeader->pixmap_width = pXWDHeader->window_width = pvfb->paddedWidth;
    pXWDHeader->bitmap_unit = BITMAP_SCANLINE_UNIT_PROTO;
    pXWDHeader->bitmap_pad = BITMAP_SCANLINE_PAD_PROTO;
#endif
    pXWDHeader->bits_per_pixel = pvfb->bitsPerPixel;
    pXWDHeader->bytes_per_line = pvfb->paddedBytesWidth;
    pXWDHeader->ncolors = pvfb->ncolors;

    /* visual related fields are written when colormap is installed */

    pXWDHeader->window_x = pXWDHeader->window_y = 0;
    pXWDHeader->window_bdrwidth = 0;

    /* write xwd "window" name: Xvfb hostname:server.screen */

    if (-1 == gethostname(hostname, sizeof(hostname)))
        hostname[0] = 0;
    else
        hostname[XWD_WINDOW_NAME_LEN - 1] = 0;
    sprintf((char *) (pXWDHeader + 1), "Xvfb %s:%s.%d", hostname, display,
            pScreen->myNum);

    /* write colormap pixel slot values */

    for (i = 0; i < pvfb->ncolors; i++) {
        pvfb->pXWDCmap[i].pixel = i;
    }

    /* byte swap to most significant byte first */

    if (needswap) {
        SwapLongs((CARD32 *) pXWDHeader, SIZEOF(XWDheader) / 4);
        for (i = 0; i < pvfb->ncolors; i++) {
            swapl(&pvfb->pXWDCmap[i].pixel);
        }
    }
}

static Bool
vfbCursorOffScreen(ScreenPtr *ppScreen, int *x, int *y)
{
    return FALSE;
}

static void
vfbCrossScreen(ScreenPtr pScreen, Bool entering)
{
}

static miPointerScreenFuncRec vfbPointerCursorFuncs = {
    vfbCursorOffScreen,
    vfbCrossScreen,
    miPointerWarpCursor
};

static Bool
vfbCloseScreen(ScreenPtr pScreen)
{
    vfbScreenInfoPtr pvfb = &vfbScreens[pScreen->myNum];

    RemoveNotifyFd(pvfb->wayland_fd);

    if(pvfb->damage) {
        //DamageDestroy(pvfb->damage);
        pvfb->damage = NULL;
    }

    if(pvfb->pixmap) {
        vfbDestroyOutputWindow(pvfb);
    }

    wl_display_disconnect(pvfb->display);

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
    vfbScreenInfoPtr pvfb = data;
    LogWrite(0, "xwlnest::frame_callback(%p, %p, %u)\n", data, callback, time);
    pvfb->frame_callback = NULL;
}

static const struct wl_callback_listener frame_listener = {
    frame_callback
};

static void
xwlnest_screen_post_damage(vfbScreenInfoPtr pvfb)
{
    RegionPtr region;
    struct wl_buffer *buffer;

    LogWrite(0, "xwlnest::screen_post_damage\n");

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

                LogWrite(0, "damage %d %d %d %d\n", x, y, w, h);

                wl_surface_damage(pvfb->surface, x, y, w, h);
                (*pGC->ops->CopyArea)(&pvfb->output_pixmap->drawable,
                        &pvfb->pixmap->pixmap->drawable, pGC, x, y, w, h, x, y);

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
vfbCreateOutputWindow(vfbScreenInfoPtr pvfb) {
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

    wl_region_add(region, 0, 0, pvfb->width, pvfb->height);
    wl_surface_set_opaque_region(pvfb->surface, region);
    wl_region_destroy(region);

    pvfb->pixmap = xwlnest_shm_create_pixmap(pvfb->pScreen, pvfb->width, pvfb->height, pvfb->depth);
    if(pvfb->pixmap == NULL) {
        ErrorF("xwlnest_shm_create_pixmap failed\n");
    }

    buffer = xwlnest_shm_pixmap_get_wl_buffer(pvfb->shm, pvfb->pixmap);
    if (buffer == NULL) {
        ErrorF("xwlnest_shm_pixmap_get_wl_buffer failed\n");
    }

    wl_surface_attach(pvfb->surface, buffer, 0, 0);
    wl_surface_damage(pvfb->surface, 0, 0, pvfb->width, pvfb->height);

    pvfb->frame_callback = wl_surface_frame(pvfb->surface);
    wl_callback_add_listener(pvfb->frame_callback, &frame_listener, pvfb);

    wl_surface_commit(pvfb->surface);

    pvfb->has_damage = 1;
    wl_display_flush(pvfb->display);
}

static void
vfbDestroyOutputWindow(vfbScreenInfoPtr pvfb) {
    struct wl_buffer *buffer;
    struct wl_region *region;

    struct xwl_seat *xwl_seat, *next_xwl_seat;

    xorg_list_for_each_entry_safe(xwl_seat, next_xwl_seat,
                                  &pvfb->seat_list, link) {
        xorg_list_del(&xwl_seat->link);
        xwl_seat_destroy(xwl_seat);
    }

    wl_surface_attach(pvfb->surface, buffer, 0, 0);
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
    vfbScreenInfoPtr pvfb = data;

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

//    else if (strcmp(interface, "wl_output") == 0 && version >= 2) {
//        if (xwl_output_create(xwl_screen, id))
//            xwl_screen->expecting_event++;
//    }
//#ifdef GLAMOR_HAS_GBM
//    else if (xwl_screen->glamor &&
//             strcmp(interface, "wl_drm") == 0 && version >= 2) {
//        xwl_screen_init_glamor(xwl_screen, id, version);
//    }
//#endif
}

static void
global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
	LogWrite(0, "xwlnest::global_remove\n");
//    struct xwl_screen *xwl_screen = data;
//    struct xwl_output *xwl_output, *tmp_xwl_output;
//
//    xorg_list_for_each_entry_safe(xwl_output, tmp_xwl_output,
//                                  &xwl_screen->output_list, link) {
//        if (xwl_output->server_output_id == name) {
//            xwl_output_destroy(xwl_output);
//            break;
//        }
//    }
}

static const struct wl_registry_listener registry_listener = {
    registry_global,
    global_remove
};

static void
socket_handler(int fd, int ready, void *data)
{
    vfbScreenInfoPtr pvfb = data;
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
    vfbScreenInfoPtr pvfb = data;
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
    vfbScreenInfoPtr pvfb = data;
    pvfb->has_damage = 1;
}

static void
damage_destroy(DamagePtr pDamage, void *data)
{
}

static Bool
xwlnest_realize_window(WindowPtr window) {
    ScreenPtr pScreen = window->drawable.pScreen;
    vfbScreenInfoPtr pvfb = &vfbScreens[pScreen->myNum];
    Bool ret;

    pScreen->RealizeWindow = pvfb->RealizeWindow;
    ret = (*pScreen->RealizeWindow) (window);
    pvfb->RealizeWindow = pScreen->RealizeWindow;
    pScreen->RealizeWindow = xwlnest_realize_window;

    LogWrite(0, "xwlnest_realize_window(%p, %d)\n", window->drawable.id);

    /* The first window is the rot window. */
    if (!pvfb->damage) {
        pvfb->damage = DamageCreate(damage_report, damage_destroy,
                DamageReportNonEmpty,
                FALSE, pScreen, pvfb);
        DamageRegister(&window->drawable, pvfb->damage);
        DamageSetReportAfterOp(pvfb->damage, TRUE);

        pvfb->output_pixmap = pvfb->pScreen->GetScreenPixmap(pvfb->pScreen);

        (*pvfb->pScreen->ModifyPixmapHeader) (pvfb->output_pixmap, pvfb->width,
                pvfb->height, pvfb->depth, BitsPerPixel(pvfb->depth),
                pvfb->paddedBytesWidth, pvfb->pfbMemory);

        vfbCreateOutputWindow(pvfb);

    }

    return ret;
}


static Bool
xwlnest_unrealize_window(WindowPtr window) {
    ScreenPtr pScreen = window->drawable.pScreen;
    vfbScreenInfoPtr pvfb = &vfbScreens[pScreen->myNum];
    Bool ret;

    pScreen->UnrealizeWindow = pvfb->UnrealizeWindow;
    ret = (*pScreen->UnrealizeWindow) (window);
    pvfb->UnrealizeWindow = pScreen->UnrealizeWindow;
    pScreen->UnrealizeWindow = xwlnest_unrealize_window;

    LogWrite(0, "xwlnest_unrealize_window(%p, %d)\n", window->drawable.id);

    return ret;
}

static Bool
vfbScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    vfbScreenInfoPtr pvfb = &vfbScreens[pScreen->myNum];
    int dpix = monitorResolution, dpiy = monitorResolution;
    int ret;
    char *pbits;
    WindowPtr window;

    if (dpix == 0)
        dpix = 100;

    if (dpiy == 0)
        dpiy = 100;

    pvfb->paddedBytesWidth = PixmapBytePad(pvfb->width, pvfb->depth);
    pvfb->bitsPerPixel = vfbBitsPerPixel(pvfb->depth);
    if (pvfb->bitsPerPixel >= 8)
        pvfb->paddedWidth = pvfb->paddedBytesWidth / (pvfb->bitsPerPixel / 8);
    else
        pvfb->paddedWidth = pvfb->paddedBytesWidth * 8;
    pbits = vfbAllocateFramebufferMemory(pvfb);
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

    pScreen->InstallColormap = vfbInstallColormap;

    pScreen->SaveScreen = vfbSaveScreen;
    pScreen->StoreColors = vfbStoreColors;

    miDCInitialize(pScreen, &vfbPointerCursorFuncs);

    vfbWriteXWDFileHeader(pScreen);

    pScreen->blackPixel = pvfb->blackPixel;
    pScreen->whitePixel = pvfb->whitePixel;

    ret = fbCreateDefColormap(pScreen);

    miSetZeroLineBias(pScreen, pvfb->lineBias);

    pvfb->pScreen = pScreen;

    pvfb->RealizeWindow = pScreen->RealizeWindow;
    pScreen->RealizeWindow = xwlnest_realize_window;

    pvfb->UnrealizeWindow = pScreen->UnrealizeWindow;
    pScreen->UnrealizeWindow = xwlnest_unrealize_window;

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

    //dixSetPrivate(&pScreen->root->devPrivates, &vfb_root_window_private_key, pvfb);

    return ret;

}                               /* end vfbScreenInit */

static const ExtensionModule vfbExtensions[] = {
#ifdef GLXEXT
    { GlxExtensionInit, "GLX", &noGlxExtension },
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
