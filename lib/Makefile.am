### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8 -fPIC
AM_LDFLAGS =

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
lib_LTLIBRARIES =
BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)

if BUILD_LIBRARY
lib_LTLIBRARIES += libumpf.la
aouhdrdir = $(includedir)/aou
aouhdr_HEADERS = umpf.h
else
noinst_LTLIBRARIES = libumpf.la
endif
noinst_HEADERS += nifty.h
noinst_HEADERS += umpf-private.h
noinst_HEADERS += epoll-helpers.h
noinst_HEADERS += ud-sock.h
noinst_HEADERS += ud-sockaddr.h
libumpf_la_SOURCES =
EXTRA_libumpf_la_SOURCES =
libumpf_la_SOURCES += umpf.c umpf.h
libumpf_la_SOURCES += proto-fixml.c proto-fixml.h proto-fixml-tag.h
libumpf_la_SOURCES += umpf-msg-glue-fixml.c
libumpf_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBXML2_CFLAGS)
libumpf_la_LDFLAGS = $(AM_LDFLAGS) $(LIBXML2_LIBS)
libumpf_la_LDFLAGS += -version-info 1:0:0
BUILT_SOURCES += proto-fixml-tag.c proto-fixml-attr.c
BUILT_SOURCES += proto-fixml-ns.c
EXTRA_libumpf_la_SOURCES += proto-fixml-tag.gperf proto-fixml-attr.gperf
EXTRA_libumpf_la_SOURCES += proto-fixml-ns.gperf
EXTRA_libumpf_la_SOURCES += $(BUILT_SOURCES)

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
