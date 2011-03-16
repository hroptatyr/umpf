### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

if BUILD_CLIAPPS
bin_PROGRAMS += umpf
umpf_SOURCES =
umpf_SOURCES += umpf.c
umpf_SOURCES += epoll-helpers.h
umpf_SOURCES += ud-sock.h
umpf_LDFLAGS = -static
umpf_LDADD = $(top_builddir)/lib/libumpf.la
umpf_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/lib
endif ## BUILD_CLIAPPS


# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
