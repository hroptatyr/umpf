/*** umpp.c -- umpf preprocessor
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
#include <string.h>
#include <stdbool.h>

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


static umpf_msg_t
make_umpf_msg(void)
{
	umpf_msg_t res = calloc(sizeof(*res), 1);
	return res;
}

static void
free_umpf_msg(umpf_msg_t msg)
{
	if (msg->pf.name) {
		free(msg->pf.name);
	}
	free(msg);
	return;
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

static inline bool
__isspace(char c)
{
	switch (c) {
	case ' ':
	case '\t':
		return true;
	default:
		return false;
	}
}

static time_t
from_zulu(const char *buf)
{
	struct tm tm[1] = {{0}};

	/* skip over whitespace */
	for (; *buf && __isspace(*buf); buf++);

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

static umpf_msg_t
msg_meld_pos(umpf_msg_t msg, struct __ins_qty_s *iq)
{
	/* try and find the position first */
	for (size_t i = 0; i < msg->pf.nposs; i++) {
		if (strcmp(msg->pf.poss[i].ins->sym, iq->ins->sym) == 0) {
			msg->pf.poss[i].qty->_long += iq->qty->_long;
			msg->pf.poss[i].qty->_shrt += iq->qty->_shrt;
			return msg;
		}
	}
	/* otherwise create a new position */
	if (msg->pf.nposs % 4 == 0) {
		size_t iqsz = sizeof(*msg->pf.poss) * (msg->pf.nposs + 4);
		msg = realloc(msg, sizeof(*msg) + iqsz);
	}
	msg->pf.poss[msg->pf.nposs++] = *iq;
	return msg;
}

static void
__pr_pf_head(umpf_msg_t msg)
{
	fputs(":portfolio \"", stdout);
	if (msg->pf.name) {
		fputs(msg->pf.name, stdout);
	}
	fputs("\" :stamp ", stdout);
	fput_zulu(msg->pf.stamp, stdout);
	fputs(" :clear ", stdout);
	fput_date(msg->pf.clr_dt, stdout);
	fputc('\n', stdout);
}

static void
__pr_pf_pos(struct __ins_qty_s pos)
{
	fputs(pos.ins->sym, stdout);
	fprintf(stdout, "\t%.8g\t%.8g\n", pos.qty->_long, pos.qty->_shrt);
	return;
}


#if defined __INTEL_COMPILER
# pragma warning (disable:593)
# pragma warning (disable:181)
#endif	/* __INTEL_COMPILER */
#include "umpp-clo.h"
#include "umpp-clo.c"
#include "umpp-meld-clo.h"
#if defined __INTEL_COMPILER
# pragma warning (default:593)
# pragma warning (default:181)
#endif	/* __INTEL_COMPILER */

/* meld mode */
static int __attribute__((noinline))
meld(int argc, char *argv[], struct gengetopt_args_info *UNUSED(argi))
{
	struct meld_args_info margi[1];
	umpf_msg_t msg = NULL;
	size_t llen;
	char *line;
	int res = 0;

	if (meld_parser(argc, argv, margi)) {
		res = 1;
		goto out;
	}

	llen = 256;
	line = malloc(llen);

	msg = make_umpf_msg();
	umpf_set_msg_type(msg, UMPF_MSG_SET_PF);
	if (margi->pf_given) {
		msg->pf.name = strdup(margi->pf_arg);
	}
	if (margi->date_given) {
		msg->pf.stamp = from_zulu(margi->date_arg);
	} else {
		msg->pf.stamp = time(NULL);
	}

	for (size_t i = 1; i < margi->inputs_num; i++) {
		struct __ins_qty_s iq = {};
		const char *file = margi->inputs[i];
		FILE *f;

		if ((f = fopen(file, "r")) == NULL) {
			fprintf(stderr, "cannot process file `%s'\n", file);
			continue;
		}

		for (ssize_t nrd; (nrd = getline(&line, &llen, f)) >= 0;) {
			if (__frob_poss_line(&iq, line, nrd) >= 0) {
				msg = msg_meld_pos(msg, &iq);
			}
		}
	}

	if (msg->pf.name) {
		__pr_pf_head(msg);
	}
	for (size_t i = 0; i < msg->pf.nposs; i++) {
		struct __ins_qty_s pos = msg->pf.poss[i];

		__pr_pf_pos(pos);
		free(pos.ins->sym);
	}
	free_umpf_msg(msg);
	free(line);
out:
	meld_parser_free(margi);
	return res;
}


static void
__pr_help(void)
{
	puts(CMDLINE_PARSER_PACKAGE_NAME " " CMDLINE_PARSER_VERSION "\n");

	if (gengetopt_args_info_purpose && gengetopt_args_info_purpose[0]) {
		printf("%s\n\n", gengetopt_args_info_purpose);
	}
	if (gengetopt_args_info_usage && gengetopt_args_info_usage[0]) {
		printf("%s\n\n", gengetopt_args_info_usage);
	}
	if (gengetopt_args_info_description &&
	    gengetopt_args_info_description[0]) {
		printf("%s\n\n", gengetopt_args_info_description);
	}

	puts("Common options:");
	for (const char **p = gengetopt_args_info_help; *p; p++) {
		if (strncmp(*p, "      --full-help", 17)) {
			puts(*p);
		}
	}

	puts("\nCommands:");

	printf("  meld\n    %s\n", meld_args_info_description);
	for (const char **p = meld_args_info_help + 2; *p; p++) {
		putchar(' ');
		putchar(' ');
		puts(*p);
	}
	return;
}

int
main(int argc, char *argv[])
{
	struct gengetopt_args_info argi[1];
	int res = 0;

	if (cmdline_parser(argc, argv, argi)) {
		__pr_help();
		res = 1;
		goto out;
	} else if (argi->help_given) {
		__pr_help();
		res = 0;
		goto out;
	} else if (argi->inputs_num == 0) {
		fputs("command CMD must be specified, see --help\n\n", stderr);
		__pr_help();
		res = 1;
		goto out;
	}

	if (strcmp(argi->inputs[0], "meld") == 0) {
		/* meld mode, oooohooooh */
		res = meld(argc, argv, argi);
	}

out:
	cmdline_parser_free(argi);
	return res;
}

/* umpp.c ends here */
