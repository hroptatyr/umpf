## generate mysql and sqlite installer and uninstaller

M4=m4
if BUILD_SERVER
umpfdir = $(datadir)/umpf

BUILT_SOURCES =
EXTRA_DIST = $(BUILT_SOURCES)
EXTRA_DIST += mysql.m4
EXTRA_DIST += sqlite3.m4

SQL_TMPL = uninstall.sql.in install.sql.in
EXTRA_DIST += $(SQL_TMPL)

umpf_DATA = install-mysql.sql install-sqlite3.sql
BUILT_SOURCES += $(umpf_DATA)

install-mysql.sql: mysql.m4 $(SQL_TMPL)
	$(M4) $^ > $@

install-sqlite3.sql: sqlite3.m4 $(SQL_TMPL)
	$(M4) $^ > $@
endif

## Makefile.am ends here
