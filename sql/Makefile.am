## generate mysql and sqlite installer and uninstaller

M4=m4
if BUILD_SERVER
umpfdir = $(datadir)/umpf

BUILT_SOURCES = install-mysql.sql install-sqlite3.sql
umpf_DATA = install-mysql.sql install-sqlite3.sql

install-mysql.sql: mysql.m4 uninstall.sql.in install.sql.in
	$(M4) $^ > $@

install-sqlite3.sql: sqlite3.m4 uninstall.sql.in install.sql.in
	$(M4) $^ > $@
endif

## Makefile.am ends here
