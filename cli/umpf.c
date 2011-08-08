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
#include <time.h>
#include <ctype.h>
#include <math.h>
/* network stuff */
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
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
	UMPF_CMD_LIST_PF,
	UMPF_CMD_NEW_PF,
	UMPF_CMD_GET_PF,
	UMPF_CMD_SET_PF,
	UMPF_CMD_CLO_PF,
	UMPF_CMD_NEW_SEC,
	UMPF_CMD_GET_SEC,
	UMPF_CMD_SET_SEC,
	UMPF_CMD_GET_POSS,
	UMPF_CMD_SET_POSS,
	UMPF_CMD_APPLY,
	UMPF_CMD_DIFF,
	UMPF_CMD_LIST_TAG,
	UMPF_CMD_DEL_TAG,
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
	const char *dfile;
	const char *descr;
};

struct __get_sec_clo_s {
	const char *mnemo;
	const char *pf;
};

struct __set_poss_clo_s {
	const char *pf;
	const char *date;
	time_t stamp;
	const char *file;
	const char *poss;
};

struct __get_poss_clo_s {
	const char *pf;
	const char *date;
	time_t stamp;
};

struct __apply_clo_s {
	const char *pf;
	const char *date;
	time_t stamp;
	const char *file;
	const char *poss;
};

struct __clo_pf_clo_s {
	const char *old;
	const char *new;
	const char *file;
	const char *descr;
	int movep;
};

struct __del_tag_clo_s {
	const char *mnemo;
	const char *tag;
	long unsigned int tag_id;
};

struct __ls_tag_clo_s {
	const char *mnemo;
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
		struct __apply_clo_s apply[1];
		struct __clo_pf_clo_s clo_pf[1];
		struct __del_tag_clo_s del_tag[1];
		struct __ls_tag_clo_s ls_tag[1];
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
  list-pf                    List all available portfolios\n\
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
  clo-pf [OPTIONS] OLD NEW   Copy or rename a portfolio OLD to NEW.\n\
    -d, --descr=STRING       Set description from STRING\n\
    -f, --file=FILE          File with description to pass on\n\
                             Use - for stdin\n\
    -m, --move               Delete the old portfolio OLD as well.\n\
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
  apply [OPTIONS] [NAME]     Apply order to portfolio NAME\n\
    -d, --date=DATE          Set the portfolio as of DATE\n\
    -f, --file=FILE          Use positions in FILE, - for stdin\n\
\n\
  list-tag NAME              List all tags in portfolio NAME\n\
  del-tag NAME TAG           Delete TAG from portfolio NAME\n\
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
#define AS_UN(x)	((struct sockaddr_un*)(x))
	struct hostent *hp;
	struct sockaddr_storage sa[1];
	volatile int sock;

	/* rinse */
	memset(sa, 0, sizeof(*sa));

#if defined __INTEL_COMPILER
# pragma warning (disable:2259)
#endif	/* __INTEL_COMPILER */
	switch (pref_fam) {
	case PF_UNIX:
		if ((sock = socket(PF_LOCAL, SOCK_STREAM, 0)) >= 0) {
			size_t unsz = sizeof(AS_UN(sa)->sun_path);

			AS_UN(sa)->sun_family = AF_LOCAL;
			strncpy(AS_UN(sa)->sun_path, host, unsz);
			AS_UN(sa)->sun_path[unsz - 1] = '\0';
			unsz = offsetof(struct sockaddr_un, sun_path) +
				strlen(AS_UN(sa)->sun_path) + 1;

			/* try connect */
			if (connect(sock, (void*)sa, unsz) >= 0) {
				break;
			}
			close(sock);
		}
		/* try v6 next */
	case PF_UNSPEC:
	case PF_INET6:
		/* try ip6 first ... */
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


static char gbuf[4096];

static umpf_msg_t
read_reply(volatile int fd)
{
	static umpf_ctx_t p = NULL;
	ssize_t nrd;
	umpf_msg_t rpl = NULL;

	while ((nrd = recv(fd, gbuf, sizeof(gbuf), 0)) > 0) {
#if defined DEBUG_FLAG
		fprintf(stderr, "read %zd\n", nrd);
#endif	/* DEBUG_FLAG */
		if ((rpl = umpf_parse_blob(&p, gbuf, nrd)) != NULL) {
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

static int
write_request(int fd, const char *buf, size_t len)
{
	ssize_t wrt;
	size_t tot = 0;

	while ((wrt = write(fd, buf + tot, len - tot)) > 0 &&
	       (tot += wrt) < len);
#if defined DEBUG_FLAG
	fprintf(stderr, "written %zu\n", tot);
#endif	/* DEBUG_FLAG */
	return tot;
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

static time_t
from_zulu(const char *buf)
{
	struct tm tm[1] = {{0}};

	/* skip over whitespace */
	for (; *buf && isspace(*buf); buf++);

	if (strptime(buf, "%Y-%m-%dT%H:%M:%S%Z", tm)) {
		;
	} else if (strptime(buf, "%Y-%m-%dT%H:%M:%S", tm)) {
		;
	} else if (strptime(buf, "%Y-%m-%d", tm)) {
		;
	} else {
		return strtoul(buf, NULL, 10);
	}
	return timegm(tm);
}

static void
pretty_print(umpf_msg_t msg)
{
	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF:
	case UMPF_MSG_SET_DESCR: {
		const char *data;

		fputs(":portfolio \"", stdout);
		fputs(msg->new_pf.name, stdout);
		fputs("\"\n", stdout);

		if (LIKELY((data = msg->new_pf.satellite->data) != NULL)) {
			const size_t size = msg->new_pf.satellite->size;
			fwrite(data, size, 1, stdout);
			if (data[size - 1] != '\n') {
				fputc('\n', stdout);
			}
		}
		break;
	}
	case UMPF_MSG_GET_DESCR:
		fputs(":portfolio \"", stdout);
		fputs(msg->new_pf.name, stdout);
		fputs("\"\n", stdout);
		break;
	case UMPF_MSG_SET_SEC:
	case UMPF_MSG_NEW_SEC: {
		const char *data;

		fputs(":portfolio \"", stdout);
		fputs(msg->new_sec.pf_mnemo, stdout);
		fputs("\" :security \"", stdout);
		fputs(msg->new_sec.ins->sym, stdout);
		fputs("\"\n", stdout);

		if (LIKELY((data = msg->new_sec.satellite->data) != NULL)) {
			const size_t size = msg->new_sec.satellite->size;
			fwrite(data, size, 1, stdout);
			if (data[size - 1] != '\n') {
				fputc('\n', stdout);
			}
		}
		break;		
	}
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
	/* serialise the message */
	char *buf = gbuf;
	size_t nsz = umpf_seria_msg(&buf, -countof(gbuf), msg);
	/* track the number of bytes written */
	size_t wrt = 0;

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

		} else if (ev & EPOLLOUT) {
			/* have we got stuff to write out? */
			if (wrt < nsz) {
				/* write a bit of the message */
				wrt += write_request(fd, buf + wrt, nsz - wrt);
			}

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

	/* free serialisation resources */
	if (buf != gbuf) {
		free(buf);
	}
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
make_umpf_new_sec_msg(const char *pf, const char *sym, void *clo)
{
	struct __set_sec_clo_s *ss = clo;
	umpf_msg_t res = make_umpf_msg();

	umpf_set_msg_type(res, UMPF_MSG_NEW_SEC);
	res->new_sec.ins->sym = strdup(sym);
	res->new_sec.pf_mnemo = strdup(pf);

	/* deal with satellite data, prefer descr over dfile */
	if (LIKELY(ss->descr != NULL)) {
		const char *sat = ss->descr;
		const size_t satlen = strlen(sat);
		res->new_sec.satellite->data =
			malloc((res->new_sec.satellite->size = satlen));
		memcpy(res->new_sec.satellite->data, sat, satlen);

	} else if (LIKELY(ss->dfile != NULL)) {
		struct stat st = {0};
		int fd;

		if (stat(ss->dfile, &st) < 0) {
			;
		} else if ((fd = open(ss->dfile, O_RDONLY)) < 0) {
			;
		} else {
			const size_t fsz = st.st_size;

			res->new_sec.satellite->data =
				malloc((res->new_sec.satellite->size = fsz));
			/* read them whole file into the satellite*/
			for (ssize_t off = 0, nrd;
			     (nrd = read(
				      fd,
				      res->new_sec.satellite->data + off,
				      fsz - off)) > 0 && nrd < (ssize_t)fsz;
			     off += nrd);

			/* brilliant, finished we iz */
			close(fd);
		}
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
make_umpf_list_pf_msg(void)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_LST_PF);
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

static umpf_msg_t
__massage_first(const char *line, size_t llen)
{
/* massage the first line of a positions file */
	umpf_msg_t res;
	const char pf_cookie[] = ":portfolio ";
	const char tm_cookie[] = ":stamp ";

	/* first line long enough and begins with :portfolio? */
	if (UNLIKELY(llen < 12 || strncmp(line, pf_cookie, 11))) {
		return NULL;
	}

	/* otherwise set up our message here */
	res = make_umpf_msg();

	{
		const char *pf = line + countof(pf_cookie) - 1;
		const char *pf_end;
		const char *stmp;
		size_t pf_len;

#if defined __INTEL_COMPILER
# pragma warning (disable:981)
#endif	/* __INTEL_COMPILER */
		if (pf[0] == '"') {
			/* search for matching quote */
			pf_end = pf++ + 1;
			while ((pf_end = strchrnul(pf_end, '"')) &&
			       pf_end[-1] == '\\');
		} else {
			while (isspace(*pf++));
			pf_end = strchrnul(pf--, ' ');
		}
		/* alloc space */
		pf_len = pf_end - pf;
		res->pf.name = malloc(pf_len + 1);
		memcpy(res->pf.name, pf, pf_len);
		res->pf.name[pf_len] = '\0';

		/* now care bout the stamp */
		stmp = memmem(line, llen, tm_cookie, countof(tm_cookie) - 1);
		if (LIKELY(stmp != NULL)) {
			stmp += countof(tm_cookie) - 1;
			while (isspace(*stmp++));
			res->pf.stamp = from_zulu(--stmp);
		} else {
			/* use current time by default */
			res->pf.stamp = time(NULL);
		}
#if defined __INTEL_COMPILER
# pragma warning (default:981)
#endif	/* __INTEL_COMPILER */
	}
	return res;
}

static int
__frob_poss_line(struct __ins_qty_s *iq, const char *line, const size_t llen)
{
	const char *sym = line;
	const char *lo;
	const char *sh;

	if (UNLIKELY(llen < 3)) {
		return -1;
	} else if ((lo = strchr(sym + 1, '\t')) == NULL) {
		return -1;
	} else if ((sh = strchr(lo + 1, '\t')) == NULL) {
		return -1;
	}

	iq->ins->sym = strndup(sym, lo - sym);
	iq->qty->_long = strtod(lo + 1, NULL);
	iq->qty->_shrt = strtod(sh + 1, NULL);
	return 0;
}

static umpf_msg_t
make_umpf_set_poss_msg(const char *mnemo, const time_t stamp, const char *file)
{
	umpf_msg_t res = NULL;
	size_t llen;
	char *line;
	FILE *f;

	/* sigh, now the hard part, open file and parse positions */
	if ((file == NULL) || (file[0] == '-' && file[1] == '\0')) {
		f = stdin; 
	} else if ((f = fopen(file, "r")) == NULL) {
		return NULL;
	}

	/* get resources together for our getline */
	llen = 256;
	line = malloc(llen);

	for (ssize_t nrd, ln = 0; (nrd = getline(&line, &llen, f)) >= 0; ln++) {
		struct __ins_qty_s iq = {};

		/* bit complicated would be the case that the command line
		 * option (--date) should take precedence, it _could_ be
		 * useful if your portfolio hasn't changed since yesterday
		 * and now you just want to stamp it off under a different
		 * name or different time stamp */
		if (ln == 0 && (res = __massage_first(line, nrd)) != NULL) {
			/* first line is full of cookies */
			umpf_set_msg_type(res, UMPF_MSG_SET_PF);
			continue;

		} else if (UNLIKELY(ln == 0 && mnemo == NULL)) {
			goto out;

		} else if (UNLIKELY(ln == 0)) {
			res = make_umpf_msg();
			umpf_set_msg_type(res, UMPF_MSG_SET_PF);
			res->pf.name = strdup(mnemo);
			res->pf.stamp = stamp ?: time(NULL);

		} else if (UNLIKELY(nrd == 0)) {
			continue;

		}

		if (__frob_poss_line(&iq, line, nrd) < 0) {
			/* parsing the line failed */
			continue;
		} else {
			/* we've got to do a realloc() for every line, fuck */
			size_t np = res->pf.nposs++;
			size_t iqsz = sizeof(*res->pf.poss) * (np + 1);
			res = realloc(res, sizeof(*res) + iqsz);
			res->pf.poss[np] = iq;
		}
	}
out:
	free(line);
	return res;
}

static umpf_msg_t
__ass_pos(umpf_msg_t msg, struct __ins_qty_s *iq)
{
	/* we've got to do a realloc() for every line, fuck */
	size_t np = msg->pf.nposs;
	size_t to_add = 0;
	umpf_msg_t res;

	if (fpclassify(iq->qty->_long) != FP_ZERO) {
		to_add++;
	}
	if (fpclassify(iq->qty->_shrt) != FP_ZERO) {
		to_add++;
	}
	/* resize */
	res = umpf_msg_add_pos(msg, to_add);
	/* assign */
	if (iq->qty->_long > 0.0) {
		res->pf.poss[np].ins->sym = iq->ins->sym;
		res->pf.poss[np].qsd->pos = iq->qty->_long;
		res->pf.poss[np++].qsd->sd = QSIDE_OPEN_LONG;
	} else if (iq->qty->_long < 0.0) {
		res->pf.poss[np].ins->sym = iq->ins->sym;
		res->pf.poss[np].qsd->pos = -iq->qty->_long;
		res->pf.poss[np++].qsd->sd = QSIDE_CLOSE_LONG;
	}
	if (iq->qty->_shrt > 0.0) {
		res->pf.poss[np].ins->sym = iq->ins->sym;
		res->pf.poss[np].qsd->pos = iq->qty->_shrt;
		res->pf.poss[np++].qsd->sd = QSIDE_OPEN_SHORT;
	} else if (iq->qty->_shrt < 0.0) {
		res->pf.poss[np].ins->sym = iq->ins->sym;
		res->pf.poss[np].qsd->pos = -iq->qty->_shrt;
		res->pf.poss[np++].qsd->sd = QSIDE_CLOSE_SHORT;
	}
	return res;
}

static umpf_msg_t
make_umpf_apply_msg(const char *mnemo, const time_t stamp, const char *file)
{
	umpf_msg_t res = NULL;
	size_t llen;
	char *line;
	FILE *f;

	/* sigh, now the hard part, open file and parse positions */
	if ((file == NULL) || (file[0] == '-' && file[1] == '\0')) {
		f = stdin; 
	} else if ((f = fopen(file, "r")) == NULL) {
		return NULL;
	}

	/* get resources together for our getline */
	llen = 256;
	line = malloc(llen);

	for (ssize_t nrd, ln = 0; (nrd = getline(&line, &llen, f)) >= 0; ln++) {
		struct __ins_qty_s iq = {};

		/* bit complicated would be the case that the command line
		 * option (--date) should take precedence, it _could_ be
		 * useful if your portfolio hasn't changed since yesterday
		 * and now you just want to stamp it off under a different
		 * name or different time stamp */
		if (ln == 0 && (res = __massage_first(line, nrd)) != NULL) {
			/* first line is full of cookies */
			umpf_set_msg_type(res, UMPF_MSG_PATCH);
			continue;

		} else if (UNLIKELY(ln == 0 && mnemo == NULL)) {
			goto out;

		} else if (UNLIKELY(ln == 0)) {
			res = make_umpf_msg();
			umpf_set_msg_type(res, UMPF_MSG_PATCH);
			res->pf.name = strdup(mnemo);
			res->pf.stamp = stamp ?: time(NULL);

		} else if (UNLIKELY(nrd == 0)) {
			continue;

		}

		/* parse the line */
		if (__frob_poss_line(&iq, line, nrd) < 0) {
			/* parsing the line failed */
			continue;
		}
		res = __ass_pos(res, &iq);
	}
out:
	free(line);
	return res;
}

static umpf_msg_t
make_umpf_ls_tag_msg(const char *mnemo)
{
	umpf_msg_t res = make_umpf_msg();
	umpf_set_msg_type(res, UMPF_MSG_LST_TAG);
	res->lst_tag.name = strdup(mnemo);
	return res;
}

static int
umpf_process(struct __clo_s *clo)
{
	int res = -1;
	umpf_msg_t msg = make_umpf_msg();
	volatile int sock;

	switch (clo->cmd) {
	case UMPF_CMD_LIST_PF: {
		msg = make_umpf_list_pf_msg();
		break;
	}
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
		const char *pf_mnemo = clo->set_sec->pf;
		msg = make_umpf_new_sec_msg(pf_mnemo, mnemo, clo->set_sec);
		break;
	}
	case UMPF_CMD_SET_SEC: {
		const char *mnemo = clo->set_sec->mnemo;
		const char *pf_mnemo = clo->set_sec->pf;
		/* call __new_sec_msg() and fiddle with the msg type later */
		msg = make_umpf_new_sec_msg(pf_mnemo, mnemo, clo->set_sec);
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
	case UMPF_CMD_SET_POSS: {
		const char *mnemo = clo->set_poss->pf;
		const time_t stamp = clo->set_poss->stamp;
		const char *file = clo->set_poss->file;
		if ((msg = make_umpf_set_poss_msg(mnemo, stamp, file))) {
			break;
		}
		return -1;
	}
	case UMPF_CMD_APPLY: {
		const char *mnemo = clo->apply->pf;
		const time_t stamp = clo->apply->stamp;
		const char *file = clo->apply->file;
		if ((msg = make_umpf_apply_msg(mnemo, stamp, file))) {
			break;
		}
		return -1;
	}
	case UMPF_CMD_CLO_PF: {
		return -1;
	}
	case UMPF_CMD_LIST_TAG: {
		const char *mnemo = clo->ls_tag->mnemo;
		if ((msg = make_umpf_ls_tag_msg(mnemo))) {
			break;
		}
		return -1;
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
	} else {
		/* must be a domain socket */
		clo->port = 0;
		clo->pref_fam = PF_UNIX;
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
			} else if (*p == 'f') {
				clo->set_sec->dfile = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-file", 5) == 0) {
				clo->set_sec->dfile = __get_val(&i, 6, argv);
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
parse_clo_pf_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d/--descr, -f/--file or -m/--move */
			if (*p == 'd') {
				clo->clo_pf->descr = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-descr", 6) == 0) {
				clo->clo_pf->descr = __get_val(&i, 7, argv);
			} else if (*p == 'f') {
				clo->clo_pf->file = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-file", 5) == 0) {
				clo->clo_pf->file = __get_val(&i, 6, argv);
			}
		} else if (clo->clo_pf->old == NULL) {
			/* must be the old name then */
			clo->clo_pf->old = argv[i];
			argv[i] = NULL;
		} else if (clo->clo_pf->new == NULL) {
			/* must be the new name then */
			clo->clo_pf->new = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_ls_tag_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		if (clo->ls_tag->mnemo == NULL) {
			/* must be name then */
			clo->ls_tag->mnemo = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_del_tag_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		if (clo->del_tag->mnemo == NULL) {
			/* must be the name then */
			clo->del_tag->mnemo = argv[i];
			argv[i] = NULL;
		} else if (clo->del_tag->tag == NULL) {
			/* must be the tag identifier then */
			clo->del_tag->tag = argv[i];
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
		case 'a': {
			/* apply */
			int new_argc = argc - i - 1;
			char **new_argv = argv + i + 1;
			if (strcmp(p, "pply") == 0) {
				clo->cmd = UMPF_CMD_APPLY;
				parse_set_poss_args(clo, new_argc, new_argv);
				continue;
			}
			break;
		}
		case 'l':
			/* list-pf/list-tag */
			if (strcmp(p, "ist-pf") == 0 ||
			    strcmp(p, "ist-pfs") == 0) {
				clo->cmd = UMPF_CMD_LIST_PF;
				continue;
			} else if (strcmp(p, "ist-tag") == 0 ||
				   strcmp(p, "ist-tags") == 0) {
				int new_argc = argc - i - 1;
				char **new_argv = argv + i + 1;
				clo->cmd = UMPF_CMD_LIST_TAG;
				parse_ls_tag_args(clo, new_argc, new_argv);
				continue;
			}
			break;
		case 'c':
			/* clo-pf */
			if (strcmp(p, "lo-pf") == 0) {
				int new_argc = argc - i - 1;
				char **new_argv = argv + i + 1;
				clo->cmd = UMPF_CMD_CLO_PF;
				parse_clo_pf_args(clo, new_argc, new_argv);
				continue;
			}
			break;
		case 'd':
			/* del-tag */
			if (strcmp(p, "el-tag") == 0) {
				int new_argc = argc - i - 1;
				char **new_argv = argv + i + 1;
				clo->cmd = UMPF_CMD_DEL_TAG;
				parse_del_tag_args(clo, new_argc, new_argv);
				continue;
			}
			break;			
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
check__ls_pf_args(struct __clo_s *UNUSED(clo))
{
	return 0;
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
	if (clo->set_sec->dfile) {
		/* check if file exists */
	}
	return 0;
}

static int
check__poss_args(struct __clo_s *clo)
{
	if (clo->set_poss->pf == NULL && clo->cmd == UMPF_CMD_GET_POSS) {
		/* allow empty mnemonic in SET_POSS mode but not in GET_POSS */
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->set_poss->date) {
		/* convert */
		clo->set_poss->stamp = from_zulu(clo->set_poss->date);
	}
	if (clo->set_poss->file) {
		/* check if file exists */
	}
	return 0;
}

static int
check__clo_pf_args(struct __clo_s *clo)
{
	if (clo->clo_pf->old == NULL || clo->clo_pf->new == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	return 0;
}


static int
check__ls_tag_args(struct __clo_s *clo)
{
	if (clo->ls_tag->mnemo == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	return 0;
}

static int
check__del_tag_args(struct __clo_s *clo)
{
	if (clo->del_tag->mnemo == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	} else if (clo->del_tag->tag == NULL) {
		fputs("tag identifier must not be NULL\n", stderr);
		return -1;
	} else if ((clo->del_tag->tag_id =
		    strtoul(clo->del_tag->tag, NULL, 10)) == 0) {
		fputs("tag identifier must be numeric\n", stderr);
		return -1;
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
	case UMPF_CMD_LIST_PF:
		if (check__ls_pf_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_NEW_PF:
	case UMPF_CMD_GET_PF:
	case UMPF_CMD_SET_PF:
		if (check__pf_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_NEW_SEC:
	case UMPF_CMD_GET_SEC:
	case UMPF_CMD_SET_SEC:
		if (check__sec_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_GET_POSS:
	case UMPF_CMD_SET_POSS:
	case UMPF_CMD_APPLY:
		if (check__poss_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_CLO_PF:
		if (check__clo_pf_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_LIST_TAG:
		if (check__ls_tag_args(&argi)) {
			goto usage_out;
		}
		break;
	case UMPF_CMD_DEL_TAG:
		if (check__del_tag_args(&argi)) {
			goto usage_out;
		}
		break;
	}

	/* now go go go */
	if (UNLIKELY((umpf_process(&argi)) < 0)) {
		return 1;
	}
	return 0;
usage_out:
	print_usage(argi.cmd);
	return 1;
}

/* umpf.c ends here */
