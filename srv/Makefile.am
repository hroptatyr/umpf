### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

moddir = $(libdir)/unserding

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
mod_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

if BUILD_SERVER
mod_LTLIBRARIES += dso-umpf.la
dso_umpf_la_SOURCES =
EXTRA_dso_umpf_la_SOURCES =
dso_umpf_la_LDFLAGS = -module
dso_umpf_la_LDFLAGS += $(unserding_LDFLAGS) $(MYSQL_LIBS) $(LIBXML2_LIBS)
dso_umpf_la_LDFLAGS += $(top_builddir)/lib/libumpf.la
dso_umpf_la_CPPFLAGS = $(AM_CPPFLAGS) $(unserding_CFLAGS)
dso_umpf_la_CPPFLAGS += -I$(top_srcdir)/lib
dso_umpf_la_CPPFLAGS += $(MYSQL_CPPFLAGS) $(LIBXML2_CFLAGS)
dso_umpf_la_SOURCES += dso-umpf.c
EXTRA_dso_umpf_la_SOURCES += con6ity.c con6ity.h
dso_umpf_la_CPPFLAGS += -DHARD_INCLUDE_con6ity
noinst_HEADERS += nifty.h
endif


# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
