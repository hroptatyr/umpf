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
	UMPF_CMD_NEW_SEC,
} umpf_cmd_t;

/* new_pf specific options */
struct __new_pf_clo_s {
	char *mnemo;
	char *descr;
};

struct __new_sec_clo_s {
	char *mnemo;
	char *descr;
	char *pf;
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
		struct __new_pf_clo_s new_pf[1];
		struct __new_sec_clo_s new_sec[1];
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
    -d, --descr=FILE         File with description to pass on.\n\
                             Use - for stdin\n\
\n\
  new-sec [OPTIONS] NAME     Register a new security NAME\n\
    -p, --pf=STRING          Name of the portfolio to register the\n\
                             security with.\n\
    -d, --descr=FILE         File with description to pass on.\n\
                             Use - for stdin\n\
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

	/* operate in non-blocking mode */
	setsock_nonblock(sock);
	return sock;
}

static int
umpf_connect(struct __clo_s *clo)
{
	return __connect(clo->pref_fam, clo->host, clo->port);
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
parse_new_pf_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d or --descr */
			if (*p == 'd') {
				clo->new_pf->descr = __get_val(&i, 2, argv);
			} else if (strncmp(p, "-descr", 6) == 0) {
				clo->new_pf->descr = __get_val(&i, 7, argv);
			}
		} else if (clo->new_pf->mnemo == NULL) {
			/* must be the name then */
			clo->new_pf->mnemo = argv[i];
			argv[i] = NULL;
		}
	}
	return;
}

static void
parse_new_sec_args(struct __clo_s *clo, int argc, char *argv[])
{
	for (int i = 0; i < argc; i++) {
		char *p = argv[i];

		if (*p++ == '-') {
			/* could be -d or --descr or -p or --pf */
			if (*p == 'd') {
				clo->new_sec->descr =
					__get_val(&i, 2, argv);
			} else if (strncmp(p, "-descr", 6) == 0) {
				clo->new_sec->descr =
					__get_val(&i, 7, argv);
			} else if (*p == 'p') {
				clo->new_sec->pf =
					__get_val(&i, 2, argv);
			} else if (strncmp(p, "-pf", 3) == 0) {
				clo->new_sec->pf = __get_val(&i, 4, argv);
			}
		} else if (clo->new_sec->mnemo == NULL) {
			/* must be the name then */
			clo->new_sec->mnemo = argv[i];
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
				parse_new_pf_args(clo, new_argc, new_argv);
				continue;

			} else if (strcmp(p, "ew-sec") == 0) {
				clo->cmd = UMPF_CMD_NEW_SEC;
				parse_new_sec_args(clo, new_argc, new_argv);
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
check_new_pf_args(struct __clo_s *clo)
{
	if (clo->cmd != UMPF_CMD_NEW_PF) {
		fputs("command must be NEW_PF\n", stderr);
		return -1;
	}
	if (clo->new_pf->mnemo == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	return 0;
}

static int
check_new_sec_args(struct __clo_s *clo)
{
	if (clo->cmd != UMPF_CMD_NEW_SEC) {
		fputs("command must be NEW_SEC\n", stderr);
		return -1;
	}
	if (clo->new_sec->pf == NULL) {
		fputs("portfolio mnemonic must not be NULL\n", stderr);
		return -1;
	}
	if (clo->new_sec->mnemo == NULL) {
		fputs("security mnemonic must not be NULL\n", stderr);
		return -1;
	}
	return 0;
}


int
main(int argc, char *argv[])
{
	struct __clo_s argi = {0};
	volatile int sock;

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
		if (check_new_pf_args(&argi)) {
			print_usage(argi.cmd);
			return 1;
		}
		break;
	case UMPF_CMD_NEW_SEC:
		if (check_new_sec_args(&argi)) {
			print_usage(argi.cmd);
			return 1;
		}
		break;
	}

	if ((sock = umpf_connect(&argi)) < 0) {
		return 1;
	};

	switch (argi.cmd) {
	case UMPF_CMD_NEW_PF:

	case UMPF_CMD_NEW_SEC:

	default:
		break;
	}

	close(sock);
	return 0;
}

/* umpf-new-pf.c ends here */
