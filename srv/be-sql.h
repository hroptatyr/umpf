/*** be-sql.h -- SQL backend for pfd
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

#if !defined INCLUDED_be_sql_h_
#define INCLUDED_be_sql_h_

#include <stddef.h>
#include <time.h>

/* just some convenience aliases */
typedef void *dbobj_t;
typedef void *dbconn_t;
typedef void *dbqry_t;
typedef void (*dbrow_f)(dbobj_t *row, size_t num_fields, void *closure);

DECLF dbconn_t
be_sql_open(
	const char *host, const char *user,
	const char *passwd, const char *dbname);
DECLF void be_sql_close(dbconn_t conn);


/* actual actions */
/**
 * Corresponds to UMPF_MSG_NEW_PF */
DECLF dbobj_t be_sql_new_pf(dbconn_t, const char *mnemo, const char *descr);

/**
 * Free resources associated with PF as obtained by `be_sql_new_pf()'. */
DECLF void be_sql_free_pf(dbconn_t, dbobj_t pf);

/**
 * Corresponds to the first part of UMPF_MSG_SET_PF.
 * \param MNEMO is the mnemonic of the portfolio.
 * \param STAMP is the time stamp at which positions are to be recorded. */
DECLF dbobj_t be_sql_new_tag(dbconn_t, const char *mnemo, time_t stamp);

/**
 * Like `be_sql_new_tag()' but for portfolio objects instead of mnemo. */
DECLF dbobj_t be_sql_new_tag_pf(dbconn_t, dbobj_t pf, time_t stamp);

/**
 * Corresponds to the first part of UMPF_MSG_GET_PF.
 * \param MNEMO is the mnemonic of the portfolio.
 * \param STAMP is the time stamp at which positions have been recorded. */
DECLF dbobj_t be_sql_get_tag(dbconn_t, const char *mnemo, time_t stamp);

/**
 * Free resources associated with TAG as obtained by `be_sql_new_tag()'. */
DECLF void be_sql_free_tag(dbconn_t, dbobj_t tag);

/**
 * Corresponds to the iteration part of UMPF_MSG_SET_PF.
 * \param TAG is the portfolio tag as obtained by `be_sql_new_tag()'.
 * \param MNEMO is the mnemonic of the security.
 * \param L is the long side of the position.
 * \param S is the short side of the position. */
DECLF void
be_sql_set_pos(dbconn_t, dbobj_t tag, const char *mnemo, double l, double s);

/**
 * Return the number of positions in the tag TAG. */
DECLF size_t be_sql_get_npos(dbconn_t c, dbobj_t tag);

/**
 * Retrieve positions in TAG calling a callback for each,
 * first argument of which is the security mnemonic, second and third are
 * long and short position respectively and fourth is a closure pointer.
 * Iterating over the result set is stopped once the callback returns a
 * non-0 value.
 * Free()ing the mnemonic string is up to the caller. */
DECLF void
be_sql_get_pos(
	dbconn_t conn, dbobj_t tag,
	int(*cb)(char*, double, double, void*), void *clo);

#endif	/* INCLUDED_be_sql_h_ */
