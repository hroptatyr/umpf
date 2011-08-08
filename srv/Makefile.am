### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D__USE_XOPEN2K8
AM_LDFLAGS =

bin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
BUILT_SOURCES =
CLEANFILES = $(BUILT_SOURCES)

if BUILD_SERVER
moddir = $(libdir)/unserding
mod_LTLIBRARIES = dso-umpf.la
dso_umpf_la_SOURCES =
EXTRA_dso_umpf_la_SOURCES =
dso_umpf_la_LDFLAGS = -module
dso_umpf_la_LDFLAGS += $(unserding_LDFLAGS) $(LIBXML2_LIBS)
dso_umpf_la_LDFLAGS += $(top_builddir)/lib/.libs/libumpf.a
dso_umpf_la_CPPFLAGS = $(AM_CPPFLAGS) $(unserding_CFLAGS)
dso_umpf_la_CPPFLAGS += -I$(top_srcdir)/lib
dso_umpf_la_CPPFLAGS += $(LIBXML2_CFLAGS)
dso_umpf_la_SOURCES += dso-umpf.c
EXTRA_dso_umpf_la_SOURCES += be-sql.c be-sql.h
noinst_HEADERS += nifty.h
## database goodness
if USE_MYSQL
dso_umpf_la_LDFLAGS += $(MYSQL_LDFLAGS)
dso_umpf_la_CPPFLAGS +=  $(MYSQL_CPPFLAGS)
endif
if USE_SQLITE
dso_umpf_la_LDFLAGS += $(sqlite_LIBS)
dso_umpf_la_CPPFLAGS +=  $(sqlite_CFLAGS)
endif
dso_umpf_la_CPPFLAGS += -DHARD_INCLUDE_be_sql
endif ## BUILD_SERVER


# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
