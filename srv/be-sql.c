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
#if defined HAVE_MYSQL
# include <mysql/mysql.h>
#endif
#include "nifty.h"
#include "be-sql.h"

#define BE_SQL		"mod/pfd/sql"

#if defined HAVE_MYSQL
DEFUN dbconn_t
be_sql_connect(const char *h, const char *u, const char *pw, const char *sch)
{
	MYSQL *conn;
	conn = mysql_init(NULL);
	if (!mysql_real_connect(conn, h, u, pw, sch, 0, NULL, 0)) {
		PFD_DEBUG(BE_SQL "failed to connect\n");
		mysql_close(conn);
		return NULL;
	}
	return conn;
}

DEFUN void
be_sql_close(dbconn_t conn)
{
	(void)mysql_close(conn);
	return;
}

#else  /* !HAVE_MYSQL */

DEFUN dbconn_t
be_sql_connect(
	const char *UNUSED(host), const char *UNUSED(user),
	const char *UNUSED(pw), const char *UNUSED(sch))
{
	return NULL;
}

DEFUN void
be_sql_close(dbconn_t UNUSED(conn))
{
	return;
}
#endif	/* HAVE_MYSQL */

/* be-sql.c ends here */
