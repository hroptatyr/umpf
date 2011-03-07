/*** be-sql.c -- SQL backend for pfd
 *
 * Copyright (C) 2011 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <sebastian.freundt@ga-group.nl>
 *
 * This file is part of the army of unserding daemons.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the author nor the names of any contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ***/

#if defined HAVE_CONFIG_H
# include "config.h"
#endif	/* HAVE_CONFIG_H */
#if defined WITH_MYSQL
# include <mysql/mysql.h>
#endif	/* WITH_MYSQL */
#if defined WITH_SQLITE
# include <sqlite3.h>
#endif	/* WITH_SQLITE */
#include "nifty.h"
#include "be-sql.h"

#define BE_SQL		"mod/umpf/sql"

typedef enum {
	BE_SQL_UNK,
	BE_SQL_MYSQL,
	BE_SQL_SQLITE,
} be_sql_type_t;

static inline dbconn_t
be_sql_set_type(void *conn, be_sql_type_t type)
{
	return (void*)((long int)conn | type);
}

static inline be_sql_type_t
be_sql_get_type(dbconn_t conn)
{
	/* we abuse the last 3 bits of the conn pointer */
	return (be_sql_type_t)((long int)conn & 0x07);
}

static inline void*
be_sql_get_conn(dbconn_t conn)
{
	/* we abuse the last 3 bits of the conn pointer */
	return (void*)((long int)conn & ~0x07);
}

static char gbuf[4096];


/* sql helpers, quoted string copiers et al. */
static char*
stpncpy_q(char *tgt, const char *src, size_t max)
{
	static const char stpset[] = "'\\";
	char *res = tgt;

	for (size_t i; i = strcspn(src, stpset); src += i + sizeof(*src)) {
		/* write what we've got */
		res = stpncpy(res, src, i);
		/* no need to inspect the character, just quote him */
		if (i >= max || src[i] == '\0') {
			/* bingo, we're finished or stumbled upon a
			 * dickhead of UTF-8 character */
			break;
		}
		*res++ = '\\';
		*res++ = src[i];
	}
	return res;
}


#if defined WITH_MYSQL
static dbconn_t
be_mysql_open(const char *h, const char *u, const char *pw, const char *sch)
{
	MYSQL *conn;
	conn = mysql_init(NULL);
	if (!mysql_real_connect(conn, h, u, pw, sch, 0, NULL, 0)) {
		UMPF_DEBUG(BE_SQL "failed to connect\n");
		mysql_close(conn);
		return NULL;
	}
	return conn;
}

static void
be_mysql_close(dbconn_t conn)
{
	(void)mysql_close(conn);
	return;
}

static dbqry_t
be_mysql_qry(dbconn_t conn, const char *qry, size_t qlen)
{
	MYSQL_RES *res;
	int err;

	if (UNLIKELY(qlen == 0)) {
		qlen = strlen(qry);
	}
	if (UNLIKELY((err = mysql_real_query(conn, qry, qlen)) != 0)) {
		UMPF_DEBUG(BE_SQL ": query returned error %d\n", err);
		return NULL;
	}
	/* always just `use' the result, i.e. prepare to fetch it later */
	if ((res = mysql_use_result(conn)) == NULL) {
		/* coulda been an INSERTion */
		;
	}
	return res;
}

static void
be_mysql_free_query(dbqry_t qry)
{
	mysql_free_result(qry);
	return;
}

static dbobj_t
be_mysql_fetch(dbqry_t qry, size_t row, size_t col)
{
	MYSQL_ROW r;

	mysql_data_seek(qry, row);
	r = mysql_fetch_row(qry);
	return r[col];
}

static void
be_mysql_rows(dbqry_t qry, dbrow_f rowf, void *clo)
{
	MYSQL_ROW r;
	size_t num_fields;

	num_fields = mysql_num_fields(qry);
	while ((r = mysql_fetch_row(qry))) {
		rowf((void**)r, num_fields, clo);
	}
	return;
}

static void
be_mysql_rows_max(dbqry_t qry, dbrow_f rowf, void *clo, size_t max_rows)
{
/* like be_mysql_rows but processes at most MAX_ROWS */
	MYSQL_ROW r;
	size_t num_fields;

	num_fields = mysql_num_fields(qry);
	while (max_rows-- > 0UL && (r = mysql_fetch_row(qry))) {
		rowf((void**)r, num_fields, clo);
	}
	return;
}

static size_t
be_mysql_nrows(dbqry_t qry)
{
	return mysql_num_rows(qry);
}

#else  /* !WITH_MYSQL */
/* mysql not support, provide stubs */
static void
be_mysql_close(dbconn_t UNUSED(conn))
{
	return;
}

static dbconn_t
be_mysql_open(
	const char *UNUSED(h), const char *UNUSED(u),
	const char *UNUSED(pw), const char *UNUSED(sch))
{
	return NULL;
}

static dbqry_t
be_mysql_qry(dbconn_t UNUSED(conn), const char *UNUSED(q), size_t UNUSED(len))
{
	return NULL;
}

static void
be_mysql_free_query(dbqry_t UNUSED(qry))
{
	return;
}

static dbobj_t
be_mysql_fetch(dbqry_t UNUSED(qry), size_t UNUSED(row), size_t UNUSED(col))
{
	return NULL;
}

static void
be_mysql_rows(dbqry_t UNUSED(qry), dbrow_f UNUSED(rowf), void *UNUSED(clo))
{
	return;
}

static void
be_mysql_rows_max(
	dbqry_t UNUSED(qry), dbrow_f UNUSED(rowf),
	void *UNUSED(clo), size_t UNUSED(max_rows))
{
/* like be_mysql_rows but processes at most MAX_ROWS */
	return;
}

static size_t
be_mysql_nrows(dbqry_t UNUSED(qry))
{
	return 0UL;
}
#endif	/* WITH_MYSQL */

#if defined WITH_SQLITE

static void
be_sqlite_close(dbconn_t conn)
{
	sqlite3_close(conn);
	return;
}

static dbconn_t
be_sqlite_open(const char *file)
{
	sqlite3 *res;
	sqlite3_open(file, &res);
	return res;
}

static dbqry_t
be_sqlite_qry(dbconn_t conn, const char *qry, size_t qlen)
{
	const char *UNUSED(tmp);
	sqlite3_stmt *stmt;
	int res;

	sqlite3_prepare_v2(conn, qry, qlen, &stmt, &tmp);
	if (UNLIKELY(stmt == NULL)) {
		return NULL;
	}
	switch ((res = sqlite3_step(stmt))) {
	case SQLITE_DONE:
		/* the sequence below seems legit for this one */
	case SQLITE_ERROR:
	case SQLITE_MISUSE:
	case SQLITE_BUSY:
	default:
		UMPF_DEBUG(BE_SQL ": res %i\n", res);
		sqlite3_reset(stmt);
		sqlite3_finalize(stmt);
		stmt = NULL;
		break;
	case SQLITE_ROW:
		sqlite3_reset(stmt);
		break;
	}
	return stmt;
}

static void
be_sqlite_free_query(dbqry_t qry)
{
	sqlite3_finalize(qry);
	return;
}

static dbobj_t
be_sqlite_fetch(dbqry_t qry, size_t row, size_t col)
{
	sqlite3_value *res;

	/* `seek' to ROW-th row */
	for (size_t i = 0; i < row; i++) {
		sqlite3_step(qry);
	}
	/* now get the data for it */
	switch (sqlite3_step(qry)) {
	case SQLITE_ROW:
		res = sqlite3_column_value(qry, col);
		break;
	default:
		res = NULL;
		break;
	}
	sqlite3_reset(qry);
	return res;
}

static void
be_sqlite_rows(dbqry_t qry, dbrow_f rowf, void *clo)
{
	size_t num_fields;

	num_fields = sqlite3_column_count(qry);
	while ((sqlite3_step(qry) == SQLITE_ROW)) {
		/* VLA */
		sqlite3_value *res[num_fields];

		for (size_t j = 0; j < num_fields; j++) {
			res[0] = sqlite3_column_value(qry, j);
		}
		rowf((void**)res, num_fields, clo);
	}
	sqlite3_reset(qry);
	return;
}

static void
be_sqlite_rows_max(dbqry_t qry, dbrow_f rowf, void *clo, size_t max_rows)
{
/* like be_sqlite_rows but processes at most MAX_ROWS */
	size_t num_fields;

	num_fields = sqlite3_column_count(qry);
	while (max_rows-- > 0UL && (sqlite3_step(qry) == SQLITE_ROW)) {
		/* VLA */
		sqlite3_value *res[num_fields];

		for (size_t j = 0; j < num_fields; j++) {
			res[0] = sqlite3_column_value(qry, j);
		}
		rowf((void**)res, num_fields, clo);
	}
	sqlite3_reset(qry);
	return;
}

static size_t
be_sqlite_nrows(dbqry_t qry)
{
	size_t nrows = 0;
	while (sqlite3_step(qry) == SQLITE_ROW) {
		nrows++;
	}
	sqlite3_reset(qry);
	return nrows;
}

#else  /* !WITH_SQLITE */
/* sqlite not support, provide stubs */
static void
be_sqlite_close(dbconn_t UNUSED(conn))
{
	return;
}

static dbconn_t
be_sqlite_open(const char *UNUSED(file))
{
	return NULL;
}

static dbqry_t
be_sqlite_qry(dbconn_t UNUSED(conn), const char *UNUSED(q), size_t UNUSED(len))
{
	return NULL;
}

static void
be_sqlite_free_query(dbqry_t UNUSED(qry))
{
	return;
}

static dbobj_t
be_sqlite_fetch(dbqry_t UNUSED(qry), size_t UNUSED(row), size_t UNUSED(col))
{
	return NULL;
}

static void
be_sqlite_rows(dbqry_t UNUSED(qry), dbrow_f UNUSED(rowf), void *UNUSED(clo))
{
	return;
}

static void
be_sqlite_rows_max(
	dbqry_t UNUSED(qry), dbrow_f UNUSED(rowf),
	void *UNUSED(clo), size_t UNUSED(max_rows))
{
/* like be_mysql_rows but processes at most MAX_ROWS */
	return;
}

static size_t
be_sqlite_nrows(dbqry_t UNUSED(qry))
{
	return 0UL;
}
#endif	/* WITH_SQLITE */

/* higher level alibi functions */
DEFUN dbconn_t
be_sql_open(const char *h, const char *u, const char *pw, const char *sch)
{
	dbconn_t res;
	if (h == NULL && u == NULL && pw == NULL && sch != NULL) {
		void *tmp = be_sqlite_open(sch);
		res = be_sql_set_type(tmp, BE_SQL_SQLITE);
	} else {
		void *tmp = be_mysql_open(h, u, pw, sch);
		res = be_sql_set_type(tmp, BE_SQL_MYSQL);
	}
	UMPF_DEBUG(BE_SQL ": db handle %p\n", res);
	return res;
}

DEFUN void
be_sql_close(dbconn_t conn)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		/* don't know what to do */
		break;
	case BE_SQL_MYSQL:
		be_mysql_close(be_sql_get_conn(conn));
		break;
	case BE_SQL_SQLITE:
		be_sqlite_close(be_sql_get_conn(conn));
		break;
	}
	return;
}


/* actual actions */
DEFUN void
be_sql_new_pf(dbconn_t conn, const char *mnemo, const char *descr)
{
	size_t mlen;
	size_t dlen;
	char *qbuf, *tmp;
	static const char pre[] = "\
INSERT INTO aou_umpf_portfolio (short, description) VALUES (";
	static const char pst[] = "\
);";

	if (UNLIKELY(mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return;
	}
	/* get some basic length info to decide whether to use
	 * gbuf or an alloc'd buffer */
	if ((mlen = strlen(mnemo)) > 64) {
		mlen = 64;
	}
	if (LIKELY(descr == NULL)) {
		dlen = 0UL;
	} else {
		dlen = strlen(descr);
	}
	if (UNLIKELY(dlen > sizeof(gbuf) - 128)) {
		/* need a new buffer */
		qbuf = malloc(dlen + 128);
	} else {
		/* how about locking the bastard? */
		qbuf = gbuf;
	}

	tmp = stpncpy(qbuf, pre, sizeof(pre));
	*tmp++ = '\'';
	tmp = stpncpy_q(tmp, mnemo, mlen);
	*tmp++ = '\'';
	*tmp++ = ',';
	if (UNLIKELY(descr != NULL)) {
		*tmp++ = '\'';
		tmp = stpncpy_q(tmp, descr, dlen);
		*tmp++ = '\'';
	} else {
		tmp = stpncpy(tmp, "NULL", 4);
	}
	tmp = stpncpy(tmp, pst, sizeof(pst));
	UMPF_DEBUG(BE_SQL ": -> %s\n", qbuf);

	switch (be_sql_get_type(conn)) {
		void *res;
	case BE_SQL_UNK:
	default:
		break;
	case BE_SQL_MYSQL:
		res = be_mysql_qry(be_sql_get_conn(conn), qbuf, tmp - qbuf);
		UMPF_DEBUG(BE_SQL ": <- %p\n", res);
		if (res != NULL) {
			/* um, that's weird */
			abort();
		}
		break;
	case BE_SQL_SQLITE:
		res = be_sqlite_qry(be_sql_get_conn(conn), qbuf, tmp - qbuf);
		UMPF_DEBUG(BE_SQL ": <- %p\n", res);
		if (res != NULL) {
			/* um, that's weird */
			abort();
		}
		break;
	}
	return;
}

/* be-sql.c ends here */
