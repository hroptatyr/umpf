### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

aouhdrdir = $(includedir)/aou
aouhdr_HEADERS =

bin_PROGRAMS =
noinst_PROGRAMS =
lib_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

lib_LTLIBRARIES += libaou-pfd.la
aouhdr_HEADERS += pfd.h
libaou_pfd_la_SOURCES =
libaou_pfd_la_SOURCES += proto-fixml.c
libaou_pfd_la_SOURCES += print-fixml.c
libaou_pfd_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBXML2_CFLAGS) -I$(top_srcdir)/srv
libaou_pfd_la_LDFLAGS = $(AM_LDFLAGS) $(LIBXML2_LIBS)
BUILT_SOURCES += proto-fixml-tag.c proto-fixml-attr.c

noinst_PROGRAMS += testlib
testlib_LDFLAGS = libaou-pfd.la

## our rule for gperf
%.c: %.gperf
	gperf $< --output-file $@

## Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
%.i: %.c
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
%.s: %.c
	$(COMPILE) -S -c $(AM_CFLAGS) $<

## Makefile.am ends here
