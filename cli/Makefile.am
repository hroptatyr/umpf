### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
BUILT_SOURCES =
GGO_HELPERS =
EXTRA_DIST = $(BUILT_SOURCES) $(GGO_HELPERS)

if BUILD_CLIAPPS
bin_PROGRAMS += umpf
umpf_SOURCES =
umpf_SOURCES += umpf.c
umpf_LDFLAGS = -static
umpf_LDADD = $(top_builddir)/lib/libumpf.la
umpf_LDADD += -lm
umpf_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/lib

bin_PROGRAMS += umpp
umpp_SOURCES = umpp.c
umpp_SOURCES += umpp-clo.ggo
umpp_SOURCES += umpp-meld-clo.ggo umpp-meld-clo.c
umpp_LDFLAGS = -static
umpp_LDADD = -lm
umpp_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/lib
BUILT_SOURCES += umpp-clo.c umpp-clo.h
BUILT_SOURCES += umpp-meld-clo.c umpp-meld-clo.h
endif ## BUILD_CLIAPPS

GGO_HELPERS += __gengetopt-no-failure.sed

## ggo rule
%.c %.h: %.ggo $(GGO_HELPERS)
	gengetopt -l -F $* -i $<
## patch the getopt error handling
	sed -i -f $(srcdir)/__gengetopt-no-failure.sed -- $*.c

# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
