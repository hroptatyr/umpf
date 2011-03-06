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

#define MOD_PRE		"mod/umpf"


/* connexion<->proto glue */
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
		umpf_print_msg(umsg, stdout);
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

static volatile int umpf_sock_net = -1;
static volatile int umpf_sock_uds = -1;
/* path to unix domain socket */
const char *umpf_sock_path;


/* unserding bindings */
void
init(void *clo)
{
	ud_ctx_t ctx = clo;
	void *settings;
	/* port to assign to */
	int port;

	UMPF_DEBUG(MOD_PRE ": loading ...");

	/* glue to lua settings */
	if ((settings = udctx_get_setting(ctx)) == NULL) {
		UMPF_DBGCONT("failed\n");
		return;
	}
	UMPF_DBGCONT("\n");
	/* obtain the unix domain sock from our settings */
	udcfg_tbl_lookup_s(&umpf_sock_path, ctx, settings, "sock");
	if (umpf_sock_path != NULL &&
	    (umpf_sock_uds = conn_listener_uds(umpf_sock_path)) > 0) {
		/* set up the IO watcher and timer */
		init_conn_watchers(ctx->mainloop, umpf_sock_uds);
	} else {
		/* make sure we don't accidentally delete arbitrary files */
		umpf_sock_path = NULL;
	}
	/* obtain port number for our network socket */
	port = udcfg_tbl_lookup_i(ctx, settings, "port");
	if (port &&
	    (umpf_sock_net = conn_listener_net(port)) > 0) {
		/* set up the IO watcher and timer */
		init_conn_watchers(ctx->mainloop, umpf_sock_net);
	}
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
	UMPF_DBGCONT("done\n");
	return;
}

/* dso-umpf.c ends here */
