/*** dso-umpf.c -- portfolio daemon
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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <unserding/unserding-ctx.h>
#include <unserding/unserding-cfg.h>
#include <unserding/module.h>
#include <unserding/tcp-unix.h>

#include "umpf.h"
#include "nifty.h"

/* get rid of sql guts */
#if defined HARD_INCLUDE_be_sql
# undef DECLF
# undef DEFUN
# define DECLF		static
# define DEFUN		static
#endif	/* HARD_INCLUDE_be_sql */
#include "be-sql.h"

/* we assume unserding with logger feature */
#define UMPF_INFO_LOG(args...)	UD_SYSLOG(LOG_INFO, UMPF_MOD " " args)
#define UMPF_ERR_LOG(args...)	UD_SYSLOG(LOG_ERR, UMPF_MOD " ERROR " args)
#define UMPF_CRIT_LOG(args...)	UD_SYSLOG(LOG_CRIT, UMPF_MOD " CRITICAL " args)

/* auto-sparsity assumes that positions set are just a subset of the whole
 * portfolio, and any positions not mentioned are taken over from the
 * previous tag */
#define UMPF_AUTO_SPARSE	1
/* auto pruning assumes that 0 positions are not to be repeated
 * in copy operations, CAREFUL, this undermines the idea of genericity
 * for instance when interest rates or price data is captured */
#define UMPF_AUTO_PRUNE		1

/* global database connexion object */
static dbconn_t umpf_dbconn;


/* connexion<->proto glue */
static int
get_cb(char *mnemo, double l, double s, void *clo)
{
	umpf_msg_t msg = clo;
	size_t idx = msg->pf.nposs;

	UMPF_DEBUG(MOD_PRE ": %s %2.4f %2.4f\n", mnemo, l, s);
	msg->pf.poss[idx].ins->sym = mnemo;
	msg->pf.poss[idx].qty->_long = l;
	msg->pf.poss[idx].qty->_shrt = s;
	msg->pf.nposs++;
	/* don't stop on our kind, request more grub */
	return 0;
}

static int
lst_cb(char *mnemo, void *clo)
{
	umpf_msg_t *msg = clo;

	UMPF_DEBUG(MOD_PRE ": %s\n", mnemo);
	if (((*msg)->lst_pf.npfs % 16) == 0) {
		/* resize */
		*msg = realloc(
			*msg,
			sizeof(**msg) +
			((*msg)->lst_pf.npfs + 16) *
			sizeof(*(*msg)->lst_pf.pfs));
	}
	(*msg)->lst_pf.pfs[(*msg)->lst_pf.npfs++] = mnemo;
	/* don't stop on our kind, request more grub */
	return 0;
}

static int
wr_fin_cb(ud_conn_t UNUSED(c), char *buf, size_t UNUSED(bsz), void *UNUSED(d))
{
	UMPF_DEBUG(MOD_PRE ": finished writing buf %p\n", buf);
	free(buf);
	return 0;
}

static size_t
interpret_msg(char **buf, umpf_msg_t msg)
{
	size_t len;

#if defined DEBUG_FLAG && 0
	umpf_print_msg(STDERR_FILENO, msg);
#endif	/* DEBUG_FLAG */

	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF:
	case UMPF_MSG_SET_DESCR: {
		const char *mnemo = msg->new_pf.name;
		const struct __satell_s *descr = msg->new_pf.satellite;
		dbobj_t pf;

		UMPF_INFO_LOG("new_pf()/set_descr();\n");
		pf = be_sql_new_pf(umpf_dbconn, mnemo, descr[0]);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_pf(umpf_dbconn, pf);
		break;
	}
	case UMPF_MSG_GET_DESCR: {
		const char *mnemo;

		UMPF_INFO_LOG("get_descr();\n");
		mnemo = msg->new_pf.name;
		if (msg->new_pf.satellite->data != NULL) {
			free(msg->new_pf.satellite->data);
		}
		msg->new_pf.satellite[0] = be_sql_get_descr(umpf_dbconn, mnemo);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);
		break;
	}
	case UMPF_MSG_LST_PF:
		UMPF_INFO_LOG("lst_pf();\n");
		be_sql_lst_pf(umpf_dbconn, lst_cb, &msg);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);
		break;

	case UMPF_MSG_GET_PF: {
		const char *mnemo;
		time_t stamp;
		dbobj_t tag;
		size_t npos;

		UMPF_INFO_LOG("get_pf();\n");
		mnemo = msg->pf.name;
		stamp = msg->pf.stamp;

		tag = be_sql_get_tag(umpf_dbconn, mnemo, stamp);
		if (LIKELY(tag != NULL)) {
			/* set correct tag stamp */
			msg->pf.stamp = be_sql_get_stamp(umpf_dbconn, tag);
			/* get the number of positions */
			npos = be_sql_get_npos(umpf_dbconn, tag);
			UMPF_DEBUG(MOD_PRE ": found %zu positions\n", npos);

			msg = umpf_msg_add_pos(msg, npos);
			msg->pf.nposs = 0;
			be_sql_get_pos(umpf_dbconn, tag, get_cb, msg);
		}

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_tag(umpf_dbconn, tag);
		break;
	}
	case UMPF_MSG_SET_PF: {
		const char *mnemo;
		time_t stamp;
		dbobj_t tag;

		UMPF_DEBUG(MOD_PRE ": set_pf();\n");
		mnemo = msg->pf.name;
		stamp = msg->pf.stamp;	
#if defined UMPF_AUTO_SPARSE
		tag = be_sql_copy_tag(umpf_dbconn, mnemo, stamp);
#else  /* !UMPF_AUTO_SPARSE */
		tag = be_sql_new_tag(umpf_dbconn, mnemo, stamp);
#endif	/* UMPF_AUTO_SPARSE */

		for (size_t i = 0; i < msg->pf.nposs; i++) {
			const char *sec = msg->pf.poss[i].ins->sym;
			double l = msg->pf.poss[i].qty->_long;
			double s = msg->pf.poss[i].qty->_shrt;
			be_sql_set_pos(umpf_dbconn, tag, sec, l, s);
		}

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_tag(umpf_dbconn, tag);
		break;
	}
	case UMPF_MSG_NEW_SEC: {
		const char *pf_mnemo = msg->new_sec.pf_mnemo;
		const char *sec_mnemo = msg->new_sec.ins->sym;
		const struct __satell_s *descr = msg->new_sec.satellite;
		dbobj_t sec;

		UMPF_DEBUG(MOD_PRE ": new_sec();\n");
		sec = be_sql_new_sec(umpf_dbconn, pf_mnemo, sec_mnemo, *descr);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_sec(umpf_dbconn, sec);
		break;
	}
	case UMPF_MSG_SET_SEC: {
		const char *pf_mnemo = msg->new_sec.pf_mnemo;
		const char *sec_mnemo = msg->new_sec.ins->sym;
		const struct __satell_s *descr = msg->new_sec.satellite;
		dbobj_t sec;

		UMPF_DEBUG(MOD_PRE ": set_sec();\n");
		sec = be_sql_set_sec(umpf_dbconn, pf_mnemo, sec_mnemo, *descr);

		/* reuse the message to send the answer,
		 * we should check if SEC is a valid sec-id actually and
		 * send an error otherwise */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_sec(umpf_dbconn, sec);
		break;
	}
	case UMPF_MSG_GET_SEC: {
		const char *pf_mnemo;
		const char *sec_mnemo;

		UMPF_DEBUG(MOD_PRE ": get_sec();\n");
		pf_mnemo = msg->new_sec.pf_mnemo;
		sec_mnemo = msg->new_sec.ins->sym;
		if (msg->new_sec.satellite->data != NULL) {
			free(msg->new_sec.satellite->data);
		}
		msg->new_sec.satellite[0] =
			be_sql_get_sec(umpf_dbconn, pf_mnemo, sec_mnemo);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);
		break;
	}
	case UMPF_MSG_PATCH: {
		/* replay position changes */
		const char *mnemo;
		time_t stamp;
		dbobj_t tag;
		size_t res_nposs = 0;

		UMPF_DEBUG(MOD_PRE ": patch();\n");
		mnemo = msg->pf.name;
		stamp = msg->pf.stamp;
		tag = be_sql_copy_tag(umpf_dbconn, mnemo, stamp);

		for (size_t i = 0, j; i < msg->pf.nposs; i++) {
			const char *sec = msg->pf.poss[i].ins->sym;
			double v = msg->pf.poss[i].qsd->pos;
			double l;
			double s;

			switch (msg->pf.poss[i].qsd->sd) {
			case QSIDE_OPEN_LONG:
				l = v;
				s = 0;
				break;
			case QSIDE_CLOSE_LONG:
				l = -v;
				s = 0;
				break;
			case QSIDE_OPEN_SHORT:
				l = 0;
				s = v;
				break;
			case QSIDE_CLOSE_SHORT:
				l = 0;
				s = -v;
				break;
			case QSIDE_UNK:
			default:
				continue;
			}
#define P	msg->pf.poss
			/* condense the resulting position report */
			for (j = 0; j < res_nposs; j++) {
				if (!strcmp(P[j].ins->sym, P[i].ins->sym)) {
					break;
				}
			}
			/* re-assign to j-th slot */
			P[j].ins->sym = P[i].ins->sym;
			*P[j].qty = be_sql_add_pos(umpf_dbconn, tag, sec, l, s);
			/* set new nposs value */
			if (j >= res_nposs) {
				res_nposs = j + 1;
			}
#undef P
		}

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		msg->pf.nposs = res_nposs;
		len = umpf_seria_msg(buf, 0, msg);

		/* free resources */
		be_sql_free_tag(umpf_dbconn, tag);
		break;
	}
	default:
		UMPF_DEBUG(MOD_PRE ": unknown message %u\n", msg->hdr.mt);
		umpf_set_msg_type(msg, UMPF_MSG_UNK);
		len = umpf_seria_msg(buf, 0, msg);
		break;
	}
	/* free 'im 'ere */
	umpf_free_msg(msg);
	return len;
}

/**
 * Take the stuff in MSG of size MSGLEN coming from FD and process it.
 * Return values <0 cause the handler caller to close down the socket. */
static int
handle_data(ud_conn_t c, char *msg, size_t msglen, void *data)
{
	umpf_ctx_t p = data;
	umpf_msg_t umsg;

	UMPF_DEBUG(MOD_PRE "/ctx: %p %zu\n", c, msglen);
#if defined DEBUG_FLAG
	/* safely write msg to logerr now */
	fwrite(msg, msglen, 1, logout);
#endif	/* DEBUG_FLAG */

	if ((umsg = umpf_parse_blob_r(&p, msg, msglen)) != NULL) {
		/* definite success */
		char *buf = NULL;
		size_t len;
		ud_conn_t wr = NULL;

		/* serialise, put results in BUF*/
		if ((len = interpret_msg(&buf, umsg)) &&
		    (wr = ud_write_soon(c, buf, len, wr_fin_cb))) {
			UMPF_DEBUG(
				MOD_PRE ": installing buf wr'er %p %p\n",
				wr, buf);
			ud_conn_put_data(wr, buf);
			return 0;
		}
		p = NULL;

	} else if (/* umsg == NULL && */p == NULL) {
		/* error occurred */
		UMPF_DEBUG(MOD_PRE ": ERROR\n");
	} else {
		UMPF_DEBUG(MOD_PRE ": need more grub\n");
	}
	ud_conn_put_data(c, p);
	return 0;
}

static int
handle_close(ud_conn_t c, void *data)
{
	UMPF_DEBUG("forgetting about %p\n", c);
	if (data) {
		/* finalise the push parser to avoid mem leaks */
		umpf_msg_t msg = umpf_parse_blob_r(&data, data, 0);

		if (UNLIKELY(msg != NULL)) {
			/* sigh */
			umpf_free_msg(msg);
		}
	}
	ud_conn_put_data(c, NULL);
	return 0;
}


/* our database connexion */
#if defined HARD_INCLUDE_be_sql
# include "be-sql.c"
#endif	/* HARD_INCLUDE_be_sql */


static const char *umpf_sock_path = NULL;

static ud_conn_t
umpf_init_uds_sock(ud_ctx_t ctx, void *settings)
{
	udcfg_tbl_lookup_s(&umpf_sock_path, ctx, settings, "sock");
	return make_unix_conn(umpf_sock_path, handle_data, handle_close, NULL);
}

static ud_conn_t
umpf_init_net_sock(ud_ctx_t ctx, void *settings)
{
	int port;

	port = udcfg_tbl_lookup_i(ctx, settings, "port");
	return make_tcp_conn(port, handle_data, handle_close, NULL);
}

static dbconn_t
umpf_init_be_sql(ud_ctx_t ctx, void *s)
{
	const char *host;
	const char *user;
	const char *pass;
	const char *sch;
	const char *db_file;
	void *db;
	dbconn_t conn;

	if ((udcfg_tbl_lookup_s(&db_file, ctx, s, "db"), db_file) != NULL) {
		/* must be sqlite then */
		return be_sql_open(NULL, NULL, NULL, db_file);

	} else if ((db = udcfg_tbl_lookup(ctx, s, "db")) == NULL) {
		/* must be bollocks */
		return NULL;
	}
	/* otherwise it's a group of specs */
	if ((udcfg_tbl_lookup_s(&db_file, ctx, db, "file"), db_file) != NULL) {
		/* sqlite again */
		conn = be_sql_open(NULL, NULL, NULL, db_file);

	} else if ((udcfg_tbl_lookup_s(&host, ctx, db, "host"), host) &&
		   (udcfg_tbl_lookup_s(&user, ctx, db, "user"), user) &&
		   (udcfg_tbl_lookup_s(&pass, ctx, db, "pass"), pass) &&
		   (udcfg_tbl_lookup_s(&sch, ctx, db, "schema"), sch)) {
		/* must be mysql */
		conn = be_sql_open(host, user, pass, sch);

	} else {
		/* must be utter bollocks */
		conn = NULL;
	}
	/* free our settings */
	udcfg_tbl_free(ctx, db);
	return conn;
}


/* unserding bindings */
static ud_conn_t __cnet = NULL;
static ud_conn_t __cuds = NULL;
void *umpf_logout;

void
init(void *clo)
{
	ud_ctx_t ctx = clo;
	void *settings;

	UMPF_INFO_LOG("loading aou-umpf module");

	/* glue to lua settings */
	if ((settings = udctx_get_setting(ctx)) == NULL) {
		UMPF_ERR_LOG("settings could not be read\n");
		return;
	}
	/* obtain the unix domain sock from our settings */
	__cuds = umpf_init_uds_sock(ctx, settings);
	/* obtain port number for our network socket */
	__cnet = umpf_init_net_sock(ctx, settings);
	/* connect to our database */
	umpf_dbconn = umpf_init_be_sql(ctx, settings);

	UMPF_INFO_LOG("successfully loaded\n");

#if defined DEBUG_FLAG
	umpf_logout = stderr;
#else  /* !DEBUG_FLAG */
	umpf_logout = fopen("/dev/null", "w");
#endif	/* DEBUG_FLAG */

	/* clean up */
	udctx_set_setting(ctx, NULL);
	return;
}

void
reinit(void *UNUSED(clo))
{
	UMPF_INFO_LOG("reloading aou-umpf module ... done");
	return;
}

void
deinit(void *UNUSED(clo))
{
	UMPF_INFO_LOG("unloading aou-umpf module");
	if (__cnet) {
		ud_conn_fini(__cnet);
	}
	if (__cuds) {
		ud_conn_fini(__cuds);
	}
	/* unlink the unix domain socket */
	if (umpf_sock_path != NULL) {
		unlink(umpf_sock_path);
	}
	/* close our db connection */
	if (umpf_dbconn) {
		be_sql_close(umpf_dbconn);
	}
	fclose(umpf_logout);
	UMPF_INFO_LOG("aou-umpf successfully unloaded");
	return;
}

/* dso-umpf.c ends here */
