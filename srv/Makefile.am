### Makefile.am

AM_CPPFLAGS = -D_GNU_SOURCE -D_POSIX_C_SOURCE=201001L -D_XOPEN_SOURCE=600
AM_LDFLAGS =

bin_PROGRAMS =
sbin_PROGRAMS =
noinst_PROGRAMS =
noinst_HEADERS =
BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)
CLEANFILES =

if BUILD_SERVER
sbin_PROGRAMS += umpfd
umpfd_SOURCES = umpfd.c
umpfd_SOURCES += umpfd-clo.ggo
BUILT_SOURCES += umpfd-clo.c umpfd-clo.h
EXTRA_umpfd_SOURCES =
umpfd_SOURCES += logger.h
umpfd_SOURCES += configger.c configger.h
umpfd_CPPFLAGS = $(AM_CPPFLAGS)
umpfd_LDFLAGS = $(AM_LDFLAGS)
umpfd_CPPFLAGS += -I$(top_srcdir)/lib
umpfd_LDFLAGS += $(top_builddir)/lib/.libs/libumpf.a
umpfd_CPPFLAGS += $(LIBXML2_CFLAGS)
umpfd_LDFLAGS += $(LIBXML2_LIBS)
umpfd_CPPFLAGS += $(libev_CFLAGS)
umpfd_LDFLAGS += $(libev_LIBS)
EXTRA_umpfd_SOURCES += be-sql.c be-sql.h
EXTRA_umpfd_SOURCES += gq.c gq.h
if HAVE_LUA
umpfd_SOURCES += lua-config.c lua-config.h
umpfd_CPPFLAGS += -DUSE_LUA $(lua_CFLAGS)
umpfd_LDFLAGS += $(lua_LIBS)
endif  HAVE_LUA
## database goodness
if USE_MYSQL
umpfd_LDFLAGS += $(MYSQL_LDFLAGS)
umpfd_CPPFLAGS +=  $(MYSQL_CPPFLAGS)
endif  ## USE_MYSQL
if USE_SQLITE
umpfd_LDFLAGS += $(sqlite_LIBS)
umpfd_CPPFLAGS +=  $(sqlite_CFLAGS)
endif  ## USE_SQLITE
umpfd_CPPFLAGS += -DHARD_INCLUDE_be_sql
endif  ## BUILD_SERVER


## our rule for gengetopt
%.c %.h: %.ggo
	$(AM_V_GEN) gengetopt -l -i $< -F $*

# 
# Help the developers get nice post-processed source files

## Create preprocessor output (debugging purposes only)
.c.i:
	$(COMPILE) -E -o $@ $<

## Create assembler output (debugging purposes only)
.c.s:
	$(COMPILE) -S -c $(AM_CFLAGS) $<
