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
#include <stdint.h>
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
stpncpy_mysql_q(char *tgt, const char *src, size_t max)
{
/* mysql needs escaping of \ and ' */
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

static char*
stpncpy_sqlite_q(char *tgt, const char *src, size_t max)
{
/* sqlite needs escaping of ' only */
	static const char stpset[] = "'";
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
		/* sqlite loves doubling the to-be-escaped character */
		*res++ = src[i];
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

static dbstmt_t
be_mysql_prep(dbconn_t conn, const char *qry, size_t qlen)
{
	MYSQL_STMT *stmt = mysql_stmt_init(conn);
	if (mysql_stmt_prepare(stmt, qry, qlen) == 0) {
		return stmt;
	}
	mysql_stmt_close(stmt);
	return NULL;
}

static void
be_mysql_fin(dbstmt_t stmt)
{
	mysql_stmt_close(stmt);
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

static uint64_t
be_mysql_last_rowid(dbconn_t conn)
{
	return mysql_insert_id(conn);
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

static dbstmt_t
be_sqlite_prep(dbconn_t conn, const char *qry, size_t qlen)
{
	sqlite3_stmt *stmt;
	const char *UNUSED(tmp);

	sqlite3_prepare_v2(conn, qry, qlen, &stmt, &tmp);
	return stmt;
}

static void
be_sqlite_fin(dbstmt_t stmt)
{
	sqlite3_finalize(stmt);
	return;
}

static void
be_sqlite_bind(dbstmt_t stmt, int idx, dbobj_t obj)
{
	sqlite3_bind_value(stmt, idx, obj);
	return;
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

static uint64_t
be_sqlite_last_rowid(dbconn_t conn)
{
	return sqlite3_last_insert_rowid(conn);
}
#endif	/* WITH_SQLITE */

/* higher level alibi functions */
DEFUN dbconn_t
be_sql_open(const char *h, const char *u, const char *pw, const char *sch)
{
	dbconn_t res;
	if (h == NULL && u == NULL && pw == NULL && sch != NULL) {
#if defined WITH_SQLITE
		void *tmp = be_sqlite_open(sch);
		res = be_sql_set_type(tmp, BE_SQL_SQLITE);
#else  /* !WITH_SQLITE */
		res = NULL;
#endif	/* WITH_SQLITE */
	} else {
#if defined WITH_MYSQL
		void *tmp = be_mysql_open(h, u, pw, sch);
		res = be_sql_set_type(tmp, BE_SQL_MYSQL);
#else  /* !WITH_MYSQL */
		res = NULL;
#endif	/* WITH_MYSQL */
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
#if defined WITH_MYSQL
		be_mysql_close(be_sql_get_conn(conn));
#endif	/* WITH_MYSQL */
		break;
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		be_sqlite_close(be_sql_get_conn(conn));
#endif	/* WITH_SQLITE */
		break;
	}
	return;
}

static dbstmt_t
be_sql_prep(dbconn_t conn, const char *qry, size_t qlen)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		/* don't know what to do */
		break;
	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return be_mysql_prep(be_sql_get_conn(conn), qry, qlen);
#endif	/* WITH_MYSQL */
		break;
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		return be_sqlite_prep(be_sql_get_conn(conn), qry, qlen);
#else  /* !WITH_SQLITE */
		break;
#endif	/* WITH_SQLITE */
	}
	return NULL;
}

static dbqry_t
be_sql_qry(dbconn_t conn, const char *qry, size_t qlen)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return NULL;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return be_mysql_qry(be_sql_get_conn(conn), qry, qlen);
#else  /* !WITH_MYSQL */
		return NULL;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		return be_sqlite_qry(be_sql_get_conn(conn), qry, qlen);
#else  /* !WITH_SQLITE */
		return NULL;
#endif	/* WITH_SQLITE */
	}
}

static void
be_sql_free_query(dbconn_t conn, dbqry_t qry)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		be_mysql_free_query(qry);
		break;
#else  /* !WITH_MYSQL */
		break;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		be_sqlite_free_query(qry);
		break;
#else  /* !WITH_SQLITE */
		break;
#endif	/* WITH_SQLITE */
	}
	return;
}

static dbobj_t
be_sql_fetch(dbconn_t conn, dbqry_t qry, size_t row, size_t col)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return NULL;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return be_mysql_fetch(qry, row, col);
#else  /* !WITH_MYSQL */
		return NULL;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		return be_sqlite_fetch(qry, row, col);
#else  /* !WITH_SQLITE */
		return NULL;
#endif	/* WITH_SQLITE */
	}
}

static void
be_sql_rows(dbconn_t conn, dbqry_t qry, dbrow_f rowf, void *clo)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		be_mysql_rows(qry, rowf, clo);
		break;
#else  /* !WITH_MYSQL */
		break;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		be_sqlite_rows(qry, rowf, clo);
		break;
#else  /* !WITH_SQLITE */
		break;
#endif	/* WITH_SQLITE */
	}
	return;
}

static void
be_sql_rows_max(dbconn_t c, dbqry_t q, dbrow_f rowf, void *clo, size_t max_rows)
{
	switch (be_sql_get_type(c)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		be_mysql_rows_max(q, rowf, clo, max_rows);
		break;
#else  /* !WITH_MYSQL */
		break;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		be_sqlite_rows_max(q, rowf, clo, max_rows);
		break;
#else  /* !WITH_SQLITE */
		break;
#endif	/* WITH_SQLITE */
	}
	return;
}

static size_t
be_sql_nrows(dbconn_t conn, dbqry_t qry)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return 0UL;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return be_mysql_nrows(qry);
#else  /* !WITH_MYSQL */
		return 0UL;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		return be_sqlite_nrows(qry);
#else  /* !WITH_SQLITE */
		return 0UL;
#endif	/* WITH_SQLITE */
	}
}

static uint64_t
be_sql_last_rowid(dbconn_t conn)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return 0UL;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return be_mysql_last_rowid(be_sql_get_conn(conn));
#else  /* !WITH_MYSQL */
		return 0UL;
#endif	/* WITH_MYSQL */

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		return be_sqlite_last_rowid(be_sql_get_conn(conn));
#else  /* !WITH_SQLITE */
		return 0UL;
#endif	/* WITH_SQLITE */
	}
}

static void
be_sql_bind_text(
	dbconn_t conn, dbstmt_t stmt, int idx, const char *text, size_t tlen)
{
	/* service to the user */
	if (tlen == 0) {
		tlen = strlen(text);
	}

	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL: {
#if defined WITH_MYSQL
		MYSQL_BIND b = {
			/* STRING PARAM */
			.buffer_type = MYSQL_TYPE_STRING,
			.buffer = text,
			.buffer_length = tlen,
			.is_null = 0,
			.length = &tlen,
		};
		mysql_bind_param(stmt, &b);
		break;
#else  /* !WITH_MYSQL */
		break;
#endif	/* WITH_MYSQL */
	}

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		sqlite3_bind_text(stmt, idx, text, tlen, SQLITE_STATIC);
		break;
#else  /* !WITH_SQLITE */
		break;
#endif	/* WITH_SQLITE */
	}
	return;
}

static int
be_sql_exec_stmt(dbconn_t conn, dbstmt_t stmt)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return -1;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		return mysql_stmt_execute(stmt);
#else  /* !WITH_MYSQL */
		return -1;
#endif	/* WITH_MYSQL */
	
	case BE_SQL_SQLITE:
		/* there is no explicit execute */
#if defined WITH_SQLITE
		if (LIKELY(stmt != NULL)) {
			return 0;
		}
#endif	/* WITH_SQLITE */
		return -1;
	}
}

static dbobj_t
be_sql_fetchXXX(dbconn_t conn, dbstmt_t UNUSED(stmt))
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		return NULL;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		/* do me */
#endif	/* WITH_MYSQL */
		return NULL;
	
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		/* do me */
#endif	/* WITH_SQLITE */
		return NULL;
	}
}


/* more sql-flavour independent helpers */
static char*
stpncpy_q(dbconn_t conn, char *tgt, const char *src, size_t max)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_MYSQL:
		return stpncpy_mysql_q(tgt, src, max);
	case BE_SQL_SQLITE:
		return stpncpy_sqlite_q(tgt, src, max);
	default:
		return tgt;
	}
}

static size_t
print_zulu(char *tgt, time_t stamp)
{
	struct tm tm[1] = {{0}};

	gmtime_r(&stamp, tm);
	return strftime(tgt, 32, "%FT%T%z", tm);
}


/* actual actions */
DEFUN uint64_t
be_sql_get_pf_id(dbconn_t conn, const char *mnemo)
{
	static const char qry1[] = "\
SELECT portfolio_id FROM aou_umpf_portfolio WHERE short = ?";
	static const char qry2[] = "\
INSERT INTO aou_umpf_portfolio (short) VALUES (?)";
	size_t mnlen;
	uint64_t pf_id = 0UL;
	dbstmt_t stmt;

	if ((stmt = be_sql_prep(conn, qry1, countof_m1(qry1))) == NULL) {
		return 0UL;
	}
	/* otherwise proceed as usual */
	mnlen = strlen(mnemo);

	/* bind the params */
	be_sql_bind_text(conn, stmt, 1, mnemo, mnlen);
	switch (be_sql_get_type(conn)) {
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		switch (sqlite3_step(stmt)) {
		case SQLITE_ROW:
			/* bingo! */
			pf_id = sqlite3_column_int64(stmt, 0);
		default:
			break;
		}
#endif	 /* WITH_SQLITE */
	default:
		break;
	}
	be_sql_free_query(conn, stmt);

	if (pf_id > 0) {
		return pf_id;
	}
	/* otherwise create a new one */
	stmt = be_sql_prep(conn, qry2, countof_m1(qry2));
	be_sql_bind_text(conn, stmt, 1, mnemo, mnlen);
	switch (be_sql_get_type(conn)) {
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
			/* bingo! */
			pf_id = be_sql_last_rowid(conn);
		default:
			break;
		}
#endif	 /* WITH_SQLITE */
	default:
		break;
	}
	be_sql_free_query(conn, stmt);
	return pf_id;
}

static uint64_t
be_sql_get_sec_id(dbconn_t conn, uint64_t pf_id, const char *mnemo)
{
	static const char qry1[] = "\
SELECT security_id FROM aou_umpf_security WHERE portfolio_id = ? AND short = ?";
	static const char qry2[] = "\
INSERT INTO aou_umpf_security (portfolio_id, short) VALUES (?, ?)";
	size_t mnlen;
	uint64_t sec_id = 0UL;
	dbstmt_t stmt;

	if ((stmt = be_sql_prep(conn, qry1, countof_m1(qry1))) == NULL) {
		return 0UL;
	}
	/* otherwise proceed as usual */
	mnlen = strlen(mnemo);

	/* bind the params */
	switch (be_sql_get_type(conn)) {
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		sqlite3_bind_int64(stmt, 1, pf_id);
		sqlite3_bind_text(stmt, 2, mnemo, mnlen, SQLITE_STATIC);
		switch (sqlite3_step(stmt)) {
		case SQLITE_ROW:
			/* bingo! */
			pf_id = sqlite3_column_int64(stmt, 0);
		default:
			break;
		}
#endif	 /* WITH_SQLITE */
	default:
		break;
	}
	be_sql_free_query(conn, stmt);

	if (sec_id > 0) {
		return sec_id;
	}
	/* otherwise create a new one */
	stmt = be_sql_prep(conn, qry2, countof_m1(qry2));

	switch (be_sql_get_type(conn)) {
	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		sqlite3_bind_int64(stmt, 1, pf_id);
		sqlite3_bind_text(stmt, 2, mnemo, mnlen, SQLITE_STATIC);
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
			/* bingo! */
			sec_id = be_sql_last_rowid(conn);
		default:
			break;
		}
#endif	 /* WITH_SQLITE */
	default:
		break;
	}
	be_sql_free_query(conn, stmt);
	return sec_id;
}

DEFUN void
be_sql_new_pf(dbconn_t conn, const char *mnemo, const char *descr)
{
	size_t mlen;
	size_t dlen;
	static const char pre[] = "\
INSERT INTO aou_umpf_portfolio (short, description) VALUES (?, ?)";
	dbstmt_t stmt;
	void *res;
	uint64_t pf_id;

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

	/* otherwise create a new one */
	stmt = be_sql_prep(conn, pre, countof_m1(pre));

	switch (be_sql_get_type(conn)) {
	case BE_SQL_MYSQL: {
#if defined WITH_MYSQL
		MYSQL_BIND b[2] = {
			{
				/* STRING PARAM */
				.buffer_type = MYSQL_TYPE_STRING,
				.buffer = mnemo,
				.buffer_length = mlen,
				.is_null = 0,
				.length = &mlen,
			}, {
				/* STRING PARAM */
				.buffer_type = MYSQL_TYPE_STRING,
				.buffer = descr,
				.buffer_length = dlen,
				.is_null = dlen == 0,
				.length = &dlen,
			}
		};
		mysql_stmt_execute(stmt);
#endif	/* WITH_MYSQL */
		break;
	}

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		sqlite3_bind_text(stmt, 1, mnemo, mlen, SQLITE_STATIC);
		if (dlen > 0) {
			sqlite3_bind_text(stmt, 2, descr, dlen, SQLITE_STATIC);
		} else {
			sqlite3_bind_null(stmt, 2);
		}
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
			/* bingo! */
			break;
		default:
			goto out;
		}
#endif	 /* WITH_SQLITE */
		break;
	default:
		break;
	}

	pf_id = be_sql_last_rowid(conn);
	UMPF_DEBUG(": pf_id <- %lu\n", pf_id);
out:
	be_sql_free_query(conn, stmt);
	return;
}

DEFUN dbobj_t
be_sql_new_tag(dbconn_t conn, const char *mnemo, time_t stamp)
{
	size_t mlen;
	size_t tmplen;
	char *qbuf, *tmp;
	static const char pre[] = "\
REPLACE INTO aou_umpf_tag (portfolio_id, tag_stamp) \
SELECT portfolio_id, \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" \
FROM aou_umpf_portfolio \
WHERE short = \
\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"";
	/* we create a new tag and return its id */
	uint64_t tag_id;
	void *res;

	if (UNLIKELY(mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return NULL;
	}
	/* get some basic length info to decide whether to use
	 * gbuf or an alloc'd buffer */
	if ((mlen = strlen(mnemo)) > 64) {
		mlen = 64;
	}

	/* get some resources for qbuf */
	qbuf = malloc(sizeof(pre));
	memcpy(qbuf, pre, sizeof(pre));
	/* get them points to insert our variables */
	tmp = qbuf + 52 + 22;
	tmp += tmplen = print_zulu(tmp, stamp);
	*tmp = '\"';
	/* pad the rest with SPCs */
	memset(tmp + 1, ' ', 32 - tmplen);

	tmp = qbuf + 52 + 56 + 24 + 14 + 1;
	tmp = stpncpy_q(conn, tmp, mnemo, mlen);
	*tmp++ = '\"';
	*tmp = '\0';

	UMPF_DEBUG(BE_SQL ": -> %s\n", qbuf);

	res = be_sql_qry(conn, qbuf, tmp - qbuf);
	UMPF_DEBUG(BE_SQL ": <- %p\n", res);
	if (res != NULL) {
		/* um, that's weird */
		abort();
	}
	/* retrieve our tag id */
	tag_id = be_sql_last_rowid(conn);
	UMPF_DEBUG(BE_SQL ": iid <- %lu\n", tag_id);
	return (dbobj_t)tag_id;
}

DEFUN void
be_sql_set_pos(
	dbconn_t conn, dbobj_t tag_id, const char *mnemo, double l, double s)
{
	static const char pre[] = "\
INSERT INTO aou_umpf_position (tag_id, security_id, long_qty, short_qty) \
SELECT portfolio_id, \"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\" \
FROM aou_umpf_portfolio \
WHERE short = \
\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\"";
	return;
}

/* be-sql.c ends here */
