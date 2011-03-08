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

#include "umpf.h"
#include "nifty.h"

/* get rid of libev guts */
#if defined HARD_INCLUDE_con6ity
# undef DECLF
# undef DECLF_W
# undef DEFUN
# undef DEFUN_W
# define DECLF		static
# define DEFUN		static
# define DECLF_W	static
# define DEFUN_W	static
#endif	/* HARD_INCLUDE_con6ity */
#include "con6ity.h"

/* get rid of sql guts */
#if defined HARD_INCLUDE_be_sql
# undef DECLF
# undef DEFUN
# define DECLF		static
# define DEFUN		static
#endif	/* HARD_INCLUDE_be_sql */
#include "be-sql.h"

#define MOD_PRE		"mod/umpf"

/* global database connexion object */
static dbconn_t umpf_dbconn;


/* connexion<->proto glue */
static void
interpret_msg(int fd, umpf_msg_t msg)
{
#if defined DEBUG_FLAG
	umpf_print_msg(STDERR_FILENO, msg);
#endif	/* DEBUG_FLAG */

	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF: {
		const char *mnemo, *descr;

		UMPF_DEBUG(MOD_PRE ": new_pf();\n");
		mnemo = msg->new_pf.name;
		descr = msg->new_pf.satellite;
		be_sql_new_pf(umpf_dbconn, mnemo, descr);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		umpf_print_msg(fd, msg);
		break;
	}
	case UMPF_MSG_GET_PF: {
		const char *mnemo;
		uint64_t pf_id;

		UMPF_DEBUG(MOD_PRE ": get_pf();\n");
		mnemo = msg->pf.name;
		pf_id = be_sql_get_pf_id(umpf_dbconn, mnemo);
		UMPF_DEBUG(MOD_PRE ": <- %lu\n", pf_id);
		break;
	}
	case UMPF_MSG_SET_PF: {
		const char *mnemo;
		time_t stamp;
		dbobj_t tag_id;

		UMPF_DEBUG(MOD_PRE ": set_pf();\n");
		mnemo = msg->pf.name;
		stamp = msg->pf.stamp;
		tag_id = be_sql_new_tag(umpf_dbconn, mnemo, stamp);

		for (size_t i = 0; i < msg->pf.nposs; i++) {
			const char *sec = msg->pf.poss[i].instr;
			double l = msg->pf.poss[i].qty->_long;
			double s = msg->pf.poss[i].qty->_shrt;
			be_sql_set_pos(umpf_dbconn, tag_id, sec, l, s);
		}

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		umpf_print_msg(fd, msg);
		break;
	}
	default:
		UMPF_DEBUG(MOD_PRE ": unknown message %u\n", msg->hdr.mt);
		break;
	}
	return;
}

/**
 * Take the stuff in MSG of size MSGLEN coming from FD and process it.
 * Return values <0 cause the handler caller to close down the socket. */
DEFUN int
handle_data(umpf_conn_t ctx, char *msg, size_t msglen)
{
	umpf_ctx_t p = get_fd_data(ctx);
	umpf_msg_t umsg;

	msg[msglen] = '\0';
	UMPF_DEBUG(MOD_PRE "/ctx: %p %zu\n%s\n", ctx, msglen, msg);

	if ((umsg = umpf_parse_blob_r(&p, msg, msglen)) != NULL) {
		/* definite success */
		interpret_msg(get_fd(ctx), umsg);
		umpf_free_msg(umsg);

	} else if (/* umsg == NULL && */ctx == NULL) {
		/* error occurred */
		UMPF_DEBUG(MOD_PRE ": ERROR\n");
	} else {
		UMPF_DEBUG(MOD_PRE ": need more grub\n");
	}
	put_fd_data(ctx, p);
	return 0;
}
#define HAVE_handle_data

DEFUN void
handle_close(umpf_conn_t ctx)
{
	umpf_ctx_t p;

	UMPF_DEBUG("forgetting about %d\n", get_fd(ctx));
	if ((p = get_fd_data(ctx)) != NULL) {
		/* finalise the push parser to avoid mem leaks */
		umpf_msg_t msg = umpf_parse_blob_r(&p, ctx, 0);

		if (UNLIKELY(msg != NULL)) {
			/* sigh */
			umpf_free_msg(msg);
		}
	}
	put_fd_data(ctx, NULL);
	return;
}
#define HAVE_handle_close


/* our connectivity cruft */
#if defined HARD_INCLUDE_con6ity
# include "con6ity.c"
#endif	/* HARD_INCLUDE_con6ity */


/* our database connexion */
#if defined HARD_INCLUDE_be_sql
# include "be-sql.c"
#endif	/* HARD_INCLUDE_be_sql */


static int
umpf_init_uds_sock(const char **sock_path, ud_ctx_t ctx, void *settings)
{
	volatile int res = -1;

	udcfg_tbl_lookup_s(sock_path, ctx, settings, "sock");
	if (*sock_path != NULL &&
	    (res = conn_listener_uds(*sock_path)) > 0) {
		/* set up the IO watcher and timer */
		init_conn_watchers(ctx->mainloop, res);
	} else {
		/* make sure we don't accidentally delete arbitrary files */
		*sock_path = NULL;
	}
	return res;
}

static int
umpf_init_net_sock(ud_ctx_t ctx, void *settings)
{
	volatile int res = -1;
	int port;

	port = udcfg_tbl_lookup_i(ctx, settings, "port");
	if (port &&
	    (res = conn_listener_net(port)) > 0) {
		/* set up the IO watcher and timer */
		init_conn_watchers(ctx->mainloop, res);
	}
	return res;
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
static volatile int umpf_sock_net = -1;
static volatile int umpf_sock_uds = -1;
/* path to unix domain socket */
const char *umpf_sock_path;

void
init(void *clo)
{
	ud_ctx_t ctx = clo;
	void *settings;

	UMPF_DEBUG(MOD_PRE ": loading ...");

	/* glue to lua settings */
	if ((settings = udctx_get_setting(ctx)) == NULL) {
		UMPF_DBGCONT("failed\n");
		return;
	}
	UMPF_DBGCONT("\n");
	/* obtain the unix domain sock from our settings */
	umpf_sock_uds = umpf_init_uds_sock(&umpf_sock_path, ctx, settings);
	/* obtain port number for our network socket */
	umpf_sock_net = umpf_init_net_sock(ctx, settings);
	/* connect to our database */
	umpf_dbconn = umpf_init_be_sql(ctx, settings);

	UMPF_DEBUG(MOD_PRE ": ... loaded\n");

	/* clean up */
	udctx_set_setting(ctx, NULL);
	return;
}

void
reinit(void *UNUSED(clo))
{
	UMPF_DEBUG(MOD_PRE ": reloading ...done\n");
	return;
}

void
deinit(void *clo)
{
	ud_ctx_t ctx = clo;

	UMPF_DEBUG(MOD_PRE ": unloading ...");
	deinit_conn_watchers(ctx->mainloop);
	umpf_sock_net = -1;
	umpf_sock_uds = -1;
	/* unlink the unix domain socket */
	if (umpf_sock_path != NULL) {
		unlink(umpf_sock_path);
	}
	/* close our db connection */
	if (umpf_dbconn) {
		be_sql_close(umpf_dbconn);
	}
	UMPF_DBGCONT("done\n");
	return;
}

/* dso-umpf.c ends here */
