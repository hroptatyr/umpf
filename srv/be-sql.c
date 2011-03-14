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

typedef void *dbstmt_t;

typedef enum {
	BE_SQL_UNK,
	BE_SQL_MYSQL,
	BE_SQL_SQLITE,
} be_sql_type_t;

typedef enum {
	BE_BIND_TYPE_UNK,
	BE_BIND_TYPE_TEXT,
	BE_BIND_TYPE_INT32,
	BE_BIND_TYPE_INT64,
	BE_BIND_TYPE_STAMP,
	BE_BIND_TYPE_DOUBLE,
	BE_BIND_TYPE_NULL,
} be_bind_type_t;

typedef struct __bind_s *__bind_t;

struct __bind_s {
	be_bind_type_t type;
	union {
		const char *txt;
		char *ptr;
		int32_t i32;
		int64_t i64;
		time_t tm;
		double dbl;
	};
	size_t len;
};

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
#endif	/* WITH_SQLITE */
		break;
	}
	return NULL;
}

static void
be_sql_fin(dbconn_t conn, dbstmt_t stmt)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		/* don't know what to do */
		break;

	case BE_SQL_MYSQL:
#if defined WITH_MYSQL
		mysql_stmt_close(stmt);
#endif	/* WITH_MYSQL */
		break;

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		sqlite3_finalize(stmt);
#endif	/* WITH_SQLITE */
		break;
	}
	return;
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

#if defined WITH_MYSQL
static void
tm_to_mysql_time(MYSQL_TIME *tgt, struct tm *src)
{
	tgt->year = src->tm_year + 1900;
	tgt->month = src->tm_mon + 1;
	tgt->day = src->tm_mday;
	tgt->hour = src->tm_hour;
	tgt->minute = src->tm_min;
	tgt->second = src->tm_sec;
	tgt->second_part = 0UL;
	tgt->neg = 0;
	return;
}

static void
mysql_time_to_tm(struct tm *tgt, MYSQL_TIME *src)
{
	tgt->tm_year = src->year - 1900;
	tgt->tm_mon = src->month + 1;
	tgt->tm_mday = src->day;
	tgt->tm_hour = src->hour;
	tgt->tm_min = src->minute;
	tgt->tm_sec = src->second;
	return;
}

static void
be_mysql_bind1(MYSQL_BIND *tgt, __bind_t src, void **extra)
{
	switch (src->type) {
	case BE_BIND_TYPE_UNK:
	default:
		break;

	case BE_BIND_TYPE_TEXT:
		tgt->buffer_type = MYSQL_TYPE_STRING;
		/* get around const qualifier, sigh */
		tgt->buffer = src->ptr;
		if (src->len > 0) {
			tgt->buffer_length = src->len;
		} else {
			tgt->buffer_length = strlen(src->txt);
		}
		tgt->is_null = NULL;
		tgt->length = &src->len;
		break;

	case BE_BIND_TYPE_NULL:
		tgt->buffer_type = MYSQL_TYPE_NULL;
		break;

	case BE_BIND_TYPE_INT64:
		tgt->buffer_type = MYSQL_TYPE_LONG;
		tgt->buffer = (char*)&src->i64;
		break;

	case BE_BIND_TYPE_INT32:
		tgt->buffer_type = MYSQL_TYPE_LONG;
		tgt->buffer = (char*)&src->i32;
		break;

	case BE_BIND_TYPE_DOUBLE:
		tgt->buffer_type = MYSQL_TYPE_DOUBLE;
		tgt->buffer = (char*)&src->dbl;
		break;

	case BE_BIND_TYPE_STAMP: {
		MYSQL_TIME *mytm = *extra;
		struct tm tm = {0};

		src->len = sizeof(*mytm);
		tgt->buffer_type = MYSQL_TYPE_TIMESTAMP;
		tgt->buffer = (char*)mytm;
		tgt->length = &src->len;
		/* wind extra */
		*extra = (char*)mytm + sizeof(*mytm);
		/* get gm time */
		gmtime_r(&src->tm, &tm);
		/* assign to mysql struct */
		tm_to_mysql_time(mytm, &tm);
		break;
	}
	}
	return;
}
#endif	/* WITH_MYSQL */

#if defined WITH_SQLITE
static void
be_sqlite_bind1(dbstmt_t stmt, int idx, __bind_t src)
{
	switch (src->type) {
	case BE_BIND_TYPE_UNK:
	default:
		UMPF_DEBUG(BE_SQL ": binding unknown type :O\n");
		break;

	case BE_BIND_TYPE_TEXT:
		if (src->len == 0) {
			src->len = strlen(src->txt);
		}
		sqlite3_bind_text(stmt, idx, src->txt, src->len, SQLITE_STATIC);
		break;

	case BE_BIND_TYPE_NULL:
		sqlite3_bind_null(stmt, idx);
		break;

	case BE_BIND_TYPE_INT64:
		sqlite3_bind_int64(stmt, idx, src->i64);
		break;

	case BE_BIND_TYPE_INT32:
		sqlite3_bind_int(stmt, idx, src->i32);
		break;

	case BE_BIND_TYPE_DOUBLE:
		sqlite3_bind_double(stmt, idx, src->dbl);
		break;

	case BE_BIND_TYPE_STAMP: {
		struct tm tm = {0};
		size_t len;

		gmtime_r(&src->tm, &tm);
		len = strftime(gbuf, 32, "%FT%T%z", &tm);
		sqlite3_bind_text(stmt, idx, gbuf, len, SQLITE_STATIC);
		break;
	}
	}
	return;
}
#endif	/* WITH_MYSQL */

static void
be_sql_bind(dbconn_t conn, dbstmt_t stmt, __bind_t b, size_t nb)
{
	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL: {
#if defined WITH_MYSQL
		/* just take our gbuf for this non-sense, makes this
		 * routine non-reentrant of course */
		MYSQL_BIND *mb = (void*)gbuf;
		void *extra = (mb + nb);

		memset(mb, 0, nb * sizeof(*mb));
		for (size_t i = 0; i < nb; i++) {
			be_mysql_bind1(mb + i, b + i, &extra);
		}
		mysql_stmt_bind_param(stmt, mb);
#endif	/* WITH_MYSQL */
		break;
	}

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		for (size_t i = 0; i < nb; i++) {
			be_sqlite_bind1(stmt, /* starts at 1*/i + 1, b + i);
		}
#endif	/* WITH_SQLITE */
		break;
	}
	return;
}

#if defined WITH_MYSQL
static enum enum_field_types
bind_type_to_mysql_type(be_bind_type_t t)
{
	switch (t) {
	case BE_BIND_TYPE_UNK:
	default:
		return MYSQL_TYPE_NULL;
	case BE_BIND_TYPE_TEXT:
		return MYSQL_TYPE_STRING;
	case BE_BIND_TYPE_INT32:
	case BE_BIND_TYPE_INT64:
		return MYSQL_TYPE_LONG;
	case BE_BIND_TYPE_STAMP:
		return MYSQL_TYPE_TIMESTAMP;
	case BE_BIND_TYPE_DOUBLE:
		/* oculd be MYSQL_TYPE_NEWDECIMAL */
		return MYSQL_TYPE_DOUBLE;
	case BE_BIND_TYPE_NULL:
		return MYSQL_TYPE_NULL;
	}
}

static void
be_mysql_fetch1(__bind_t tgt, dbstmt_t stmt, MYSQL_BIND *src, int idx)
{
	/* i'm not sure about this, we may change the type slot
	 * of TGT in accordance to what mysql deems feasible,
	 * i.e. if you request a INT64 in TGT and mysql comes up
	 * with MYSQL_TYPE_TIMESTAMP we'd change the type to
	 * BE_BIND_TYPE_STAMP, consecutive calls will then
	 * request a time stamp. */
	if (*src->is_null) {
		UMPF_DEBUG(BE_SQL ": mysql result is NULL\n");
		return;
	}
	switch (src->buffer_type) {
	default:
		UMPF_DEBUG(BE_SQL ": mysql returned unknown type\n");
		break;

	case MYSQL_TYPE_STRING:
		tgt->type = BE_BIND_TYPE_TEXT;
		tgt->len = *src->length;
		if (*src->length > src->buffer_length) {
			/* should reget the bugger */
			size_t new_size = *src->length;

			tgt->ptr = malloc(new_size + 1);
			src->buffer = tgt->ptr;
			src->buffer_length = new_size;
			mysql_stmt_fetch_column(stmt, src, idx, 0);
			tgt->ptr[new_size] = '\0';
		} else {
			tgt->ptr = strndup(src->buffer, tgt->len);
		}
		break;

	case MYSQL_TYPE_NULL:
		tgt->type = BE_BIND_TYPE_NULL;
		break;

	case MYSQL_TYPE_LONG:
		tgt->type = BE_BIND_TYPE_INT64;
		tgt->i64 = *(int64_t*)src->buffer;
		break;

	case MYSQL_TYPE_DOUBLE:
		tgt->type = BE_BIND_TYPE_DOUBLE;
		tgt->dbl = *(double*)src->buffer;
		break;

	case MYSQL_TYPE_TIMESTAMP: {
		MYSQL_TIME *mytm = (void*)src->buffer;
		struct tm tm = {0};

		tgt->type = BE_BIND_TYPE_STAMP;
		mysql_time_to_tm(&tm, mytm);
		tgt->tm = timegm(&tm);
		break;
	}
	}
	return;
}

static int
be_mysql_fetch(dbstmt_t stmt, __bind_t b, size_t nb)
{
	/* we only support the 0-th column */
	MYSQL_BIND *mb = (void*)gbuf;
	size_t *lens = (void*)(mb + nb);
	my_bool *nullps = (void*)(lens + nb);
	union {
		MYSQL_TIME tm;
		void *ptr;
	} *extra = (void*)(nullps + nb);

	/* rinse and set up */
	memset(mb, 0, (char*)(extra + nb) - (char*)mb);
	for (size_t i = 0; i < nb; i++) {
		mb[i].buffer_type =
			bind_type_to_mysql_type(b[i].type);
		mb[i].buffer = extra + i;
		mb[i].buffer_length = sizeof(*extra);
		mb[i].is_null = nullps + i;
		mb[i].length = lens + i;
	}
	/* bind and fetch*/
	mysql_stmt_bind_result(stmt, mb);
	switch (mysql_stmt_fetch(stmt)) {
	case MYSQL_NO_DATA:
	case 1:
		return -1;
	default:
		/* sort our results */
		for (size_t i = 0; i < nb; i++) {
			be_mysql_fetch1(b + i, stmt, mb + i, i);
		}
	}
	return 0;
}
#endif	/* WITH_MYSQL */

#if defined WITH_SQLITE
static void
be_sqlite_fetch1(__bind_t tgt, dbstmt_t stmt, int idx)
{
	UMPF_DEBUG(BE_SQL ": sqlite result\n");

	switch (tgt->type) {
	case BE_BIND_TYPE_UNK:
	case BE_BIND_TYPE_NULL:
	default:
		break;

	case BE_BIND_TYPE_TEXT: {
		const char *tmp = (const char*)sqlite3_column_text(stmt, idx);

		if (LIKELY(tmp != NULL)) {
			size_t len = strlen(tmp);

			tgt->ptr = strndup(tmp, len);
			tgt->len = len;
		} else {
			tgt->ptr = NULL;
		}
		break;
	}

	case BE_BIND_TYPE_INT32:
		tgt->i32 = sqlite3_column_int(stmt, idx);
		break;

	case BE_BIND_TYPE_INT64:
		tgt->i64 = sqlite3_column_int64(stmt, idx);
		break;

	case BE_BIND_TYPE_DOUBLE:
		tgt->dbl = sqlite3_column_double(stmt, idx);
		break;

	case BE_BIND_TYPE_STAMP: {
		tgt->tm = sqlite3_column_int(stmt, idx);
		break;
	}
	}
	return;
}

static int
be_sqlite_fetch(dbstmt_t stmt, __bind_t b, size_t nb)
{
	/* check if we need to do stuff */
	if (UNLIKELY(sqlite3_data_count(stmt) == 0)) {
		return -1;
	}
	/* sqlite data IS fetched already, so just frob and fetch again */
	for (size_t i = 0; i < nb; i++) {
		be_sqlite_fetch1(b + i, stmt, i);
	}
	switch (sqlite3_step(stmt)) {
	case SQLITE_DONE:
	case SQLITE_ROW:
		return 0;
	default:
		return -1;
	}
}
#endif	/* WITH_SQLITE */

static int
be_sql_fetch(dbconn_t conn, dbstmt_t stmt, __bind_t b, size_t nb)
{
	int res = -1;

	switch (be_sql_get_type(conn)) {
	case BE_SQL_UNK:
	default:
		break;

	case BE_SQL_MYSQL: {
#if defined WITH_MYSQL
		res = be_mysql_fetch(stmt, b, nb);
#endif	/* WITH_MYSQL */
		break;
	}

	case BE_SQL_SQLITE:
#if defined WITH_SQLITE
		res = be_sqlite_fetch(stmt, b, nb);
#endif	/* WITH_SQLITE */
		break;
	}
	return res;
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
		/* there is no explicit execute, we do the step here,
		 * then when we fetch results we do another step */
#if defined WITH_SQLITE
		switch (sqlite3_step(stmt)) {
		case SQLITE_DONE:
		case SQLITE_ROW:
			return 0;
		default:
			break;
		}
#endif	/* WITH_SQLITE */
		return -1;
	}
}


/* actual actions */
/* tag mumbo jumbo */
struct __tag_s {
	uint64_t pf_id;
	uint64_t tag_id;
	time_t tag_stamp;
	time_t log_stamp;
};

static uint64_t
__get_pf_id(dbconn_t conn, const char *mnemo)
{
/* this is really a get-create */
	static const char qry1[] = "\
SELECT portfolio_id FROM aou_umpf_portfolio WHERE short = ?";
	static const char qry2[] = "\
INSERT INTO aou_umpf_portfolio (short) VALUES (?)";
	size_t mnlen = strlen(mnemo);
	uint64_t pf_id = 0UL;
	dbstmt_t stmt;
	struct __bind_s b[1] = {{
			.type = BE_BIND_TYPE_TEXT,
			.txt = mnemo,
			.len = mnlen,
		}};

	if ((stmt = be_sql_prep(conn, qry1, countof_m1(qry1))) == NULL) {
		return 0UL;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[1] = {{
				.type = BE_BIND_TYPE_INT64,
				/* default value */
				.i64 = 0UL,
			}};
		be_sql_fetch(conn, stmt, rb, countof(rb));
		pf_id = rb[0].i64;
	}
	be_sql_fin(conn, stmt);

	if (pf_id > 0) {
		return pf_id;
	}
	/* otherwise create a new one */
	stmt = be_sql_prep(conn, qry2, countof_m1(qry2));
	/* bind params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		pf_id = be_sql_last_rowid(conn);
	}

	be_sql_fin(conn, stmt);
	return pf_id;
}

static uint64_t
__get_sec_id_from_mnemos(
	dbconn_t conn, const char *pf_mnemo, const char *sec_mnemo)
{
	static const char qry[] = "\
SELECT security_id FROM aou_umpf_security\n\
LEFT JOIN aou_umpf_portfolio USING (portfolio_id)\n\
WHERE aou_umpf_portfolio.short = ? AND aou_umpf_security.short = ?";
	size_t pf_mnlen = strlen(pf_mnemo);
	size_t sec_mnlen = strlen(sec_mnemo);
	uint64_t sec_id = 0UL;
	dbstmt_t stmt;
	/* for our parameter binding later on */
	struct __bind_s b[2] = {{
			.type = BE_BIND_TYPE_TEXT,
			.txt = pf_mnemo,
			.len = pf_mnlen,
		}, {
			.type = BE_BIND_TYPE_TEXT,
			.txt = sec_mnemo,
			.len = sec_mnlen,
		}};

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return 0UL;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[1] = {{
				.type = BE_BIND_TYPE_INT64,
				/* default value */
				.i64 = 0UL,
			}};
		be_sql_fetch(conn, stmt, rb, countof(rb));
		sec_id = rb[0].i64;
	}
	be_sql_fin(conn, stmt);
	return sec_id;
}

static uint64_t
__get_sec_id(dbconn_t conn, uint64_t pf_id, const char *mnemo)
{
	static const char qry1[] = "\
SELECT security_id FROM aou_umpf_security WHERE portfolio_id = ? AND short = ?";
	static const char qry2[] = "\
INSERT INTO aou_umpf_security (portfolio_id, short) VALUES (?, ?)";
	size_t mnlen = strlen(mnemo);
	uint64_t sec_id = 0UL;
	dbstmt_t stmt;
	/* for our parameter binding later on */
	struct __bind_s b[2] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = pf_id,
		}, {
			.type = BE_BIND_TYPE_TEXT,
			.txt = mnemo,
			.len = mnlen,
		}};

	if ((stmt = be_sql_prep(conn, qry1, countof_m1(qry1))) == NULL) {
		return 0UL;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[1] = {{
				.type = BE_BIND_TYPE_INT64,
				/* default value */
				.i64 = 0UL,
			}};
		be_sql_fetch(conn, stmt, rb, countof(rb));
		sec_id = rb[0].i64;
	}
	be_sql_fin(conn, stmt);

	if (sec_id > 0) {
		return sec_id;
	}
	/* otherwise create a new one */
	stmt = be_sql_prep(conn, qry2, countof_m1(qry2));
	/* bind the params again */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		sec_id = be_sql_last_rowid(conn);
	}
	be_sql_fin(conn, stmt);
	return sec_id;
}

static uint64_t
__new_tag_id(dbconn_t conn, uint64_t pf_id, time_t stamp)
{
	static const char qry[] = "\
INSERT INTO aou_umpf_tag (portfolio_id, tag_stamp) VALUES (?, ?)";
	uint64_t tag_id = 0UL;
	dbstmt_t stmt;
	struct __bind_s b[2] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = pf_id,
		}, {
			.type = BE_BIND_TYPE_STAMP,
			.tm = stamp,
		}};

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return 0UL;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		tag_id = be_sql_last_rowid(conn);
	}
	be_sql_fin(conn, stmt);
	return tag_id;
}

static uint64_t
__get_tag_id(dbconn_t conn, uint64_t pf_id, time_t stamp)
{
/* finds the largest tag id whose tag_stamp is before STAMP */
	static const char qry[] = "\
SELECT tag_id FROM aou_umpf_tag \
WHERE portfolio_id = ? AND tag_stamp <= ? \
ORDER BY tag_stamp DESC, tag_id DESC \
LIMIT 1";
	uint64_t tag_id = 0UL;
	dbstmt_t stmt;
	struct __bind_s b[2] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = pf_id,
		}, {
			.type = BE_BIND_TYPE_STAMP,
			.tm = stamp,
		}};

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return 0UL;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[1] = {{
				.type = BE_BIND_TYPE_INT64,
				/* default value */
				.i64 = 0UL,
			}};
		be_sql_fetch(conn, stmt, rb, countof(rb));
		tag_id = rb[0].i64;
	}
	be_sql_fin(conn, stmt);
	return tag_id;
}

static int
__get_tag(struct __tag_s *tag, dbconn_t conn, uint64_t pf_id, time_t stamp)
{
/* like __get_tag_id() but also find out about the time stamp */
	static const char qry[] = "\
SELECT tag_id, tag_stamp, log_stamp \
FROM aou_umpf_tag \
WHERE portfolio_id = ? AND tag_stamp <= ? \
ORDER BY tag_stamp DESC, tag_id DESC \
LIMIT 1";
	dbstmt_t stmt;
	struct __bind_s b[2] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = pf_id,
		}, {
			.type = BE_BIND_TYPE_STAMP,
			.tm = stamp,
		}};
	int res = -1;

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return -1;
	}

	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[3];

		/* just assign the type wishes for the results */
		rb[0].type = BE_BIND_TYPE_INT64;
		rb[1].type = BE_BIND_TYPE_STAMP;
		rb[2].type = BE_BIND_TYPE_STAMP;

		if (LIKELY(be_sql_fetch(conn, stmt, rb, countof(rb)) == 0)) {
			res = 0;
			tag->tag_id = rb[0].i64;
			tag->pf_id = pf_id;
			tag->tag_stamp = rb[1].tm;
			tag->log_stamp = rb[2].tm;
		}
	}
	be_sql_fin(conn, stmt);
	return res;
}


/* public functions */
DEFUN dbobj_t
be_sql_new_pf(dbconn_t conn, const char *mnemo, const char *descr)
{
/* new_pf is a get_pf_id + update the description */
	size_t dlen;
	static const char pre[] = "\
UPDATE aou_umpf_portfolio SET description = ? WHERE portfolio_id = ?";
	dbstmt_t stmt;
	uint64_t pf_id;

	if (UNLIKELY(mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return NULL;
	} else if ((pf_id = __get_pf_id(conn, mnemo)) == 0) {
		/* portfolio getter is fucked */
		UMPF_DEBUG(BE_SQL ": could not obtain portfolio id\n");
		return NULL;
	}

	if (LIKELY(descr == NULL)) {
		return (dbobj_t)pf_id;
	}
	/* otherwise there's more work to be done */
	dlen = strlen(descr);
	stmt = be_sql_prep(conn, pre, countof_m1(pre));

	/* bind the params */
	{
		struct __bind_s b[2] = {{
				.type = BE_BIND_TYPE_TEXT,
				.txt = descr,
				.len = dlen,
			}, {
				.type = BE_BIND_TYPE_INT64,
				.i64 = pf_id,
			}};
		be_sql_bind(conn, stmt, b, countof(b));
		be_sql_exec_stmt(conn, stmt);
		be_sql_fin(conn, stmt);
	}
	return (dbobj_t)pf_id;
}

DEFUN void
be_sql_free_pf(dbconn_t UNUSED(conn), dbobj_t UNUSED(pf))
{
	/* it's just a uint64 so do fuckall */
	return;
}

DEFUN dbobj_t
be_sql_new_tag(dbconn_t conn, const char *mnemo, time_t stamp)
{
	struct __tag_s *tag = xnew(*tag);

	/* get portfolio */
	tag->pf_id = __get_pf_id(conn, mnemo);
	/* we create a new tag and return its id */
	tag->tag_id = __new_tag_id(conn, tag->pf_id, stamp);
	UMPF_DEBUG("tag_id <- %lu for pf_id %lu\n", tag->tag_id, tag->pf_id);
	return (dbobj_t)tag;
}

DEFUN dbobj_t
be_sql_new_tag_pf(dbconn_t conn, dbobj_t pf, time_t stamp)
{
	struct __tag_s *tag = xnew(*tag);

	/* get portfolio */
	tag->pf_id = (uint64_t)pf;
	/* we create a new tag and return its id */
	tag->tag_id = __new_tag_id(conn, tag->pf_id, stamp);
	return (dbobj_t)tag;
}

DEFUN dbobj_t
be_sql_get_tag(dbconn_t conn, const char *mnemo, time_t stamp)
{
	struct __tag_s *tag, tmp;
	uint64_t pf_id;

	/* get portfolio */
	if ((pf_id = __get_pf_id(conn, mnemo)) == 0) {
		UMPF_DEBUG(BE_SQL ": uh oh, no portfolio id for %s\n", mnemo);
		return NULL;
	} else if (__get_tag(&tmp, conn, pf_id, stamp) != 0) {
		return NULL;
	}
	/* oh we seem to have hit the jackpot */
	tag = xnew(*tag);
	*tag = tmp;
	UMPF_DEBUG(BE_SQL ": tag_id <- %lu (%lu, %ld)\n",
		   tmp.tag_id, tmp.pf_id, tmp.tag_stamp);
	return (dbobj_t)tag;
}

DECLF void
be_sql_free_tag(dbconn_t UNUSED(conn), dbobj_t tag)
{
	free((struct __tag_s*)tag);
	return;
}

DEFUN void
be_sql_set_pos(dbconn_t c, dbobj_t tag, const char *mnemo, double l, double s)
{
	struct __tag_s *t = tag;
	/* get security */
	uint64_t sec_id;
	dbstmt_t stmt;
	static const char qry[] = "\
INSERT INTO aou_umpf_position (tag_id, security_id, long_qty, short_qty) \
VALUES (?, ?, ?, ?)";

	/* obtain a sec id first, get/creator */
	if ((sec_id = __get_sec_id(c, t->pf_id, mnemo)) == 0UL) {
		UMPF_DEBUG(BE_SQL ": no security id for pf %lu %s\n",
			   t->pf_id, mnemo);
		return;
	} else if ((stmt = be_sql_prep(c, qry, countof_m1(qry))) == NULL) {
		return;
	}
	/* bind the params */
	{
		struct __bind_s b[4] = {{
				.type = BE_BIND_TYPE_INT64,
				.i64 = t->tag_id,
			}, {
				.type = BE_BIND_TYPE_INT64,
				.i64 = sec_id,
			}, {
				.type = BE_BIND_TYPE_DOUBLE,
				.dbl = l,
			}, {
				.type = BE_BIND_TYPE_DOUBLE,
				.dbl = s,
			}};
		be_sql_bind(c, stmt, b, countof(b));
		/* execute */
		be_sql_exec_stmt(c, stmt);
		be_sql_fin(c, stmt);
	}
	return;
}

DEFUN time_t
be_sql_get_stamp(dbconn_t UNUSED(conn), dbobj_t tag)
{
	struct __tag_s *t = tag;
	return t->tag_stamp;
}

DEFUN size_t
be_sql_get_npos(dbconn_t conn, dbobj_t tag)
{
	struct __tag_s *t = tag;
	dbstmt_t stmt;
	static const char qry[] = "\
SELECT COUNT(security_id) FROM aou_umpf_position \
WHERE tag_id = ?";
	struct __bind_s b[1] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = t->tag_id,
		}};
	size_t npos = 0UL;

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return 0UL;
	}
	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s rb[1] = {{
				.type = BE_BIND_TYPE_INT64,
				/* default value */
				.i64 = 0UL,
			}};
		be_sql_fetch(conn, stmt, rb, countof(rb));
		npos = rb[0].i64;
	}
	be_sql_fin(conn, stmt);
	return npos;
}

DECLF void
be_sql_get_pos(
	dbconn_t conn, dbobj_t tag,
	int(*cb)(char*, double, double, void*), void *clo)
{
	struct __tag_s *t = tag;
	dbstmt_t stmt;
	static const char qry[] = "\
SELECT short, long_qty, short_qty FROM aou_umpf_position \
LEFT JOIN aou_umpf_security USING (security_id) \
WHERE tag_id = ?";
	struct __bind_s b[1] = {{
			.type = BE_BIND_TYPE_INT64,
			.i64 = t->tag_id,
		}};

	if ((stmt = be_sql_prep(conn, qry, countof_m1(qry))) == NULL) {
		return;
	}
	/* bind the params */
	be_sql_bind(conn, stmt, b, countof(b));
	/* execute */
	if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
		struct __bind_s mb[3];

		/* just assign the type wishes for the results */
		mb[0].type = BE_BIND_TYPE_TEXT;
		mb[1].type = BE_BIND_TYPE_DOUBLE;
		mb[2].type = BE_BIND_TYPE_DOUBLE;

		while (be_sql_fetch(conn, stmt, mb, countof(mb)) == 0 &&
		       cb(mb[0].ptr, mb[1].dbl, mb[2].dbl, clo) == 0);
	}
	be_sql_fin(conn, stmt);
	return;
}

DEFUN dbobj_t
be_sql_new_sec(
	dbconn_t conn, const char *pf_mnemo,
	const char *sec_mnemo, const char *descr)
{
/* this is a get_pf + get_sec/INSERT + update */
	size_t dlen;
	static const char pre[] = "\
UPDATE aou_umpf_security SET description = ? WHERE security_id = ?";
	dbstmt_t stmt;
	uint64_t pf_id;
	uint64_t sec_id;

	if (UNLIKELY(pf_mnemo == NULL || sec_mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return NULL;
	} else if ((pf_id = __get_pf_id(conn, pf_mnemo)) == 0) {
		/* portfolio getter is fucked */
		UMPF_DEBUG(BE_SQL ": could not obtain portfolio id\n");
		return NULL;
	} else if ((sec_id = __get_sec_id(conn, pf_id, sec_mnemo)) == 0) {
		/* portfolio getter is fucked */
		UMPF_DEBUG(BE_SQL ": could not obtain security id\n");
		return NULL;
	}

	/* otherwise there's more work to be done */
	if (UNLIKELY(descr == NULL)) {
		/* journey ends here */
		return (dbobj_t)sec_id;
	}
	dlen = strlen(descr);
	stmt = be_sql_prep(conn, pre, countof_m1(pre));

	/* bind the params */
	{
		struct __bind_s b[2] = {{
				.type = BE_BIND_TYPE_TEXT,
				.txt = descr,
				.len = dlen,
			}, {
				.type = BE_BIND_TYPE_INT64,
				.i64 = sec_id,
			}};
		be_sql_bind(conn, stmt, b, countof(b));
		be_sql_exec_stmt(conn, stmt);
		be_sql_fin(conn, stmt);
	}
	return (dbobj_t)sec_id;
}

DEFUN dbobj_t
be_sql_set_sec(
	dbconn_t conn, const char *pf_mnemo,
	const char *sec_mnemo, const char *descr)
{
/* this is a get_sec + update */
	size_t dlen;
	static const char pre[] = "\
UPDATE aou_umpf_security SET description = ? WHERE security_id = ?";
	dbstmt_t stmt;
	uint64_t sec_id;

	if (UNLIKELY(pf_mnemo == NULL || sec_mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return NULL;
	} else if ((sec_id = __get_sec_id_from_mnemos(
			    conn, pf_mnemo, sec_mnemo)) == 0) {
		/* portfolio getter is fucked */
		UMPF_DEBUG(BE_SQL ": could not obtain security id\n");
		return NULL;
	}

	if (UNLIKELY(descr == NULL)) {
		return (dbobj_t)sec_id;
	}
	/* otherwise there's more work to be done */
	dlen = strlen(descr);
	stmt = be_sql_prep(conn, pre, countof_m1(pre));

	/* bind the params */
	{
		struct __bind_s b[2] = {{
				.type = BE_BIND_TYPE_TEXT,
				.txt = descr,
				.len = dlen,
			}, {
				.type = BE_BIND_TYPE_INT64,
				.i64 = sec_id,
			}};
		be_sql_bind(conn, stmt, b, countof(b));
		be_sql_exec_stmt(conn, stmt);
		be_sql_fin(conn, stmt);
	}
	return (dbobj_t)sec_id;
}

DECLF char*
be_sql_get_sec(dbconn_t conn, const char *pf_mnemo, const char *sec_mnemo)
{
/* this is a get_sec + update */
	static const char pre[] = "\
SELECT description FROM aou_umpf_security WHERE security_id = ?";
	dbstmt_t stmt;
	uint64_t sec_id;
	char *descr;

	if (UNLIKELY(pf_mnemo == NULL || sec_mnemo == NULL)) {
		UMPF_DEBUG(BE_SQL ": mnemonic of size 0 not allowed\n");
		return NULL;
	} else if ((sec_id = __get_sec_id_from_mnemos(
			    conn, pf_mnemo, sec_mnemo)) == 0) {
		/* portfolio getter is fucked */
		UMPF_DEBUG(BE_SQL ": could not obtain security id\n");
		return NULL;
	}

	/* get them statements prepared */
	stmt = be_sql_prep(conn, pre, countof_m1(pre));

	/* bind the params */
	{
		struct __bind_s b[1] = {{
				.type = BE_BIND_TYPE_INT64,
				.i64 = sec_id,
			}};
		/* bind + exec */
		be_sql_bind(conn, stmt, b, countof(b));
		if (LIKELY(be_sql_exec_stmt(conn, stmt) == 0)) {
			struct __bind_s rb[1] = {{
					.type = BE_BIND_TYPE_TEXT,
					/* default value */
					.ptr = NULL,
				}};
			be_sql_fetch(conn, stmt, rb, countof(rb));
			descr = rb[0].ptr;
		}
		be_sql_fin(conn, stmt);
	}
	return descr;
}

DEFUN void
be_sql_free_sec(dbconn_t UNUSED(conn), dbobj_t UNUSED(pf))
{
	/* it's just a uint64 so do fuckall */
	return;
}

/* be-sql.c ends here */
