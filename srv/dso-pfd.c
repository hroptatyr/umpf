/*** dso-pfd.c -- portfolio daemon
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

#include "nifty.h"

#define MOD_PRE		"mod/pfd"

/* some forwards and globals */
static int handle_data(int fd, char *msg, size_t msglen);
static void handle_close(int fd);


/* message buffer */
static char mbuf[1048576], *mptr;

static void
reset(void)
{
	mptr = mbuf;
	return;
}

/**
 * Take the stuff in MSG of size MSGLEN coming from FD and process it.
 * Return values <0 cause the handler caller to close down the socket. */
static int
handle_data(int fd, char *msg, size_t msglen)
{
	return 0;
}

static void
handle_close(int fd)
{
	/* delete fd from our htpush cache */
	PFD_DEBUG("forgetting about %d\n", fd);
	return;
}


/* our connectivity cruft */
#include "con6ity.c"

static volatile int pfd_sock_net = -1;
static volatile int pfd_sock_uds = -1;
/* path to unix domain socket */
const char *pfd_sock_path;


/* unserding bindings */
void
init(void *clo)
{
	ud_ctx_t ctx = clo;
	void *settings;
	/* port to assign to */
	int port;

	PFD_DEBUG(MOD_PRE ": loading ...");

	/* glue to lua settings */
	if ((settings = udctx_get_setting(ctx)) == NULL) {
		PFD_DBGCONT("failed\n");
		return;
	}
	PFD_DBGCONT("\n");
	/* obtain the unix domain sock from our settings */
	udcfg_tbl_lookup_s(&pfd_sock_path, ctx, settings, "sock");
	if (pfd_sock_path != NULL &&
	    (pfd_sock_uds = listener_uds(pfd_sock_path)) > 0) {
		/* set up the IO watcher and timer */
		init_watchers(ctx->mainloop, pfd_sock_uds);
	} else {
		/* make sure we don't accidentally delete arbitrary files */
		pfd_sock_path = NULL;
	}
	/* obtain port number for our network socket */
	port = udcfg_tbl_lookup_i(ctx, settings, "port");
	if (port &&
	    (pfd_sock_net = listener_net(port)) > 0) {
		/* set up the IO watcher and timer */
		init_watchers(ctx->mainloop, pfd_sock_net);
	}
	PFD_DEBUG(MOD_PRE ": ... loaded\n");

	/* clean up */
	udctx_set_setting(ctx, NULL);
	return;
}

void
reinit(void *UNUSED(clo))
{
	PFD_DEBUG(MOD_PRE ": reloading ...done\n");
	return;
}

void
deinit(void *clo)
{
	ud_ctx_t ctx = clo;

	PFD_DEBUG(MOD_PRE ": unloading ...");
	deinit_watchers(ctx->mainloop);
	pfd_sock_net = -1;
	pfd_sock_uds = -1;
	/* unlink the unix domain socket */
	if (pfd_sock_path != NULL) {
		unlink(pfd_sock_path);
	}
	PFD_DBGCONT("done\n");
	return;
}

/* dso-pfd.c ends here */
