SUBDIRS = man

bin_PROGRAMS = Xwlnest

AM_CFLAGS = -DHAVE_DIX_CONFIG_H \
            $(XWLNESTMODULES_CFLAGS) \
	    $(DIX_CFLAGS)

SRCS =	xwlnest-output.c \
	xwlnest-shm.c \
	xwlnest-input.c \
	xwlnest-cursor.c \
	$(top_srcdir)/mi/miinitext.c

Xwlnest_SOURCES = $(SRCS)

XWLNEST_LIBS = \
        @XWLNEST_LIBS@ \
	$(MAIN_LIB) \
	$(XSERVER_LIBS) \
	$(top_builddir)/Xext/libXextdpmsstubs.la \
	$(top_builddir)/Xi/libXistubs.la

Xwlnest_LDADD = $(XWLNEST_LIBS) $(XWLNEST_SYS_LIBS) $(XSERVER_SYS_LIBS)
Xwlnest_DEPENDENCIES = $(XWLNEST_LIBS)
Xwlnest_LDFLAGS = $(LD_EXPORT_SYMBOLS_FLAG)

relink:
	$(AM_V_at)rm -f Xwlnest$(EXEEXT) && $(MAKE) Xwlnest$(EXEEXT)

%-protocol.c : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) code < $< > $@

%-client-protocol.h : %.xml
	$(AM_V_GEN)$(WAYLAND_SCANNER) client-header < $< > $@
