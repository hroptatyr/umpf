/*** umpfd.c -- portfolio daemon
 *
 * Copyright (C) 2011-2013  Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
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
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#if defined HAVE_EV_H
# include <ev.h>
# undef EV_P
# define EV_P  struct ev_loop *loop __attribute__((unused))
#endif	/* HAVE_EV_H */

#include "umpf.h"
#include "logger.h"
#include "configger.h"
#include "ud-sock.h"
#include "ud-sockaddr.h"
#include "gq.h"
#include "nifty.h"

#if !defined IPPROTO_IPV6
# error umpf needs ipv6 to work
#endif	/* IPPROTO_IPV6 */

/* get rid of sql guts */
#if defined HARD_INCLUDE_be_sql
# undef DECLF
# undef DEFUN
# define DECLF		static
# define DEFUN		static
#endif	/* HARD_INCLUDE_be_sql */
#include "be-sql.h"

#define UMPF_MOD		"[mod/gand]"
#define UMPF_INFO_LOG(args...)				\
	do {						\
		UMPF_SYSLOG(LOG_INFO, UMPF_MOD " " args);	\
		UMPF_DEBUG("INFO " args);		\
	} while (0)
#define UMPF_ERR_LOG(args...)					\
	do {							\
		UMPF_SYSLOG(LOG_ERR, UMPF_MOD " ERROR " args);	\
		UMPF_DEBUG("ERROR " args);			\
	} while (0)
#define UMPF_CRIT_LOG(args...)						\
	do {								\
		UMPF_SYSLOG(LOG_CRIT, UMPF_MOD " CRITICAL " args);	\
		UMPF_DEBUG("CRITICAL " args);				\
	} while (0)
#define UMPF_NOTI_LOG(args...)						\
	do {								\
		UMPF_SYSLOG(LOG_NOTICE, UMPF_MOD " NOTICE " args);	\
		UMPF_DEBUG("NOTICE " args);				\
	} while (0)

/* auto-sparsity assumes that positions set are just a subset of the whole
 * portfolio, and any positions not mentioned are taken over from the
 * previous tag */
#define UMPF_AUTO_SPARSE	1
/* auto pruning assumes that 0 positions are not to be repeated
 * in copy operations, CAREFUL, this undermines the idea of genericity
 * for instance when interest rates or price data is captured */
#define UMPF_AUTO_PRUNE		1


/* the connection queue */
typedef struct ev_io_q_s *ev_io_q_t;
typedef struct ev_qio_s *ev_qio_t;

struct ev_io_q_s {
	struct gq_s q[1];
};

struct ev_qio_s {
	struct gq_item_s i;
	ev_io w[1];
	/* ctx used for blob parser */
	union {
		umpf_ctx_t ctx;
		size_t nwr;
	};
	/* reply buffer, pointer and size */
	char *rsp;
	size_t rsz;
};

/* global database connexion object */
static dbconn_t umpf_dbconn;


/* aux */
#include "gq.c"

static struct ev_io_q_s ioq = {0};

static ev_qio_t
make_qio(void)
{
	ev_qio_t res;

	if (ioq.q->free->i1st == NULL) {
		size_t nitems = ioq.q->nitems / sizeof(*res);

		assert(ioq.q->free->ilst == NULL);
		UMPF_DEBUG("IOQ RESIZE -> %zu\n", nitems + 16);
		init_gq(ioq.q, sizeof(*res), nitems + 16);
		/* we don't need to rebuild our items */
	}
	/* get us a new client and populate the object */
	res = (void*)gq_pop_head(ioq.q->free);
	memset(res, 0, sizeof(*res));
	return res;
}

static void
free_io(ev_qio_t io)
{
	gq_push_tail(ioq.q->free, (gq_item_t)io);
	return;
}

static void
ev_io_shut(EV_P_ ev_io *w)
{
	int fd = w->fd;

	ev_io_stop(EV_A_ w);
	shutdown(fd, SHUT_RDWR);
	close(fd);
	w->fd = -1;
	return;
}

static void
ev_qio_shut(EV_P_ ev_io *w)
{
/* attention, W *must* come from the ev io queue */
	ev_qio_t qio = w->data;

	ev_io_shut(EV_A_ w);
	free_io(qio);
	return;
}

static void
log_conn(int fd, ud_const_sockaddr_t sa)
{
	static char abuf[INET6_ADDRSTRLEN];
	short unsigned int p;

	ud_sockaddr_ntop(abuf, sizeof(abuf), sa);
	p = ud_sockaddr_port(sa);
	UMPF_INFO_LOG(":sock %d connect :from [%s]:%d\n", fd, abuf, p);
	return;
}


/* stuff from former unserding */
static int
make_unix_conn(const char *path)
{
	static struct sockaddr_un __s = {
		.sun_family = AF_LOCAL,
	};
	volatile int s;
	size_t sz;

	if (UNLIKELY((s = socket(PF_LOCAL, SOCK_STREAM, 0)) < 0)) {
		return s;
	}

	/* bind a name now */
	strncpy(__s.sun_path, path, sizeof(__s.sun_path));
	__s.sun_path[sizeof(__s.sun_path) - 1] = '\0';

	/* The size of the address is
	   the offset of the start of the filename,
	   plus its length,
	   plus one for the terminating null byte.
	   Alternatively you can just do:
	   size = SUN_LEN (&name); */
	sz = offsetof(struct sockaddr_un, sun_path) + strlen(__s.sun_path) + 1;

	/* just unlink the socket */
	unlink(path);
	/* we used to retry upon failure, but who cares */
	if (bind(s, (struct sockaddr*)&__s, sz) < 0) {
		goto fuck;
	}
	if (listen(s, 2) < 0) {
		goto fuck;
	}
	/* allow the whole world to connect to us */
	chmod(path, 0777);
	return s;

fuck:
	close(s);
	unlink(path);
	return -1;
}

static int
make_tcp_conn(uint16_t port)
{
	struct sockaddr_in6 __sa6 = {
		.sin6_family = AF_INET6,
		.sin6_addr = IN6ADDR_ANY_INIT,
		.sin6_port = htons(port),
	};
	volatile int s;

	if (UNLIKELY((s = socket(PF_INET6, SOCK_STREAM, 0)) < 0)) {
		return -1;
	}

#if defined IPV6_V6ONLY
	setsockopt_int(s, IPPROTO_IPV6, IPV6_V6ONLY, 0);
#endif	/* IPV6_V6ONLY */
#if defined IPV6_USE_MIN_MTU
	/* use minimal mtu */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_USE_MIN_MTU, 1);
#endif
#if defined IPV6_DONTFRAG
	/* rather drop a packet than to fragment it */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_DONTFRAG, 1);
#endif
#if defined IPV6_RECVPATHMTU
	/* obtain path mtu to send maximum non-fragmented packet */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_RECVPATHMTU, 1);
#endif
	setsock_reuseaddr(s);
	setsock_reuseport(s);

	/* we used to retry upon failure, but who cares */
	if (bind(s, (struct sockaddr*)&__sa6, sizeof(__sa6)) < 0 ||
	    listen(s, 2) < 0) {
		close(s);
		return -1;
	}
	return s;
}

static void
write_pidfile(const char *pidfile)
{
	char str[32];
	pid_t pid;
	size_t len;
	int fd;

	if ((pid = getpid()) &&
	    (len = snprintf(str, sizeof(str) - 1, "%d\n", pid)) &&
	    (fd = open(pidfile, O_RDWR | O_CREAT | O_TRUNC, 0644)) >= 0) {
		write(fd, str, len);
		close(fd);
	} else {
		UMPF_ERR_LOG("Could not write pid file %s\n", pidfile);
	}
	return;
}


/* connexion<->proto glue */
static int
get_cb(char *mnemo, double l, double s, void *clo)
{
	umpf_msg_t msg = clo;
	size_t idx = msg->pf.nposs;

	UMPF_DEBUG("%s %2.4f %2.4f\n", mnemo, l, s);
	msg->pf.poss[idx].ins->sym = mnemo;
	msg->pf.poss[idx].qty->_long = l;
	msg->pf.poss[idx].qty->_shrt = s;
	msg->pf.nposs++;
	/* don't stop on our kind, request more grub */
	return 0;
}

static int
lst_pf_cb(char *mnemo, void *clo)
{
	umpf_msg_t *msg = clo;

	UMPF_DEBUG("%s\n", mnemo);
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
lst_tag_cb(uint64_t tid, time_t tm, void *clo)
{
	umpf_msg_t *msg = clo;
	size_t idx = (*msg)->lst_tag.ntags;

	if ((idx % 512) == 0) {
		/* resize */
		*msg = realloc(
			*msg,
			sizeof(**msg) +
			((*msg)->lst_tag.ntags + 512) *
			sizeof(*(*msg)->lst_tag.tags));
	}
	(*msg)->lst_tag.tags[idx].id = tid;
	(*msg)->lst_tag.tags[idx].stamp = tm;
	(*msg)->lst_tag.ntags++;
	/* don't stop on our kind, request more grub */
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
		be_sql_lst_pf(umpf_dbconn, lst_pf_cb, &msg);

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
			tag_t tid = be_sql_tag_get_id(umpf_dbconn, tag);

			/* set correct tag stamp */
			msg->pf.stamp = be_sql_get_stamp(umpf_dbconn, tag);
			msg->pf.tag_id = tid;
			/* get the number of positions */
			npos = be_sql_get_npos(umpf_dbconn, tag);
			UMPF_DEBUG("found %zu positions for %lu\n", npos, tid);

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

		UMPF_DEBUG("set_pf();\n");
		mnemo = msg->pf.name;
		stamp = msg->pf.stamp;	
#if defined UMPF_AUTO_SPARSE
		tag = be_sql_copy_tag(umpf_dbconn, mnemo, stamp);
#else  /* !UMPF_AUTO_SPARSE */
		tag = be_sql_new_tag(umpf_dbconn, mnemo, stamp);
#endif	/* UMPF_AUTO_SPARSE */
		msg->pf.tag_id = be_sql_tag_get_id(umpf_dbconn, tag);

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

		UMPF_DEBUG("new_sec();\n");
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

		UMPF_DEBUG("set_sec();\n");
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

		UMPF_DEBUG("get_sec();\n");
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

		UMPF_DEBUG("patch();\n");
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
	case UMPF_MSG_LST_TAG:
		UMPF_INFO_LOG("lst_tag();\n");
		be_sql_lst_tag(
			umpf_dbconn, msg->lst_tag.name, lst_tag_cb, &msg);

		/* reuse the message to send the answer */
		msg->hdr.mt++;
		len = umpf_seria_msg(buf, 0, msg);
		break;
	default:
		UMPF_DEBUG("unknown message %u\n", msg->hdr.mt);
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
handle_data(ev_qio_t qio, char *msg, size_t msglen)
{
	umpf_ctx_t p = qio->ctx;
	umpf_msg_t umsg;

	UMPF_DEBUG("/ctx: %p %zu\n", p, msglen);
#if defined DEBUG_FLAG
	/* safely write msg to logerr now */
	fwrite(msg, msglen, 1, umpf_logout);
#endif	/* DEBUG_FLAG */

	if ((umsg = umpf_parse_blob_r(&p, msg, msglen)) != NULL) {
		/* definite success */
		char *buf = NULL;
		size_t len;

		/* serialise, put results in BUF*/
		if ((len = interpret_msg(&buf, umsg))) {
			qio->rsp = buf;
			qio->rsz = len;
			UMPF_DEBUG("requesting write buffer\n");
		}
		qio->ctx = NULL;
		/* request connection close and writing of data */
		return 1;

	} else if (/* umsg == NULL && */p == NULL) {
		/* error occurred */
		UMPF_DEBUG("ERROR\n");
		qio->ctx = NULL;
		/* request connection close */
		return -1;
	}
	UMPF_DEBUG("need more grub\n");
	qio->ctx = p;
	return 0;
}

static int
handle_close(ev_qio_t qio)
{
	umpf_ctx_t p = qio->ctx;

	UMPF_DEBUG("forgetting about %p\n", p);
	if (p != NULL) {
		/* finalise the push parser to avoid mem leaks */
		umpf_msg_t msg = umpf_parse_blob_r(&p, p, 0);

		if (UNLIKELY(msg != NULL)) {
			/* sigh */
			umpf_free_msg(msg);
		}
		qio->ctx = p;
	}
	if (qio->rsp != NULL) {
		free(qio->rsp);
	}
	return 0;
}


/* our database connexion */
#if defined HARD_INCLUDE_be_sql
# include "be-sql.c"
#endif	/* HARD_INCLUDE_be_sql */


/* callbacks */
static void
dccp_dtwr_cb(EV_P_ ev_io *w, int UNUSED(re))
{
	ev_qio_t qio = w->data;
	ssize_t nwr;
	const char *buf = qio->rsp + qio->nwr;
	size_t bsz = qio->rsz - qio->nwr;

	if (UNLIKELY((nwr = write(w->fd, buf, bsz)) <= 0) ||
	    LIKELY((qio->nwr += nwr) >= qio->rsz)) {
		/* something's fucked or everything's written */
		qio->ctx = NULL;
		goto clo;
	}
	/* just keep a note of how much is left */
	qio->nwr += nwr;
	return;

clo:
	handle_close(qio);
	ev_qio_shut(EV_A_ w);
	return;
}

static void
dccp_data_cb(EV_P_ ev_io *w, int UNUSED(re))
{
	static char buf[4096];
	ev_qio_t qio = w->data;
	ssize_t nrd;
	ssize_t nwr;

	if (UNLIKELY((nrd = read(w->fd, buf, sizeof(buf))) <= 0)) {
		goto clo;
	} else if (LIKELY((size_t)nrd < sizeof(buf))) {
		buf[nrd] = '\0';
	} else {
		/* uh oh, mega request, wtf? */
		buf[sizeof(buf) - 1] = '\0';
	}

	/* see what the handler makes of it */
	if (handle_data(qio, buf, nrd) == 0) {
		/* connection shall not be closed */
		return;
	}
	/* check if we want stuff written */
	if (qio->rsp != NULL &&
	    (nwr = write(w->fd, qio->rsp, qio->rsz)) < (ssize_t)qio->rsz) {
		ev_qio_t qwr = make_qio();

		UMPF_DEBUG("instantiating write buffer\n");
		ev_io_init(qwr->w, dccp_dtwr_cb, w->fd, EV_WRITE);
		qwr->w->data = qwr;
		ev_io_start(EV_A_ qwr->w);

		/* we've done nwr bytes already */
		qwr->nwr = nwr > 0 ? (size_t)nwr : 0U;
		/* move rsp/rsz pair to qwr */
		qwr->rsp = qio->rsp;
		qwr->rsz = qio->rsz;
		qio->rsp = NULL;
		qio->rsz = 0UL;
	} else if (qio->rsp != NULL) {
		UMPF_DEBUG("no write buffer needed\n");
	}

clo:
	/* notify the blob parser about the conn close */
	handle_close(qio);
	ev_qio_shut(EV_A_ w);
	return;
}

static void
dccp_cb(EV_P_ ev_io *w, int UNUSED(re))
{
	union ud_sockaddr_u sa;
	socklen_t sasz = sizeof(sa);
	ev_qio_t qio;
	int s;

	if ((s = accept(w->fd, &sa.sa, &sasz)) < 0) {
		return;
	}
	log_conn(s, &sa);

	qio = make_qio();
	ev_io_init(qio->w, dccp_data_cb, s, EV_READ);
	qio->w->data = qio;
	ev_io_start(EV_A_ qio->w);
	return;
}

static void
sigint_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	UMPF_NOTI_LOG("C-c caught, unrolling everything\n");
	ev_unloop(EV_A_ EVUNLOOP_ALL);
	return;
}

static void
sigpipe_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	UMPF_NOTI_LOG("SIGPIPE caught, doing nothing\n");
	return;
}

static void
sighup_cb(EV_P_ ev_signal *UNUSED(w), int UNUSED(revents))
{
	UMPF_NOTI_LOG("SIGHUP caught, doing nothing\n");
	return;
}


/* config glue */
struct dbnfo_s {
	enum {
		DBNFO_UNK,
		DBNFO_SQLITE,
		DBNFO_MYSQL,
	} t;
	union {
		char *h;
		char *f;
	};
	char *u;
	char *p;
	char *s;
};

#define GLOB_CFG_PRE	"/etc/unserding"
#if !defined MAX_PATH_LEN
# define MAX_PATH_LEN	64
#endif	/* !MAX_PATH_LEN */

/* do me properly */
static const char cfg_glob_prefix[] = GLOB_CFG_PRE;

#if defined USE_LUA
/* that should be pretty much the only mention of lua in here */
static const char cfg_file_name[] = "umpf.lua";
#endif	/* USE_LUA */

static void
umpf_expand_user_cfg_file_name(char *tgt)
{
	char *p;
	const char *homedir = getenv("HOME");
	size_t homedirlen = strlen(homedir);

	/* get the user's home dir */
	memcpy(tgt, homedir, homedirlen);
	p = tgt + homedirlen;
	*p++ = '/';
	*p++ = '.';
	strncpy(p, cfg_file_name, sizeof(cfg_file_name));
	return;
}

static void
umpf_expand_glob_cfg_file_name(char *tgt)
{
	char *p;

	/* get the user's home dir */
	strncpy(tgt, cfg_glob_prefix, sizeof(cfg_glob_prefix));
	p = tgt + sizeof(cfg_glob_prefix);
	*p++ = '/';
	strncpy(p, cfg_file_name, sizeof(cfg_file_name));
	return;
}

static cfg_t
umpf_read_config(const char *user_cf)
{
	char cfgf[MAX_PATH_LEN];
	cfg_t cfg;

        UMPF_DEBUG("reading configuration from config file ...");

	/* we prefer the user's config file, then fall back to the
	 * global config file if that's not available */
	if (user_cf != NULL && (cfg = configger_init(user_cf)) != NULL) {
		UMPF_DBGCONT("done\n");
		return cfg;
	}

	umpf_expand_user_cfg_file_name(cfgf);
	if (cfgf != NULL && (cfg = configger_init(cfgf)) != NULL) {
		UMPF_DBGCONT("done\n");
		return cfg;
	}

	/* otherwise there must have been an error */
	umpf_expand_glob_cfg_file_name(cfgf);
	if (cfgf != NULL && (cfg = configger_init(cfgf)) != NULL) {
		UMPF_DBGCONT("done\n");
		return cfg;
	}
	UMPF_DBGCONT("failed\n");
	return NULL;
}

static void
umpf_free_config(cfg_t ctx)
{
	if (ctx != NULL) {
		configger_fini(ctx);
	}
	return;
}

/* high level */
static char*
umpf_get_sock(cfg_t ctx)
{
	cfgset_t *cs;
	size_t rsz;
	const char *res = NULL;

	if (UNLIKELY(ctx == NULL)) {
		goto dflt;
	}

	/* start out with an empty target */
	for (size_t i = 0, n = cfg_get_sets(&cs, ctx); i < n; i++) {
		if ((rsz = cfg_tbl_lookup_s(&res, ctx, cs[i], "sock"))) {
			goto out;
		}
	}

	/* otherwise try the root domain */
	if ((rsz = cfg_glob_lookup_s(&res, ctx, "sock"))) {
		goto out;
	}

	/* otherwise we'll construct it from the trolfdir */
dflt:
	return NULL;

out:
	/* make sure the return value is freeable */
	return strndup(res, rsz);
}

static uint16_t
umpf_get_port(cfg_t ctx)
{
	cfgset_t *cs;
	int res;

	if (UNLIKELY(ctx == NULL)) {
		goto dflt;
	}

	/* start out with an empty target */
	for (size_t i = 0, n = cfg_get_sets(&cs, ctx); i < n; i++) {
		if ((res = cfg_tbl_lookup_i(ctx, cs[i], "port"))) {
			goto out;
		}
	}

	/* otherwise try the root domain */
	res = cfg_glob_lookup_i(ctx, "port");

out:
	if (res > 0 && res < 65536) {
		return (uint16_t)res;
	}
dflt:
	return 0U;
}

static struct dbnfo_s
__get_dbnfo(cfg_t ctx, cfgset_t s)
{
	struct dbnfo_s res = {DBNFO_UNK};
	const char *tmp;
	void *db;

	if ((cfg_tbl_lookup_s(&tmp, ctx, s, "db"), tmp) != NULL) {
		/* must be sqlite then */
		res.f = strdup(tmp);
		res.t = DBNFO_SQLITE;
		goto out;

	} else if ((db = cfg_tbl_lookup(ctx, s, "db")) == NULL) {
		/* must be bollocks */
		goto out;

	} else if ((cfg_tbl_lookup_s(&tmp, ctx, db, "file"), tmp) != NULL) {
		/* sqlite again */
		res.f = strdup(tmp);
		res.t = DBNFO_SQLITE;

	} else {
		res.t = DBNFO_MYSQL;
		if ((cfg_tbl_lookup_s(&tmp, ctx, db, "host"), tmp)) {
			res.h = strdup(tmp);
		}
		if ((cfg_tbl_lookup_s(&tmp, ctx, db, "user"), tmp)) {
			res.u = strdup(tmp);
		}
		if ((cfg_tbl_lookup_s(&tmp, ctx, db, "pass"), tmp)) {
			res.p = strdup(tmp);
		}
		if ((cfg_tbl_lookup_s(&tmp, ctx, db, "schema"), tmp)) {
			res.s = strdup(tmp);
		}
	}
	/* free our settings */
	cfg_tbl_free(ctx, db);

out:
	return res;
}

static struct dbnfo_s
umpf_get_dbnfo(cfg_t ctx)
{
	struct dbnfo_s res = {DBNFO_UNK};
	cfgset_t *cs;

	for (size_t i = 0, n = cfg_get_sets(&cs, ctx); i < n; i++) {
		if ((res = __get_dbnfo(ctx, cs[i]), res.t) != DBNFO_UNK) {
			break;
		}
	}
	return res;
}


#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#endif	/* __INTEL_COMPILER */
#include "umpfd-clo.h"
#include "umpfd-clo.c"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#endif	/* __INTEL_COMPILER */

void *umpf_logout;

static int
daemonise(void)
{
	int fd;
	pid_t pid;

	switch (pid = fork()) {
	case -1:
		return -1;
	case 0:
		break;
	default:
		UMPF_NOTI_LOG("Successfully bore a squaller: %d\n", pid);
		exit(0);
	}

	if (setsid() == -1) {
		return -1;
	}
	for (int i = getdtablesize(); i>=0; --i) {
		/* close all descriptors */
		close(i);
	}
	if (LIKELY((fd = open("/dev/null", O_RDWR, 0)) >= 0)) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO) {
			(void)close(fd);
		}
	}
	umpf_logout = fopen("/dev/null", "w");
	return 0;
}

int
main(int argc, char *argv[])
{
	/* use the default event loop unless you have special needs */
	struct ev_loop *loop;
	static ev_signal ALGN16(sigint_watcher)[1];
	static ev_signal ALGN16(sighup_watcher)[1];
	static ev_signal ALGN16(sigterm_watcher)[1];
	static ev_signal ALGN16(sigpipe_watcher)[1];
	/* our communication sockets */
	ev_io lstn[2];
	/* args */
	struct gengetopt_args_info argi[1];
	/* our take on args */
	int daemonisep = 0;
	struct dbnfo_s db;
	char *sock;
	uint16_t port;
	cfg_t cfg;

	/* whither to log */
	umpf_logout = stderr;

	/* parse the command line */
	if (cmdline_parser(argc, argv, argi)) {
		exit(1);
	}

	/* evaluate argi */
	daemonisep |= argi->daemon_flag;

	/* try and read the context file */
	if ((cfg = umpf_read_config(argi->config_arg)) == NULL) {
		;
	} else {
		daemonisep |= cfg_glob_lookup_b(cfg, "daemonise");
	}

	/* run as daemon, do me properly */
	if (daemonisep && daemonise() < 0) {
		perror("daemonisation failed");
		exit(1);
	}

	/* start them log files */
	umpf_openlog();

	/* write a pid file? */
	{
		const char *pidf;

		if ((argi->pidfile_given && (pidf = argi->pidfile_arg)) ||
		    (cfg && cfg_glob_lookup_s(&pidf, cfg, "pidfile") > 0)) {
			/* command line has precedence */
			write_pidfile(pidf);
		}
	}

	/* get the sock name and tcp host/port */
	sock = umpf_get_sock(cfg);
	port = umpf_get_port(cfg);
	db = umpf_get_dbnfo(cfg);

	/* free cmdline parser goodness */
	cmdline_parser_free(argi);
	/* kick the config context */
	umpf_free_config(cfg);

	/* initialise the main loop */
	loop = ev_default_loop(EVFLAG_AUTO);

	/* initialise a sig C-c handler */
	ev_signal_init(sigint_watcher, sigint_cb, SIGINT);
	ev_signal_start(EV_A_ sigint_watcher);
	/* initialise a sig C-c handler */
	ev_signal_init(sigpipe_watcher, sigpipe_cb, SIGPIPE);
	ev_signal_start(EV_A_ sigpipe_watcher);
	/* initialise a SIGTERM handler */
	ev_signal_init(sigterm_watcher, sigint_cb, SIGTERM);
	ev_signal_start(EV_A_ sigterm_watcher);
	/* initialise a SIGHUP handler */
	ev_signal_init(sighup_watcher, sighup_cb, SIGHUP);
	ev_signal_start(EV_A_ sighup_watcher);

	/* stuff that was formerly in dso_init() */
	if (port) {
		int s;

		if ((s = make_tcp_conn(port)) < 0) {
			perror("cannot bind tcp port");
			lstn[0].fd = -1;
		} else {
			ev_io_init(lstn + 0, dccp_cb, s, EV_READ);
			ev_io_start(EV_A_ lstn + 0);
		}
	} else {
		lstn[0].fd = -1;
	}

	if (sock != NULL) {
		int s;

		if ((s = make_unix_conn(sock)) < 0) {
			perror("cannot bind unix socket");
			lstn[1].fd = -1;
		} else {
			ev_io_init(lstn + 1, dccp_cb, s, EV_READ);
			ev_io_start(EV_A_ lstn + 1);
		}
	} else {
		lstn[1].fd = -1;
	}

	/* connect to our database */
	switch (db.t) {
	case DBNFO_UNK:
	default:
		break;
	case DBNFO_SQLITE:
		umpf_dbconn = be_sql_open(NULL, NULL, NULL, db.f);
		break;
	case DBNFO_MYSQL:
		umpf_dbconn = be_sql_open(db.h, db.u, db.p, db.s);
		break;
	}

	UMPF_NOTI_LOG("umpfd ready\n");

	/* now wait for events to arrive */
	ev_loop(EV_A_ 0);

	UMPF_NOTI_LOG("shutting down umpfd\n");

	/* stuff that was in dso_deinit() formerly */
	if (lstn[0].fd >= 0) {
		ev_io_shut(EV_A_ lstn + 0);
	}
	if (lstn[1].fd >= 0) {
		ev_io_shut(EV_A_ lstn + 1);
	}

	/* destroy the default evloop */
	ev_default_destroy();

	/* close our db connection */
	if (umpf_dbconn) {
		be_sql_close(umpf_dbconn);
		switch (db.t) {
		case DBNFO_UNK:
		default:
			break;
		case DBNFO_SQLITE:
			free(db.f);
			break;
		case DBNFO_MYSQL:
			if (db.h) {
				free(db.h);
			}
			if (db.u) {
				free(db.u);
			}
			if (db.p) {
				free(db.p);
			}
			if (db.s) {
				free(db.s);
			}
			break;
		}
	}
	/* unlink the unix domain socket */
	if (sock != NULL) {
		unlink(sock);
		free(sock);
	}

	/* close our log output */
	fflush(umpf_logout);
	fclose(umpf_logout);
	umpf_closelog();
	/* unloop was called, so exit */
	return 0;
}

/* umpfd.c ends here */
