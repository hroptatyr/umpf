## generate mysql and sqlite installer and uninstaller

M4=m4

if BUILD_SERVER
umpfdir = $(datadir)/umpf
dist_umpf_DATA = $(SQL_OUT)
endif

EXTRA_DIST =
EXTRA_DIST += mysql.m4
EXTRA_DIST += sqlite3.m4
EXTRA_DIST += $(SQL_TMPL)

SQL_TMPL = uninstall.sql.in install.sql.in
SQL_OUT = install-mysql.sql install-sqlite3.sql

install-mysql.sql: mysql.m4 $(SQL_TMPL)
	$(M4) $^ > $@

install-sqlite3.sql: sqlite3.m4 $(SQL_TMPL)
	$(M4) $^ > $@

## Makefile.am ends here
