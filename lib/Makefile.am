### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

aouhdrdir = $(includedir)/aou
aouhdr_HEADERS =

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
lib_LTLIBRARIES =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST =

lib_LTLIBRARIES += libumpf.la
aouhdr_HEADERS += umpf.h
noinst_HEADERS += umpf-private.h
noinst_HEADERS += epoll-helpers.h
noinst_HEADERS += ud-sock.h
libumpf_la_SOURCES =
EXTRA_libumpf_la_SOURCES =
libumpf_la_SOURCES += proto-fixml.c
libumpf_la_SOURCES += print-fixml.c
libumpf_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBXML2_CFLAGS) -I$(top_srcdir)/srv
libumpf_la_LDFLAGS = $(AM_LDFLAGS) $(LIBXML2_LIBS)
BUILT_SOURCES += proto-fixml-tag.c proto-fixml-attr.c
BUILT_SOURCES += proto-fixml-ns.c
EXTRA_libumpf_la_SOURCES += proto-fixml-tag.gperf proto-fixml-attr.gperf
EXTRA_libumpf_la_SOURCES += proto-fixml-ns.gperf
EXTRA_libumpf_la_SOURCES += $(BUILT_SOURCES)

noinst_PROGRAMS += testlib
testlib_LDFLAGS = libumpf.la

## serves documentation purposes
EXTRA_DIST += example-msg-01.xml
EXTRA_DIST += example-msg-02.xml
EXTRA_DIST += example-msg-03.xml

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
