### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

headerdir = $(includedir)/unserding
moddir = $(libdir)/unserding

bin_PROGRAMS =
noinst_PROGRAMS =
mod_LTLIBRARIES =

if BUILD_SERVER
mod_LTLIBRARIES += dso-pfd.la
dso_pfd_la_LDFLAGS = -module
dso_pfd_la_LDFLAGS += $(unserding_LDFLAGS) $(MYSQL_LIBS) $(LIBXML2_LIBS)
dso_pfd_la_CPPFLAGS = $(AM_CPPFLAGS) $(unserding_CFLAGS)
dso_pfd_la_CPPFLAGS += $(MYSQL_CPPFLAGS) $(LIBXML2_CFLAGS)
dso_pfd_la_SOURCES = dso-pfd.c
endif

BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
