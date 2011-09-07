INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = backlight.la

moduledir = $(libdir)/lxpanel/plugins

backlight_la_SOURCES = backlight.c

backlight_la_LIBADD = -lXrender $(PACKAGE_LIBS)

backlight_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
