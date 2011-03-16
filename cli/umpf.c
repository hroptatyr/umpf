/*** umpf-new-pf.c -- request new portfolios
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
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
/* network stuff */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
/* epoll stuff */
#include "epoll-helpers.h"

#include "umpf.h"

#if defined DEBUG_FLAG
# include <assert.h>
#else  /* !DEBUG_FLAG */
# define assert(args...)
#endif	/* DEBUG_FLAG */

#if !defined UNUSED
# define UNUSED(_x)	__attribute__((unused)) _x##_unused
#endif	/* !UNUSED */
#if !defined LIKELY
# define LIKELY(_x)	__builtin_expect((_x), 1)
#endif
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif
#define countof(_x)	(sizeof(_x) / sizeof(*_x))

typedef enum {
	CONN_METH_UNK,
	CONN_METH_TCP,
	CONN_METH_DCCP,
	CONN_METH_UDP,
} conn_meth_t;

typedef enum {
	UMPF_CMD_UNK,
	UMPF_CMD_VERSION,
	UMPF_CMD_NEW_PF,
	UMPF_CMD_GET_PF,
	UMPF_CMD_SET_PF,
	UMPF_CMD_NEW_SEC,
	UMPF_CMD_GET_SEC,
	UMPF_CMD_SET_SEC,
	UMPF_CMD_GET_POSS,
	UMPF_CMD_SET_POSS,
} umpf_cmd_t;

/* new_pf specific options */
struct __set_pf_clo_s {
	const char *mnemo;
	const char *file;
	const char *descr;
};

struct __get_pf_clo_s {
	const char *mnemo;
};

struct __set_sec_clo_s {
	const char *mnemo;
	const char *pf;
	const char *file;
	const char *descr;
};

struct __get_sec_clo_s {
	const char *mnemo;
	const char *pf;
};

struct __set_poss_clo_s {
	const char *pf;
	const char *date;
	const char *file;
	const char *poss;
	time_t stamp;
};

struct __get_poss_clo_s {
	const char *pf;
	const char *date;
	time_t stamp;
};

/* command line options */
struct __clo_s {
	int helpp;

	conn_meth_t meth;
	const char *host;
	uint16_t port;
	int pref_fam;

	umpf_cmd_t cmd;
	union {
		struct __set_pf_clo_s set_pf[1];
		struct __get_pf_clo_s get_pf[1];
		struct __set_sec_clo_s set_sec[1];
		struct __get_sec_clo_s get_sec[1];
		struct __get_poss_clo_s get_poss[1];
		struct __set_poss_clo_s set_poss[1];
	};
};

#define VER	"umpf v" VERSION "\n"

static const char usage[] = VER "\
\n\
Usage: umpf [-h|--help] [-V|--version]\n\
  or : umpf [OPTIONS] COMMAND [COMMAND-OPTIONS]\n\
\n\
Communicate with an umpf server.\n\
\n\
  -h, --help            Print help and exit\n\
  -V, --version         Print version and exit\n\
\n\
Options common to all commands:\n\
      --host=STRING     Server to connect to [default: localhost]\n\
                        Can also point to a unix domain socket in\n\
                        which case a file name is expected.\n\
                        Also the form `hostname:port' is supported.\n\
\n\
Supported commands:\n\
\n\
  new-pf [OPTIONS] NAME      Register a new portfolio NAME\n\
    -d, --descr=STRING       Use description from STRING\n\
    -f, --file=FILE          File with description, - for stdin\n\
\n\
  get-pf NAME                Get the description of portfolio NAME\n\
\n\
  set-pf [OPTIONS] NAME      Set the description of portfolio NAME\n\
    -d, --descr=STRING       Set description from STRING\n\
    -f, --file=FILE          File with description to pass on\n\
                             Use - for stdin\n\
\n\
  new-sec [OPTIONS] NAME     Register a new security NAME\n\
    -p, --pf=STRING          Name of the portfolio to register the\n\
                             security with.\n\
    -d, --descr=STRING       Use description from STRING\n\
    -f, --file=FILE          File with description, - for stdin\n\
\n\
  get-sec [OPTIONS] NAME     Retrieve the security definition of NAME\n\
    -p, --pf=STRING          Name of the portfolio to register the\n\
                             security with.\n\
\n\
  set-sec [OPTIONS] NAME     Update the security definition of NAME\n\
    -p, --pf=STRING          Name of the portfolio that holds the security\n\
    -d, --descr=STRING       Set description from STRING\n\
    -f, --file=FILE          File with description, - for stdin\n\
\n\
  get-poss [OPTIONS] NAME    Retrieve the positions of portfolio NAME\n\
    -d, --date=DATE          Request the portfolio as of DATE\n\
\n\
  set-poss [OPTIONS] [NAME]  Set the portfolio NAME\n\
    -d, --date=DATE          Set the portfolio as of DATE\n\
    -f, --file=FILE          Use positions in FILE, - for stdin\n\
\n\
";


/* network bollocks */
static void
__set_v6only(int sock)
{
#if defined IPV6_V6ONLY
	int opt = 1;
	setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif	/* IPV6_V6ONLY */
	return;
}

static int
__connect(unsigned int pref_fam, const char *host, const uint16_t port)
{
#define AS_V4(x)	((struct sockaddr_in*)(x))
#define AS_V6(x)	((struct sockaddr_in6*)(x))
	struct hostent *hp;
	struct sockaddr_storage sa[1];
	volatile int sock;

#if defined __INTEL_COMPILER
# pragma warning (disable:2259)
#endif	/* __INTEL_COMPILER */
	switch (pref_fam) {
	case PF_UNSPEC:
	case PF_INET6:
		/* try ip6 first ... */
		memset(sa, 0, sizeof(*sa));
		if ((hp = gethostbyname2(host, AF_INET6)) != NULL &&
		    (sock = socket(PF_INET6, SOCK_STREAM, IPPROTO_IP)) >= 0) {
			AS_V6(sa)->sin6_family = AF_INET6;
			AS_V6(sa)->sin6_port = htons(port);
			memcpy(&AS_V6(sa)->sin6_addr, hp->h_addr, hp->h_length);

			__set_v6only(sock);

			/* try connect */
			if (connect(sock, (void*)sa, sizeof(*sa)) >= 0) {
				break;
			}
			close(sock);
		}
		/* ... then fall back to ip4 */
	case PF_INET:
		memset(sa, 0, sizeof(*sa));
		if ((hp = gethostbyname2(host, AF_INET)) != NULL &&
		    (sock = socket(PF_INET, SOCK_STREAM, IPPROTO_IP)) >= 0) {
			AS_V4(sa)->sin_family = AF_INET;
			AS_V4(sa)->sin_port = htons(port);
			AS_V4(sa)->sin_addr = *(struct in_addr*)hp->h_addr;

			/* try connect */
			if (connect(sock, (void*)sa, sizeof(*sa)) >= 0) {
				break;
			}
			close(sock);
		}
	default:
		/* no socket whatsoever */
		fprintf(stderr, "cannot connect to host %s.\n", host);
		return -1;
	}
#if defined __INTEL_COMPILER
# pragma warning (default:2259)
#endif	/* __INTEL_COMPILER */

	/* operate in non-blocking mode */
	setsock_nonblock(sock);
	return sock;
}

static int
umpf_connect(struct __clo_s *clo)
{
	return __connect(clo->pref_fam, clo->host, clo->port);
}

typedef enum {
	GUTS_GET,
	GUTS_FREE,
} epoll_guts_action_t;

static ep_ctx_t
epoll_guts(epoll_guts_action_t action)
{
	static struct ep_ctx_s gepg = FLEX_EP_CTX_INITIALISER(4);

	switch (action) {
	case GUTS_GET:
		if (UNLIKELY(gepg.sock == -1)) {
			init_ep_ctx(&gepg, gepg.nev);
		}
		break;
	case GUTS_FREE:
		if (LIKELY(gepg.sock != -1)) {
			free_ep_ctx(&gepg);
		}
		break;
	}
	return &gepg;
}


static umpf_msg_t
read_reply(volatile int fd)
{
	static char buf[4096];
	static umpf_ctx_t p = NULL;
	ssize_t nrd;
	umpf_msg_t rpl = NULL;

	while ((nrd = recv(fd, buf, sizeof(buf), 0)) > 0) {
#if defined DEBUG_FLAG
		fprintf(stderr, "read %zd\n", nrd);
#endif	/* DEBUG_FLAG */
		if ((rpl = umpf_parse_blob(&p, buf, nrd)) != NULL) {
			/* bingo */
			break;

		} else if (/* rpl == NULL && */p == NULL) {
			/* error */
			break;
		}
		/* not enough data yet, request more */
	}
	return rpl;
}

static void
fput_zulu(time_t stamp, FILE *where)
{
	struct tm tm[1] = {{0}};
	char buf[32];

	if (LIKELY(stamp > 0)) {
		gmtime_r(&stamp, tm);
		(void)strftime(buf, sizeof(buf), "%FT%T%z", tm);
		fputs(buf, where);
	} else {
		fputc('0', where);
	}
	return;
}

static void
fput_date(time_t stamp, FILE *where)
{
	struct tm tm[1] = {{0}};
	char buf[32];

	if (LIKELY(stamp > 0)) {
		gmtime_r(&stamp, tm);
		(void)strftime(buf, sizeof(buf), "%F", tm);
		fputs(buf, where);
	} else {
		fputc('0', where);
	}
	return;
}

static void
pretty_print(umpf_msg_t msg)
{
	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF:
	case UMPF_MSG_SET_DESCR:
		fputs(":portfolio \"", stdout);
		fputs(msg->new_pf.name, stdout);
		fputs("\"\n", stdout);
		{
			const char *data = msg->new_pf.satellite->data;
			const size_t size = msg->new_pf.satellite->size;
			fwrite(data, size, 1, stdout);
			if (data[size - 1] != '\n') {
				fputc('\n', stdout);
			}
		}
		break;
	case UMPF_MSG_GET_DESCR:
		fputs(":portfolio \"", stdout);
		fputs(msg->new_pf.name, stdout);
		fputs("\"\n", stdout);
		break;
	case UMPF_MSG_SET_SEC:
	case UMPF_MSG_NEW_SEC:
		fputs(":portfolio \"", stdout);
		fputs(msg->new_sec.pf_mnemo, stdout);
		fputs("\" :security \"", stdout);
		fputs(msg->new_sec.ins->sym, stdout);
		fputs("\"\n", stdout);
		if (msg->new_sec.satellite->data) {
			const char *data = msg->new_sec.satellite->data;
			const size_t size = msg->new_sec.satellite->size;
			fwrite(data, size, 1, stdout);
			if (data[size - 1] != '\n') {
				fputc('\n', stdout);
			}
		}
		break;		
	case UMPF_MSG_GET_SEC:
		fputs(":portfolio \"", stdout);
		fputs(msg->new_sec.pf_mnemo, stdout);
		fputs("\" :security \"", stdout);
		fputs(msg->new_sec.ins->sym, stdout);
		fputs("\"\n", stdout);
		break;

	case UMPF_MSG_SET_PF:
		fputs(":portfolio \"", stdout);
		fputs(msg->pf.name, stdout);
		fputs("\" :stamp ", stdout);
		fput_zulu(msg->pf.stamp, stdout);
		fputs(" :clear ", stdout);
		fput_date(msg->pf.clr_dt, stdout);
		fputc('\n', stdout);

		for (size_t i = 0; i < msg->pf.nposs; i++) {
			struct __ins_qty_s *pos = msg->pf.poss + i;
			fputs(pos->ins->sym, stdout);
			fprintf(stdout, "\t%.6f\t%.6f\n",
				pos->qty->_long, pos->qty->_shrt);
		}
		break;
	case UMPF_MSG_GET_PF:
		fputs(":portfolio \"", stdout);
		fputs(msg->pf.name, stdout);
		fputs("\" :stamp ", stdout);
		fput_zulu(msg->pf.stamp, stdout);
		fputs(" :clear ", stdout);
		fput_date(msg->pf.clr_dt, stdout);
		fputc('\n', stdout);
		break;
	default:
		fputs("cannot interpret response\n", stderr);
		break;
	}
	return;
}

/* main loop */
static int
umpf_repl(umpf_msg_t msg, volatile int sock)
{
#define SRV_TIMEOUT	(4000/*millis*/)
	ep_ctx_t epg;
	int nfds;

	/* also set up our epoll magic */
	epg = epoll_guts(GUTS_GET);
	/* setup event waiter */
	ep_prep_et_rdwr(epg, sock);

	while ((nfds = ep_wait(epg, SRV_TIMEOUT)) > 0) {
		typeof(epg->ev[0].events) ev = epg->ev[0].events;
		int fd = epg->ev[0].data.fd;

		/* we've only asked for one, so it would be peculiar */
		assert(nfds == 1);

		if (LIKELY(ev & EPOLLIN)) {
			/* read what's on the wire */
			umpf_msg_t rpl = read_reply(fd);

			if (LIKELY(rpl != NULL)) {
				/* everything's brill */
#if defined DEBUG_FLAG
				umpf_print_msg(STDERR_FILENO, rpl);
#endif	/* DEBUG_FLAG */
				pretty_print(rpl);
				umpf_free_msg(rpl);
				nfds = 0;
				break;
			}

		} else if (ev & EPOLLOUT && msg) {
#if defined DEBUG_FLAG
			umpf_print_msg(STDERR_FILENO, msg);
#endif	/* DEBUG_FLAG */
			umpf_print_msg(fd, msg);
			msg = NULL;
		} else {
#if defined DEBUG_FLAG
			fprintf(stderr, "epoll repl exitted, flags %x\n", ev);
#endif	/* DEBUG_FLAG */
			nfds = -1;
			break;
		}
	}
	/* stop waiting for events */
	ep_fini(epg, sock);
	(void)epoll_guts(GUTS_FREE);
	return nfds;
}


/* message bollocks */
static umpf_msg_t
make_umpf_msg(void)
{
	umpf_msg_t res = calloc(sizeof(*res), 1);
	return res;
}

static umpf_msg_t
make_umpf_new_pf_msg(const char *mnemo, const char *satell, size_t ssize)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_NEW_PF);
	res->new_pf.name = strdup(mnemo);
	if (LIKELY(satell != NULL)) {
		res->new_pf.satellite->data =
			malloc((res->new_pf.satellite->size = ssize));
		memcpy(res->new_pf.satellite->data, satell, ssize);
	}
	return res;
}

static umpf_msg_t
make_umpf_new_sec_msg(
	const char *pf, const char *sym, const char *sat, size_t satlen)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_NEW_SEC);
	res->new_sec.ins->sym = strdup(sym);
	res->new_sec.pf_mnemo = strdup(pf);
	if (LIKELY(sat != NULL)) {
		res->new_sec.satellite->data =
			malloc((res->new_sec.satellite->size = satlen));
		memcpy(res->new_sec.satellite->data, sat, satlen);
	}
	return res;
}

static umpf_msg_t
make_umpf_get_pf_msg(const char *mnemo)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_GET_DESCR);
	res->new_pf.name = strdup(mnemo);
	return res;
}

static umpf_msg_t
make_umpf_get_poss_msg(const char *mnemo, const time_t stamp)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_GET_PF);
	res->pf.name = strdup(mnemo);
	res->pf.stamp = stamp;
	return res;
}

static umpf_msg_t
make_umpf_get_sec_msg(const char *pf_mnemo, const char *mnemo)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_GET_SEC);
	res->new_sec.ins->sym = strdup(mnemo);
	res->new_sec.pf_mnemo = strdup(pf_mnemo);
	return res;
}

static int
umpf_process(struct __clo_s *clo)
{
	int res = -1;
	umpf_msg_t msg = make_umpf_msg();
	volatile int sock;

	switch (clo->cmd) {
	case UMPF_CMD_SET_PF:
		/* FIXML can't distinguish between new_pf and set_pf,
		 * so we just use NEW_PF for this */
	case UMPF_CMD_NEW_PF: {
		const char *mnemo = clo->set_pf->mnemo;
		const char *descr = clo->set_pf->descr;
		const size_t dsize = descr ? strlen(descr) : 0;
		msg = make_umpf_new_pf_msg(mnemo, descr, dsize);
		break;
	}
	case UMPF_CMD_NEW_SEC: {
		const char *mnemo = clo->set_sec->mnemo;
		const char *descr = clo->set_sec->descr;
		const size_t dsize = descr ? strlen(descr) : 0;
		const char *pf_mnemo = clo->set_sec->pf;
		msg = make_umpf_new_sec_msg(pf_mnemo, mnemo, descr, dsize);
		break;
	}
	case UMPF_CMD_SET_SEC: {
		const char *mnemo = clo->set_sec->mnemo;
		const char *pf_mnemo = clo->set_sec->pf;
		const char *descr = clo->set_sec->descr;
		const size_t dsize = descr ? strlen(descr) : 0;
		/* call __new_sec_msg() and fiddle with the msg type later */
		msg = make_umpf_new_sec_msg(pf_mnemo, mnemo, descr, dsize);
		umpf_set_msg_type(msg, UMPF_MSG_SET_SEC);
		break;
	}
	case UMPF_CMD_GET_PF: {
		const char *mnemo = clo->set_pf->mnemo;
		msg = make_umpf_get_pf_msg(mnemo);
		break;
	}
	case UMPF_CMD_GET_SEC: {
		const char *mnemo = clo->set_sec->mnemo;
		const char *pf_mnemo = clo->set_sec->pf;
		msg = make_umpf_get_sec_msg(pf_mnemo, mnemo);
		break;
	}
	case UMPF_CMD_GET_POSS: {
		const char *mnemo = clo->get_poss->pf;
		const time_t stamp = clo->get_poss->stamp ?: time(NULL);
		msg = make_umpf_get_poss_msg(mnemo, stamp);
		break;
	}
	default:
		/* don't even try the connect */
		return -1;
	}

	/* get ourselves a connection */
	if (LIKELY((sock = umpf_connect(clo)) >= 0)) {
		/* main loop */
		res = umpf_repl(msg, sock);
		/* close socket */
		close(sock);
	}
	/* we don't need this message object anymore */
	umpf_free_msg(msg);
	return res;
}


/* command line parsing */
static void
pr_unknown(const char *arg)
{
	fprintf(stderr, "unknown option %s\n", arg);
	return;
}

static char*
__get_val(int *i, size_t len, char *argv[])
{
	char *p = argv[*i];

	switch (p[len]) {
	case '\0':
		p = argv[*i + 1];
		argv[*i] = NULL;
		argv[*i + 1] = NULL;
		(*i)++;
		break;
	case '=':
		p += len + 1;
		argv[*i] = NULL;
		break;
	default:
		pr_unknown(argv[*i]);
		p = NULL;
	}
	return p;
}

static void
parse_host_args(struct __clo_s *clo, char *argv[])
{
	int i = 0;
	char *p;

	if ((clo->host = __get_val(&i, /*--host*/6, argv)) == NULL) {
		/* was --hostsomething, which is bollocks
		 * actually i'd really love to abort() here now, stupid user */
		clo->helpp = 1;
		return;
	}
	/* otherwise check for HOST:PORT syntax */
	if ((p = strchr(clo->host, ':')) != NULL) {
		*p = '\0';
		clo->port = strtoul(p + 1, NULL, 10);
	}
	return;
}

static void
parse_set_pf_args(struct __clo_s *clo, int argc, char *argv[])
{
/* also used for new-pf */
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d or --descr */
			if (*p == 'd') {
				clo->set_pf->descr = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-descr", 6) == 0) {
				clo->set_pf->descr = __get_val(&i, 7, argv);
			}
		} else if (clo->set_pf->mnemo == NULL) {
			/* must be the name then */
			clo->set_pf->mnemo = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_set_sec_args(struct __clo_s *clo, int argc, char *argv[])
{
/* also used for new-sec */
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d or --descr or -p or --pf */
			if (*p == 'd') {
				clo->set_sec->descr = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-descr", 6) == 0) {
				clo->set_sec->descr = __get_val(&i, 7, argv);
			} else if (*p == 'p') {
				clo->set_sec->pf = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-pf", 3) == 0) {
				clo->set_sec->pf = __get_val(&i, 4, argv);
			}
		} else if (clo->set_sec->mnemo == NULL) {
			/* must be the name then */
			clo->set_sec->mnemo = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_set_poss_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d or --date or -f or --file */
			if (*p == 'd') {
				clo->set_poss->date = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-date", 5) == 0) {
				clo->set_poss->date = __get_val(&i, 6, argv);
			} else if (*p == 'f') {
				clo->set_poss->file = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-file", 5) == 0) {
				clo->set_poss->file = __get_val(&i, 6, argv);
			}
		} else if (clo->set_poss->pf == NULL) {
			/* must be the name then */
			clo->set_poss->pf = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (p == NULL) {
			/* args may be set to NULL during the course */
			continue;
		}

		switch (*p++) {
		case '-':
			/* global option */
			switch (*p++) {
			case '-':
				/* long opt */
				if (strncmp(p, "host", 4) == 0) {
					parse_host_args(clo, argv + i);
					continue;
				} else if (strcmp(p, "help") == 0) {
					clo->helpp = 1;
					continue;
				} else if (strcmp(p, "version") == 0) {
					clo->cmd = UMPF_CMD_VERSION;
					return;
				}
				break;
			case 'V':
				if (*p == '\0') {
					/* it's -V */
					clo->cmd = UMPF_CMD_VERSION;
					return;
				}
				break;
			case 'h':
				if (*p == '\0') {
					/* it's -h */
					clo->helpp = 1;
					continue;
				}
				break;
			}
			break;
		case 'n': {
			/* new-pf or new-sec */
			int new_argc = argc - i - 1;
			char **new_argv = argv + i + 1;
			if (strcmp(p, "ew-pf") == 0) {
				clo->cmd = UMPF_CMD_NEW_PF;
				parse_set_pf_args(clo, new_argc, new_argv);
				continue;

			} else if (strcmp(p, "ew-sec") == 0) {
				clo->cmd = UMPF_CMD_NEW_SEC;
				parse_set_sec_args(clo, new_argc, new_argv);
				continue;
			}
			break;
		}
		case 's':
		case 'g': {
			/* set-pf or set-sec or set-poss */
			/* get-pf or get-sec or get-poss */
			int new_argc = argc - i - 1;
			char **new_argv = argv + i + 1;
			if (strcmp(p, "et-pf") == 0) {
				if (p[-1] == 'g') {
					clo->cmd = UMPF_CMD_GET_PF;
				} else {
					clo->cmd = UMPF_CMD_SET_PF;
				}
				parse_set_pf_args(clo, new_argc, new_argv);
				continue;

			} else if (strcmp(p, "et-sec") == 0) {
				if (p[-1] == 'g') {
					clo->cmd = UMPF_CMD_GET_SEC;
				} else {
					clo->cmd = UMPF_CMD_SET_SEC;
				}
				parse_set_sec_args(clo, new_argc, new_argv);
				continue;
			} else if (strcmp(p, "et-poss") == 0) {
				if (p[-1] == 'g') {
					clo->cmd = UMPF_CMD_GET_POSS;
				} else {
					clo->cmd = UMPF_CMD_SET_POSS;
				}
				parse_set_poss_args(clo, new_argc, new_argv);
				continue;
			}
			break;
		}
		default:
			break;
		}

		/* if we end up here, something could not be parsed */
		pr_unknown(argv[i]);
	}
	return;
}

static void
print_usage(umpf_cmd_t UNUSED(cmd))
{
	fputs(usage, stdout);
	return;
}

static void
print_version(void)
{
	fputs(VER, stdout);
	return;
}

static int
check__pf_args(struct __clo_s *clo)
{
	if (clo->set_pf->mnemo == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->set_pf->file) {
		/* check if file exists */
	}
	return 0;
}

static int
check__sec_args(struct __clo_s *clo)
{
	if (clo->set_sec->pf == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->set_sec->mnemo == NULL) {
		fputs("security mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->set_sec->file) {
		/* check if file exists */
	}
	return 0;
}

static int
check__poss_args(struct __clo_s *clo)
{
	if (clo->set_poss->pf == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->set_poss->file) {
		/* check if file exists */
	}
	return 0;
}


int
main(int argc, char *argv[])
{
	struct __clo_s argi = {0};

	/* default values */
	argi.host = "localhost";
	argi.port = 8675;
	/* parse them command line */
	parse_args(&argi, argc - 1, argv + 1);

	if (argi.helpp) {
		/* command specific help? la'ers */
		print_usage(argi.cmd);
		return 0;
	}

	switch (argi.cmd) {
	case UMPF_CMD_UNK:
	default:
		print_usage(argi.cmd);
		return 1;
	case UMPF_CMD_VERSION:
		print_version();
		return 0;
	case UMPF_CMD_NEW_PF:
	case UMPF_CMD_GET_PF:
	case UMPF_CMD_SET_PF:
		if (check__pf_args(&argi)) {
			print_usage(argi.cmd);
			return 1;
		}
		break;
	case UMPF_CMD_NEW_SEC:
	case UMPF_CMD_GET_SEC:
	case UMPF_CMD_SET_SEC:
		if (check__sec_args(&argi)) {
			print_usage(argi.cmd);
			return 1;
		}
		break;
	case UMPF_CMD_GET_POSS:
	case UMPF_CMD_SET_POSS:
		if (check__poss_args(&argi)) {
			print_usage(argi.cmd);
			return 1;
		}
		break;
	}

	/* now go go go */
	if (UNLIKELY((umpf_process(&argi)) < 0)) {
		return 1;
	}
	return 0;
}

/* umpf-new-pf.c ends here */
