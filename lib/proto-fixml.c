/*** proto-fixml.c -- reader/writer for fixml position messages
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
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#if defined HAVE_LIBXML2
# include <libxml/parser.h>
# include <libxml/parserInternals.h>
#endif	/* HAVE_LIBXML2 */
#include <math.h>
#include "nifty.h"
#include "proto-fixml.h"
#include "umpf-private.h"

/* gperf goodness */
#include "proto-fixml-tag.c"
#include "proto-fixml-attr.c"
#include "proto-fixml-ns.c"

#if defined __INTEL_COMPILER
# pragma warning (disable:2259)
#endif	/* __INTEL_COMPILER */
#if defined DEBUG_FLAG
# include <assert.h>
#else
# define assert(args...)
#endif	/* DEBUG_FLAG */

#if defined DEBUG_FLAG
# include <stdio.h>
# define PFIXML_DEBUG(args...)			\
	fprintf(stderr, "[umpf/fixml] " args)
# define PFIXML_DBGCONT(args...)			\
	fprintf(stderr, args)
#else  /* !DEBUG_FLAG */
# define PFIXML_DEBUG(args...)
# define PFIXML_DBGCONT(args...)
#endif	/* DEBUG_FLAG */

typedef struct __ctx_s *__ctx_t;
typedef xmlSAXHandler sax_hdl_s;
typedef sax_hdl_s *sax_hdl_t;
typedef struct umpf_ctxcb_s *umpf_ctxcb_t;
typedef struct umpf_ns_s *umpf_ns_t;

struct umpf_ns_s {
	char *pref;
	char *href;
	umpf_nsid_t nsid;
};

struct __satell_s {
	char *data;
	size_t size;
};

/* contextual callbacks */
struct umpf_ctxcb_s {
	/* for a linked list */
	umpf_ctxcb_t next;

	/* navigation info, stores the context */
	umpf_tid_t otype;
	union {
		void *object;
		long int objint;
	};
	umpf_ctxcb_t old_state;
};

struct __ctx_s {
	struct umpf_ns_s ns[16];
	size_t nns;
	/* stuff buf */
#define INITIAL_STUFF_BUF_SIZE	(4096)
	char *sbuf;
	size_t sbsz;
	size_t sbix;
	/* the current sax handler */
	sax_hdl_s hdl[1];
	/* parser state, for contextual callbacks */
	umpf_ctxcb_t state;
	/* pool of context trackers, implies maximum parsing depth */
	struct umpf_ctxcb_s ctxcb_pool[16];
	umpf_ctxcb_t ctxcb_head;

	/* pfix structure */
	struct pfix_fixml_s *fix;

	/* push parser */
	xmlParserCtxtPtr pp;
};

const char fixml50_ns_uri[] = "http://www.fixprotocol.org/FIXML-5-0";
const char fixml44_ns_uri[] = "http://www.fixprotocol.org/FIXML-4-4";

#if defined DEBUG_FLAG && 0
# include <stdint.h>
# undef xfree
static void
__dbg_free(void *ptr)
{
/* assume it's at least 8 bytes */
	*(uint64_t*)ptr = 0xDEADBEEFCAFEBABE;
	free(ptr);
	return;
}

# define xfree(x)	(__dbg_free(x), x = (void*)0xDEADBEEFCAFEBABEUL)
#endif	/* DEBUG_FLAG */


static void
init_ctxcb(__ctx_t ctx)
{
	memset(ctx->ctxcb_pool, 0, sizeof(ctx->ctxcb_pool));
	for (size_t i = 0; i < countof(ctx->ctxcb_pool) - 1; i++) {
		ctx->ctxcb_pool[i].next = ctx->ctxcb_pool + i + 1;
	}
	ctx->ctxcb_head = ctx->ctxcb_pool;
	return;
}

static umpf_ctxcb_t
pop_ctxcb(__ctx_t ctx)
{
	umpf_ctxcb_t res = ctx->ctxcb_head;

	if (LIKELY(res != NULL)) {
		ctx->ctxcb_head = res->next;
		memset(res, 0, sizeof(*res));
	}
	return res;
}

static void
push_ctxcb(__ctx_t ctx, umpf_ctxcb_t ctxcb)
{
	ctxcb->next = ctx->ctxcb_head;
	ctx->ctxcb_head = ctxcb;
	return;
}

static void
pop_state(__ctx_t ctx)
{
/* restore the previous current state */
	umpf_ctxcb_t curr = ctx->state;

	ctx->state = curr->old_state;
	/* queue him in our pool */
	push_ctxcb(ctx, curr);
	return;
}

static umpf_ctxcb_t
push_state(__ctx_t ctx, umpf_tid_t otype, void *object)
{
	umpf_ctxcb_t res = pop_ctxcb(ctx);

	/* stuff it with the object we want to keep track of */
	res->object = object;
	res->otype = otype;
	/* fiddle with the states in our context */
	res->old_state = ctx->state;
	ctx->state = res;
	return res;
}

static umpf_tid_t
get_state_otype(__ctx_t ctx)
{
	return ctx->state ? ctx->state->otype : UMPF_TAG_UNK;
}

static void* __attribute__((unused))
get_state_object(__ctx_t ctx)
{
	return ctx->state->object;
}

static void __attribute__((unused))
set_state_object(__ctx_t ctx, void *z)
{
	ctx->state->object = z;
	return;
}

static long int __attribute__((unused))
get_state_objint(__ctx_t ctx)
{
	return ctx->state->objint;
}

static void __attribute__((unused))
set_state_objint(__ctx_t ctx, long int z)
{
	ctx->state->objint = z;
	return;
}


static void __attribute__((unused))
zulu_stamp(char *buf, size_t bsz, time_t stamp)
{
	struct tm tm = {0};
	gmtime_r(&stamp, &tm);
	strftime(buf, bsz, "%Y-%m-%dT%H:%M:%S%z", &tm);
	return;
}

static void __attribute__((unused))
zulu_stamp_now(char *buf, size_t bsz)
{
	zulu_stamp(buf, bsz, time(NULL));
	return;
}

static time_t
get_zulu(const char *buf)
{
	struct tm tm[1] = {{0}};
	time_t res = -1;

	/* skip over whitespace */
	for (; *buf && isspace(*buf); buf++);

	if (strptime(buf, "%Y-%m-%dT%H:%M:%S%Z", tm)) {
		res = timegm(tm);
	} else if (strptime(buf, "%Y-%m-%dT%H:%M:%S", tm)) {
		res = timegm(tm);
	} else if (strptime(buf, "%Y-%m-%d", tm)) {
		res = timegm(tm);
	}
	return res;
}

static const char*
tag_massage(const char *tag)
{
/* return the real part of a (ns'd) tag or attribute,
 * i.e. foo:that_tag becomes that_tag */
	const char *p = strchr(tag, ':');

	if (p) {
		/* skip over ':' */
		return p + 1;
	}
	/* otherwise just return the tag as is */
	return tag;
}

static void
umpf_reg_ns(__ctx_t ctx, const char *pref, const char *href)
{
	if (ctx->nns >= countof(ctx->ns)) {
		fputs("too many name spaces\n", stderr);
		return;
	}

	if (UNLIKELY(href == NULL)) {
		/* bollocks, user MUST be a twat */
		return;
	}

	/* get us those lovely ns ids */
	{
		size_t ulen = strlen(href);
		const struct umpf_nsuri_s *n = __nsiddify(href, ulen);
		const umpf_nsid_t nsid = n ? n->nsid : UMPF_NS_UNK;

		switch (nsid) {
			size_t i;
		case UMPF_NS_FIXML_5_0:
			if (UNLIKELY(ctx->ns[0].href != NULL)) {
				/* fixml 4.4 must occupy the throne
				 * dispossess him */
				i = ctx->nns++;
				ctx->ns[i] = ctx->ns[0];
			}
		case UMPF_NS_FIXML_4_4:
			/* oh, it's our fave, copy the  */
			ctx->ns[0].pref = (pref ? strdup(pref) : NULL);
			ctx->ns[0].href = strdup(href);
			ctx->ns[0].nsid = nsid;
			break;

		case UMPF_NS_AOU_0_1:
			/* no special place */
		case UMPF_NS_MDDL_3_0:
			/* no special place */
		case UMPF_NS_UNK:
		default:
			i = ctx->nns++;
			ctx->ns[i].pref = pref ? strdup(pref) : NULL;
			ctx->ns[i].href = strdup(href);
			ctx->ns[i].nsid = nsid;
			break;
		}
	}
	return;
}

static void
umpf_init(__ctx_t ctx)
{
	/* initialise the ctxcb pool */
	init_ctxcb(ctx);
	return;
}

static bool
umpf_pref_p(__ctx_t ctx, const char *pref, size_t pref_len)
{
	/* we sorted our namespaces so that umpf is always at index 0 */
	if (UNLIKELY(ctx->ns[0].href == NULL)) {
		return false;

	} else if (LIKELY(ctx->ns[0].pref == NULL)) {
		/* prefix must not be set here either */
		return pref == NULL || pref_len == 0;

	} else if (UNLIKELY(pref_len == 0)) {
		/* this node's prefix is "" but we expect a prefix of
		 * length > 0 */
		return false;

	} else {
		/* special service for us because we're lazy:
		 * you can pass pref = "foo:" and say pref_len is 4
		 * easier to deal with when strings are const etc. */
		if (pref[pref_len - 1] == ':') {
			pref_len--;
		}
		return memcmp(pref, ctx->ns[0].pref, pref_len) == 0;
	}
}

static umpf_ns_t
__pref_to_ns(__ctx_t ctx, const char *pref, size_t pref_len)
{
	if (UNLIKELY(ctx->ns[0].nsid == UMPF_NS_UNK)) {
		/* bit of a hack innit? */
		ctx->ns->nsid = UMPF_NS_FIXML_5_0;
		return ctx->ns;

	} else if (LIKELY(pref_len == 0 && ctx->ns[0].pref == NULL)) {
		/* most common case when people use the default ns */
		return ctx->ns;
	}
	/* special service for us because we're lazy:
	 * you can pass pref = "foo:" and say pref_len is 4
	 * easier to deal with when strings are const etc. */
	if (pref[pref_len - 1] == ':') {
		pref_len--;
	}
	for (size_t i = (ctx->ns[0].pref == NULL); i < ctx->nns; i++) {
		if (strncmp(ctx->ns[i].pref, pref, pref_len) == 0) {
			return ctx->ns + i;
		}
	}
	return NULL;
}

static uint16_t
hex_digit_to_val(char c)
{
	if (c >= '0' && c <= '9') {
		return (uint16_t)(c - '0');
	} else if (c >= 'a' && c <= 'f') {
		return (uint16_t)(c - 'a' + 10);
	} else if (c >= 'A' && c <= 'F') {
		return (uint16_t)(c - 'A' + 10);
	} else {
		/* big error actually */
		return 0;
	}
}

static uint16_t
dec_digit_to_val(char c)
{
	if (c >= '0' && c <= '9') {
		return (uint16_t)(c - '0');
	} else {
		/* big bang */
		return 0;
	}
}

static size_t
unquotn(char **tgt, const char *src, size_t len)
{
/* return a copy of SRC with all entities replaced */
	const char *sp = src;
	char *rp;

	*tgt = rp = malloc(len + 1);

	do {
		size_t tmp;

		/* find next occurrence of stop set characters */
		if ((sp = memchr(src, '&', len)) == NULL) {
			sp = src + len;
		}
		/* write what we've got so far */
		tmp = sp - src;
		memcpy(rp, src, tmp);
		rp += tmp;
		len -= tmp;

		if (LIKELY(*sp == '\0')) {
			break;
		}

		/* check if it's an entity */
		tmp = (len < 8 ? len - 1 : 7);
		if (UNLIKELY(memchr(sp + 1, ';', tmp) == NULL)) {
			/* just copy the next TMP bytes */
			memcpy(rp, sp, tmp + 1);
			rp += tmp + 1;
			len -= tmp + 1;
			src = sp + tmp + 1;
			continue;
		}

		/* inspect the entity */
		switch (sp[1]) {
		case '#': {
			/* hex or dec representation */
			uint16_t a = 0;

			if (sp[2] == 'x') {
				/* hex */
				sp += 2;
				while (*sp != ';') {
					a *= 16;
					a += hex_digit_to_val(*sp++);
				}
				src = sp + 1;
				
			} else {
				/* dec */
				sp += 2;
				while (*sp != ';') {
					a *= 10;
					a += dec_digit_to_val(*sp++);
				}
				src = sp + 1;
			}
			/* prefer smaller chars */
			if (LIKELY(a < 256)) {
				*rp++ = (char)a;
			} else {
				*rp++ = (char)(a >> 8);
				*rp++ = (char)(a & 0xff);
			}
			break;
		}
		case 'a':
			/* could be &amp; or &apos; */
			if (sp[2] == 'm' && sp[3] == 'p' && sp[4] == ';') {
				*rp++ = '&';
				src = sp + 5;
			} else if (sp[2] == 'p' && sp[3] == 'o' &&
				   sp[4] == 's' && sp[5] == ';') {
				*rp++ = '\'';
				src = sp + 6;
			}
			break;
		case 'l':
			if (sp[2] == 't' && sp[3] == ';') {
				*rp++ = '<';
				src = sp + 4;
			}
			break;
		case 'g':
			if (sp[2] == 't' && sp[3] == ';') {
				*rp++ = '>';
				src = sp + 4;
			}
			break;
		case 'q':
			if (sp[2] == 'u' && sp[3] == 'o' &&
			    sp[4] == 't' && sp[5] == ';') {
				*rp++ = '"';
				src = sp + 6;
			}
			break;
		default:
			/* um */
			*rp++ = *sp++;
			src = sp;
			len--;
			break;
		}
		len -= (src - sp);
	} while (len > 0);
	*rp = '\0';
	return rp - *tgt;
}

static char*
unquot(const char *src)
{
/* return a copy of SRC with all entities replaced */
	size_t len = strlen(src);
	char *res;

	(void)unquotn(&res, src, len);
	return res;
}

static char
unquotc(const char *src)
{
/* return a copy of SRC with all entities replaced */
	const char *sp = src;

	/* find next occurrence of stop set characters */
	if (LIKELY(sp[0] != '&')) {
		return sp[0];
	} else if (LIKELY(*sp == '\0')) {
		return '\0';
	} else if (UNLIKELY(strchr(sp + 1, ';') == NULL)) {
		/* not an entity */
		return sp[0];
	}

	/* inspect the entity */
	switch (sp[1]) {
	case '#': {
		/* hex or dec representation */
		uint16_t a = 0;

		if (sp[2] == 'x') {
			/* hex */
			sp += 2;
			while (*sp != ';') {
				a *= 16;
				a += hex_digit_to_val(*sp++);
			}
		} else {
			/* dec */
			sp += 2;
			while (*sp != ';') {
				a *= 10;
				a += dec_digit_to_val(*sp++);
			}
		}
		/* prefer smaller chars */
		return (char)(a & 0xff);
	}
	case 'a':
		/* could be &amp; or &apos; */
		if (sp[2] == 'm' && sp[3] == 'p' && sp[4] == ';') {
			return '&';
		} else if (sp[2] == 'p' && sp[3] == 'o' &&
			   sp[4] == 's' && sp[5] == ';') {
			return '\'';
		}
		break;
	case 'l':
		if (sp[2] == 't' && sp[3] == ';') {
			return '<';
		}
		break;
	case 'g':
		if (sp[2] == 't' && sp[3] == ';') {
			return '>';
		}
		break;
	case 'q':
		if (sp[2] == 'u' && sp[3] == 'o' &&
		    sp[4] == 't' && sp[5] == ';') {
			return '"';
		}
		break;
	default:
		break;
	}
	return '\0';
}


/* xml deserialiser */
static void
__eat_ws_ass(struct pfix_glu_s *g, const char *d, size_t l)
{
	const char *p = d;

#if defined __INTEL_COMPILER
# pragma warning (disable:981)
#endif	/* __INTEL_COMPILER */

	/* strip leading and trailing whitespace */
	while (isspace(*p++));
	l -= --p - d;
	while (isspace(p[--l]));
	l++;

#if defined __INTEL_COMPILER
# pragma warning (default:981)
#endif	/* __INTEL_COMPILER */

	g->dlen = unquotn(&g->data, p, l);
	g->data[g->dlen] = '\0';
	return;
}

#define INITIAL_GBUF_SIZE	(4096UL)

static void
check_realloc(__ctx_t ctx, size_t len)
{
	if (UNLIKELY(ctx->sbix + len > ctx->sbsz)) {
		size_t new_sz = ctx->sbix + len + INITIAL_GBUF_SIZE;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;
		/* realloc now */
		ctx->sbuf = realloc(ctx->sbuf, ctx->sbsz = new_sz);

	} else if (UNLIKELY(ctx->sbix + len > -ctx->sbsz)) {
		/* now we need a malloc */
		char *old = ctx->sbuf;
		size_t new_sz = ctx->sbix + len + INITIAL_GBUF_SIZE;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;

		ctx->sbuf = malloc(ctx->sbsz = new_sz);
		memcpy(ctx->sbuf, old, ctx->sbix);
	}
	return;
}

static size_t
sputs(__ctx_t ctx, const char *src)
{
	size_t len = strlen(src);

	check_realloc(ctx, len);
	memcpy(ctx->sbuf + ctx->sbix, src, len);
	ctx->sbix += len;
	return len;
}

static void
snputs(__ctx_t ctx, const char *src, size_t len)
{
	check_realloc(ctx, len);
	memcpy(ctx->sbuf + ctx->sbix, src, len);
	ctx->sbix += len;
	return;
}

static void
sputc(__ctx_t ctx, const char c)
{
	check_realloc(ctx, 1);
	ctx->sbuf[ctx->sbix++] = c;
	return;
}

/* declare our variadic goodness */
static size_t
csnprintf(__ctx_t ctx, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
static size_t
csnprintf(__ctx_t ctx, const char *fmt, ...)
{
	va_list ap;
	size_t left;
	ssize_t len = 0;

	do {
		check_realloc(ctx, len + 1);
		left = ctx->sbsz - ctx->sbix;
		va_start(ap, fmt);
		len = vsnprintf(ctx->sbuf + ctx->sbix, left, fmt, ap);
		va_end(ap);
	} while (UNLIKELY(len == -1 || (size_t)len >= left));

	ctx->sbix += len;
	return len;
}

static void
print_indent(__ctx_t ctx, size_t indent)
{
	check_realloc(ctx, indent);
	memset(ctx->sbuf + ctx->sbix, ' ', indent);
	ctx->sbix += indent;
	return;
}

static void
print_zulu(__ctx_t ctx, time_t stamp)
{
	struct tm tm[1] = {{0}};

	check_realloc(ctx, 32);
	gmtime_r(&stamp, tm);
	ctx->sbix += strftime(ctx->sbuf + ctx->sbix, 32, "%FT%T%z", tm);
	return;
}

static void
print_date(__ctx_t ctx, time_t stamp)
{
	struct tm tm[1] = {{0}};

	check_realloc(ctx, 32);
	gmtime_r(&stamp, tm);
	ctx->sbix += strftime(ctx->sbuf + ctx->sbix, 32, "%F", tm);
	return;
}

static void
sputc_encq(__ctx_t ctx, char s)
{
	/* inspect the character */
	switch (s) {
	default:
		sputc(ctx, s);
	case '0':
		break;
	case '<':
		snputs(ctx, "&lt;", 4);
		break;
	case '>':
		snputs(ctx, "&gt;", 4);
		break;
	case '&':
		snputs(ctx, "&amp;", 5);
		break;
	case '\'':
		snputs(ctx, "&apos;", 6);
		break;
	case '"':
		snputs(ctx, "&quot;", 6);
		break;
	}
	return;
}

static void
snputs_enc(__ctx_t ctx, const char *s, size_t z)
{
	static const char stpset[] = "<>&";
	const char *sp = s;
	size_t idx;

	while (1) {
		/* find next occurrence of stop set characters */
		if ((idx = strcspn(sp, stpset)) >= z) {
			snputs(ctx, sp, z - (sp - s));
			return;
		}
		/* write what we've got */
		snputs(ctx, sp, idx);
		/* advance our buffer */
		sp += idx;
		/* inspect the character */
		switch (*sp++) {
		case '\0':
		default:
			PFIXML_DEBUG("unknown character in stop set\n");
			return;
		case '<':
			snputs(ctx, "&lt;", 4);
			break;
		case '>':
			snputs(ctx, "&gt;", 4);
			break;
		case '&':
			snputs(ctx, "&amp;", 5);
			break;
		}
	}
	/* not reached */
}

static void
sputs_encq(__ctx_t ctx, const char *s)
{
/* like fputs() but encode special chars */
	static const char stpset[] = "<>&'\"";
	size_t idx;

	while (1) {
		/* find next occurrence of stop set characters */
		idx = strcspn(s, stpset);
		/* write what we've got */
		snputs(ctx, s, idx);
		/* inspect the character */
		switch (s[idx]) {
		default:
		case '0':
			return;
		case '<':
			snputs(ctx, "&lt;", 4);
			break;
		case '>':
			snputs(ctx, "&gt;", 4);
			break;
		case '&':
			snputs(ctx, "&amp;", 5);
			break;
		case '\'':
			snputs(ctx, "&apos;", 6);
			break;
		case '"':
			snputs(ctx, "&quot;", 6);
			break;
		}
		/* advance our buffer */
		s += idx + sizeof(*s);
	}
	/* not reached */
}


static umpf_tid_t
sax_tid_from_tag(const char *tag)
{
	size_t tlen = strlen(tag);
	const struct umpf_tag_s *t = __tiddify(tag, tlen);
	return t ? t->tid : UMPF_TAG_UNK;
}

static umpf_aid_t
sax_aid_from_attr(const char *attr)
{
	size_t alen = strlen(attr);
	const struct umpf_attr_s *a = __aiddify(attr, alen);
	return a ? a->aid : UMPF_ATTR_UNK;
}

static void
proc_FIXML_xmlns(__ctx_t ctx, const char *pref, const char *value)
{
	PFIXML_DEBUG("reg'ging name space %s <- %s\n", pref, value);
	umpf_reg_ns(ctx, pref, value);
	return;
}

static void
proc_FIXML_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	umpf_aid_t aid;
	struct pfix_fixml_s *fix = get_state_object(ctx);

	if (UNLIKELY(rattr > attr && !umpf_pref_p(ctx, attr, rattr - attr))) {
		const struct umpf_attr_s *a = __aiddify(attr, rattr - attr - 1);
		aid = a ? a->aid : UMPF_ATTR_UNK;
	} else {
		aid = sax_aid_from_attr(rattr);
	}

	switch (aid) {
	case UMPF_ATTR_XMLNS:
		proc_FIXML_xmlns(ctx, rattr == attr ? NULL : rattr, value);
		break;
	case UMPF_ATTR_S:
	case UMPF_ATTR_R:
		/* we're so not interested in version mumbo jumbo */
		break;
	case UMPF_ATTR_V:
		fix->v = strdup(value);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static umpf_aid_t
check_attr(__ctx_t ctx, const char *attr)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFIXML_DEBUG("unknown namespace %s\n", attr);
		return UMPF_ATTR_UNK;
	}
	return aid;
}

static void
proc_REQ_FOR_POSS_attr(
	struct pfix_req_for_poss_s *rfp,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_RPT_ID:
		/* ignored */
		break;
	case UMPF_ATTR_SET_SES_ID:
		/* ignored */
		break;
	case UMPF_ATTR_REQ_TYP:
		rfp->req_typ = strtol(value, NULL, 10);
		break;
	case UMPF_ATTR_BIZ_DT:
		rfp->biz_dt = get_zulu(value);
		break;
	case UMPF_ATTR_TXN_TM:
		rfp->txn_tm = get_zulu(value);
		break;
	case UMPF_ATTR_RSLT:
		rfp->rslt = strtol(value, NULL, 10);
		break;
	case UMPF_ATTR_STAT:
		rfp->stat = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}


static void
proc_REQ_FOR_POSS_ACK_attr(
	struct pfix_req_for_poss_ack_s *rfpa,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_TOT_RPTS:
		rfpa->tot_rpts = strtol(value, NULL, 10);
		break;
	default:
		proc_REQ_FOR_POSS_attr(&rfpa->rfp, aid, value);
		break;
	}
	return;
}

static void
proc_RGST_INSTRCTNS_attr(
	struct pfix_rgst_instrctns_s *ri,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_ID:
		ri->id = unquot(value);
		break;
	case UMPF_ATTR_REF_ID:
		ri->ref_id = unquot(value);
		break;
	case UMPF_ATTR_TRANS_TYP:
		ri->trans_typ = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_RGST_INSTRCTNS_RSP_attr(
	struct pfix_rgst_instrctns_rsp_s *rsp,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_REG_STAT:
		rsp->reg_stat = unquotc(value);
		break;
	default:
		proc_RGST_INSTRCTNS_attr(&rsp->ri, aid, value);
		break;
	}
	return;
}

static void
proc_SUB_attr(struct pfix_sub_s *sub, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_ID:
		sub->id = unquot(value);
		break;
	case UMPF_ATTR_SRC:
		sub->src = value[0];
		break;
	case UMPF_ATTR_R:
		sub->r = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_PTY_attr(struct pfix_pty_s *pty, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	default:
		proc_SUB_attr(&pty->prim, aid, value);
		break;
	}
	return;
}

static void
proc_INSTRMT_attr(
	struct pfix_instrmt_s *ins, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_SYM: {
		ins->sym = unquot(value);
		break;
	}
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_QTY_attr(struct pfix_qty_s *qty, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_TYP:
		qty->typ = unquot(value);
		break;
	case UMPF_ATTR_LONG:
		qty->long_ = strtod(value, NULL);
		break;
	case UMPF_ATTR_SHORT:
		qty->short_ = strtod(value, NULL);
		break;
	case UMPF_ATTR_QTY_DT:
		qty->qty_dt = get_zulu(value);
		break;
	case UMPF_ATTR_STAT:
		qty->stat = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_POS_RPT_attr(
	struct pfix_pos_rpt_s *pr, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_RSLT:
		pr->rslt = strtol(value, NULL, 10);
		break;
	case UMPF_ATTR_REQ_TYP:
		pr->req_typ = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_SEC_DEF_all_attr(
	struct pfix_sec_def_s *sd, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_TXT:
		sd->txt = unquot(value);
		break;
	case UMPF_ATTR_TXN_TM:
		sd->txn_tm = get_zulu(value);
		break;
	case UMPF_ATTR_BIZ_DT:
		sd->biz_dt = get_zulu(value);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

#if 0
static void
proc_ALLOC_all_attr(__ctx_t ctx, const umpf_aid_t aid, const char *value)
{
	umpf_msg_t msg = ctx->msg;
	struct __ins_qty_s *qty = get_state_object(ctx);

	switch (aid) {
	case UMPF_ATTR_ID:
	case UMPF_ATTR_TYP:
	case UMPF_ATTR_TRD_DT:
		/* ignored */
		break;
	case UMPF_ATTR_TRANS_TYP:
		if (value && value[0] == '0' && value[1] == '\0') {
			break;
		}
		PFIXML_DEBUG("WARN: trans type != 0\n");
		break;
	case UMPF_ATTR_SIDE:
		if (value == NULL || value[1] != '\0') {
			goto warn;
		}
		switch (value[0]) {
		case '\0':
		case '1':
		case '3':
			qty->qsd->sd = QSIDE_OPEN_LONG;
			break;
		case '2':
		case '4':
			qty->qsd->sd = QSIDE_CLOSE_LONG;
			break;
		case '5':
		case '6':
			/* sell short (exempt) */
			qty->qsd->sd = QSIDE_OPEN_SHORT;
			break;
		case 'X':
			/* fucking FIXML has no field to denote
			 * closing of short positions */
			qty->qsd->sd = QSIDE_CLOSE_SHORT;
			break;
		default:
		warn:
			qty->qsd->sd = QSIDE_UNK;
			PFIXML_DEBUG("WARN: cannot interpret side\n");
			break;
		}
		break;

	case UMPF_ATTR_SETTL_DT:
		msg->pf.clr_dt = get_zulu(value);
		break;
	case UMPF_ATTR_QTY:
		qty->qsd->pos = strtod(value, NULL);
		break;
	case UMPF_ATTR_ACCT:
		if (msg->pf.name) {
			/* only instantiate once */
			break;
		}
		msg->pf.name = unquot(value);
		break;
	case UMPF_ATTR_TXN_TM:
		msg->pf.stamp = get_zulu(value);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}
#endif

static void
proc_APPL_MSG_REQ_all_attr(
	struct pfix_appl_msg_attr_s *ama,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_APPL_REQ_ID:
		ama->appl_req_id = unquot(value);
		break;
	case UMPF_ATTR_APPL_REQ_TYP:
		ama->appl_req_typ = strtol(value, NULL, 10);
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

#if 0
/* belongs somewhere else */
static uint64_t
get_SUB_ID_u64(__ctx_t ctx, const char **attrs)
{
	for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
		const umpf_aid_t a = check_attr(ctx, attrs[j]);
		if (a == UMPF_ATTR_ID) {
			return strtoul(attrs[j + 1], NULL, 10);
		}
	}
	return 0UL;
}

/* belongs somewhere else */
static void
__lst_tag_add_tag(__ctx_t ctx, tag_t tid)
{
	umpf_msg_t msg = ctx->msg;

	if (UNLIKELY(tid == 0)) {
		return;
	}
	/* check if we need to resize */
	if ((msg->lst_tag.ntags % 512) == 0) {
		msg = realloc(
			msg,
			sizeof(*msg) +
			(msg->lst_tag.ntags + 512) *
			sizeof(*msg->lst_tag.tags));
	}
	msg->lst_tag.tags[msg->lst_tag.ntags++] = tid;
	ctx->msg = msg;
	return;
}
#endif


static void
sax_bo_top_level_elt(__ctx_t ctx, const umpf_tid_t tid, const char **attrs)
{
	struct pfix_fixml_s *fix = ctx->fix;

	PFIXML_DEBUG("otype %u tid %u\n", get_state_otype(ctx), tid);
	assert(get_state_otype(ctx) == UMPF_TAG_FIXML);

	/* sigh, subtle differences */
	switch (tid) {
	case UMPF_TAG_REQ_FOR_POSS: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_req_for_poss_s *rfp = &b->req_for_poss;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_REQ_FOR_POSS_attr(rfp, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, rfp);
		break;
	}
	case UMPF_TAG_REQ_FOR_POSS_ACK: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_req_for_poss_ack_s *rfpa = &b->req_for_poss_ack;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_REQ_FOR_POSS_ACK_attr(rfpa, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, rfpa);
		break;
	}
	case UMPF_TAG_RGST_INSTRCTNS: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_rgst_instrctns_s *ri = &b->rgst_instrctns;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_RGST_INSTRCTNS_attr(ri, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ri);
		break;
	}
	case UMPF_TAG_RGST_INSTRCTNS_RSP: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_rgst_instrctns_rsp_s *rir = &b->rgst_instrctns_rsp;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_RGST_INSTRCTNS_RSP_attr(rir, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, rir);
		break;
	}
	case UMPF_TAG_POS_RPT: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_pos_rpt_s *pr = &b->pos_rpt;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_POS_RPT_attr(pr, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, pr);
		break;
	}
	case UMPF_TAG_SEC_DEF_REQ:
	case UMPF_TAG_SEC_DEF_UPD:
	case UMPF_TAG_SEC_DEF: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_sec_def_s *sd = &b->sec_def;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_SEC_DEF_all_attr(sd, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, sd);
		break;
	}

#if 0
	case UMPF_TAG_ALLOC_INSTRCTN: {
		/* for the instrument guy and the attr code */
		struct __ins_qty_s *iq = NULL;

		umpf_set_msg_type(msg, UMPF_MSG_PATCH);
		ctx->msg = msg = umpf_msg_add_pos(msg, 1);
		iq = msg->pf.poss + msg->pf.nposs - 1;
		(void)push_state(ctx, tid, iq);
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_ALLOC_all_attr(ctx, aid, attrs[j + 1]);
		}
		break;
	}
#endif
	case UMPF_TAG_APPL_MSG_REQ: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_appl_msg_req_s *amr = &b->appl_msg_req;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_APPL_MSG_REQ_all_attr(
				&amr->attr, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, amr);
		break;
	}
	case UMPF_TAG_APPL_MSG_REQ_ACK: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_appl_msg_req_ack_s *amra = &b->appl_msg_req_ack;

		b->tag = tid;
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_APPL_MSG_REQ_all_attr(
				&amra->attr, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, amra);
		break;
	}
	default:
		break;
	}
	return;
}

static void
sax_bo_FIXML_elt(__ctx_t ctx, const char *name, const char **attrs)
{
	const umpf_tid_t tid = sax_tid_from_tag(name);

	/* all the stuff that needs a new sax handler */
	switch (tid) {
	case UMPF_TAG_FIXML: {
		/* has got to be the root */
		assert(ctx->state == NULL);
		umpf_init(ctx);

		if (UNLIKELY(attrs == NULL)) {
			break;
		}

		/* generate a fix obj */
		ctx->fix = calloc(1, sizeof(*ctx->fix));
		push_state(ctx, tid, ctx->fix);

		for (int i = 0; attrs[i] != NULL; i += 2) {
			proc_FIXML_attr(ctx, attrs[i], attrs[i + 1]);
		}
		break;
	}

	case UMPF_TAG_BATCH:
		break;

	case UMPF_TAG_REQ_FOR_POSS:
		/* translate to get_pf/get_descr */
	case UMPF_TAG_REQ_FOR_POSS_ACK:
	case UMPF_TAG_POS_RPT:
		/* translate to set_pf */
	case UMPF_TAG_RGST_INSTRCTNS:
		/* translate to new_pf/set_descr */
	case UMPF_TAG_RGST_INSTRCTNS_RSP:
		/* translate to get_descr */
	case UMPF_TAG_SEC_DEF_REQ:
		/* translate to get_sec */
	case UMPF_TAG_SEC_DEF:
		/* translate to new_sec */
	case UMPF_TAG_SEC_DEF_UPD:
		/* translate to set_sec */
	case UMPF_TAG_ALLOC_INSTRCTN:
		/* translate to apply */
	case UMPF_TAG_ALLOC_INSTRCTN_ACK:
		/* translate to apply */
	case UMPF_TAG_APPL_MSG_REQ:
	case UMPF_TAG_APPL_MSG_REQ_ACK:
		/* translate to various user messages */
		sax_bo_top_level_elt(ctx, tid, attrs);
		break;

	case UMPF_TAG_APPL_ID_REQ_GRP: {
		struct pfix_appl_msg_req_s *amr = get_state_object(ctx);
		struct pfix_appl_id_req_grp_s *rg =
			appl_msg_req_add_air_grp(amr);

		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);

			/* only listen for RefApplID */
			if (aid != UMPF_ATTR_REF_APPL_ID) {
				continue;
			}
			PFIXML_DEBUG("found %s\n", attrs[j + 1]);
			rg->ref_appl_id = unquot(attrs[j + 1]);
		}
		(void)push_state(ctx, tid, rg);
		break;
	}
	case UMPF_TAG_APPL_ID_REQ_ACK_GRP: {
		struct pfix_appl_msg_req_ack_s *amra = get_state_object(ctx);
		struct pfix_appl_id_req_grp_s *rag =
			appl_msg_req_ack_add_aira_grp(amra);

		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);

			/* only listen for RefApplID */
			if (aid != UMPF_ATTR_REF_APPL_ID) {
				continue;
			}
			PFIXML_DEBUG("found %s\n", attrs[j + 1]);
			rag->ref_appl_id = unquot(attrs[j + 1]);
		}
		(void)push_state(ctx, tid, rag);
		break;
	}

	case UMPF_TAG_RG_DTL: {
		struct pfix_rgst_instrctns_s *ri = get_state_object(ctx);
		struct pfix_rg_dtl_s *rd;
		rd = rgst_instrctns_add_rg_dtl(ri);
		(void)push_state(ctx, tid, rd);
		break;
	}

	case UMPF_TAG_PTY: {
		/* context sensitive node, bummer */
		struct pfix_pty_s *pty;

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_RG_DTL: {
			struct pfix_rg_dtl_s *rd = get_state_object(ctx);
			pty = rg_dtl_add_pty(rd);
			break;
		}
		case UMPF_TAG_REQ_FOR_POSS: {
			struct pfix_req_for_poss_s *rfp =
				get_state_object(ctx);
			pty = req_for_poss_add_pty(rfp);
			break;
		}
		case UMPF_TAG_REQ_FOR_POSS_ACK: {
			struct pfix_req_for_poss_ack_s *rfpa =
				get_state_object(ctx);
			pty = req_for_poss_add_pty(&rfpa->rfp);
			break;
		}
		case UMPF_TAG_POS_RPT: {
			struct pfix_pos_rpt_s *pr = get_state_object(ctx);
			pty = pos_rpt_add_pty(pr);
			break;
		}
		case UMPF_TAG_APPL_ID_REQ_GRP:
		case UMPF_TAG_APPL_ID_REQ_ACK_GRP: {
			struct pfix_appl_id_req_grp_s *rg =
				get_state_object(ctx);
			pty = appl_id_req_grp_add_pty(rg);
			break;
		}
		default:
			pty = NULL;
			break;
		}
		(void)push_state(ctx, tid, pty);
		if (UNLIKELY(pty == NULL)) {
			break;
		}
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t a = check_attr(ctx, attrs[j]);
			proc_PTY_attr(pty, a, attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_SUB: {
		struct pfix_sub_s *sub;

		if (get_state_otype(ctx) == UMPF_TAG_PTY) {
			struct pfix_pty_s *pty = get_state_object(ctx);
			sub = pty_add_sub(pty);
		} else {
			/* sub as a child of something else? :O */
			sub = NULL;
		}

		if (UNLIKELY(sub == NULL)) {
			break;
		}
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t a = check_attr(ctx, attrs[j]);
			proc_SUB_attr(sub, a, attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_INSTRMT: {
		struct pfix_instrmt_s *ins;

		if (UNLIKELY(attrs == NULL)) {
			break;
		}

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_POS_RPT: {
			struct pfix_pos_rpt_s *pr = get_state_object(ctx);
			ins = pos_rpt_add_instrmt(pr);
			break;
		}
		case UMPF_TAG_SEC_DEF_UPD:
		case UMPF_TAG_SEC_DEF_REQ:
		case UMPF_TAG_SEC_DEF: {
			struct pfix_sec_def_s *sd = get_state_object(ctx);
			ins = sec_def_add_instrmt(sd);
			break;
		}
		default:
			/* add more here if need be */
			ins = NULL;
			break;
		}

		if (UNLIKELY(ins == NULL)) {
			break;
		}
		for (int j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t a = check_attr(ctx, attrs[j]);
			proc_INSTRMT_attr(ins, a, attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_QTY: {
		struct pfix_qty_s *qty;

		if (UNLIKELY(attrs == NULL)) {
			break;
		}

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_POS_RPT: {
			struct pfix_pos_rpt_s *pr = get_state_object(ctx);
			qty = pos_rpt_add_qty(pr);
			break;
		}
		default:
			/* add more here if need be */
			qty = NULL;
			break;
		}

		if (UNLIKELY(qty == NULL)) {
			break;
		}
		for (int j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t a = check_attr(ctx, attrs[j]);
			proc_QTY_attr(qty, a, attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_AMT:
		/* unsupported */
		PFIXML_DEBUG("Amt tags are currently unsupported\n");
		break;

	case UMPF_TAG_SEC_XML: {
		struct pfix_sec_xml_s *sx;

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_SEC_DEF_UPD:
		case UMPF_TAG_SEC_DEF_REQ:
		case UMPF_TAG_SEC_DEF: {
			struct pfix_sec_def_s *sd = get_state_object(ctx);
			sx = sd->sec_xml;
			break;
		}
		default:
			sx = NULL;
			break;
		}
		(void)push_state(ctx, tid, sx);
		break;
	}
#if 0
	case UMPF_TAG_ALLOC:
		/* just go through the attrs again */
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_ALLOC_all_attr(ctx, aid, attrs[j + 1]);
		}
		break;
#endif

	default:
		PFIXML_DEBUG("WARN: unknown tag %s\n", name);
		break;
	}

	PFIXML_DEBUG("STATE: %u <- %s\n", get_state_otype(ctx), name);
	return;
}

static void
sax_eo_FIXML_elt(__ctx_t ctx, const char *name)
{
	umpf_tid_t tid = sax_tid_from_tag(name);

	/* stuff that needed to be done, fix up state etc. */
	switch (tid) {
		/* top-leverls */
	case UMPF_TAG_REQ_FOR_POSS_ACK:
	case UMPF_TAG_POS_RPT:
	case UMPF_TAG_REQ_FOR_POSS:
	case UMPF_TAG_RGST_INSTRCTNS:
	case UMPF_TAG_RGST_INSTRCTNS_RSP:
	case UMPF_TAG_RG_DTL:
	case UMPF_TAG_SEC_DEF:
	case UMPF_TAG_SEC_DEF_REQ:
	case UMPF_TAG_SEC_DEF_UPD:
	case UMPF_TAG_ALLOC_INSTRCTN:
	case UMPF_TAG_APPL_MSG_REQ:
	case UMPF_TAG_APPL_MSG_REQ_ACK:
		pop_state(ctx);
		break;

		/* non top-levels */
	case UMPF_TAG_PTY:
	case UMPF_TAG_APPL_ID_REQ_GRP:
	case UMPF_TAG_APPL_ID_REQ_ACK_GRP:
	case UMPF_TAG_SEC_XML:
		pop_state(ctx);
		break;

		/* non top-levels without children */
	case UMPF_TAG_INSTRMT:
	case UMPF_TAG_QTY:
	case UMPF_TAG_SUB:
		/* noone dare push this */
		break;

	case UMPF_TAG_FIXML:
		/* finalise the document */
		assert(ctx->state != NULL);
		assert(ctx->state->otype == tid);
		assert(ctx->state->old_state == NULL);
		pop_state(ctx);
		assert(ctx->state == NULL);
	default:
		break;
	}

	PFIXML_DEBUG("STATE: %s -> %u\n", name, get_state_otype(ctx));
	return;
}


static size_t
__eat_glue(const char *src, size_t len, const char *cookie, size_t cklen)
{
	const char *end;

	if ((end = memmem(src, len + cklen, cookie, cklen)) != NULL) {
		PFIXML_DEBUG("found end tag, eating contents\n");
		return end - src;
	} else {
		PFIXML_DEBUG("end tag not found, eating all\n");
		return len;
	}
}

static size_t
__push_glue(__ctx_t ctx, const char *src, size_t len)
{
	const char *cookie = ctx->sbuf + sizeof(size_t);
	size_t cookie_len = ((size_t*)ctx->sbuf)[0];
	size_t consum;

	PFIXML_DEBUG("looking for %s %zu\n", cookie, cookie_len);
	consum = __eat_glue(src, len, cookie, cookie_len);

	/* maybe realloc first? */
	if (UNLIKELY(ctx->sbix + consum + 1 >= ctx->sbsz)) {
		size_t new_sz = ctx->sbix + consum + 1;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;
		/* realloc now */
		ctx->sbuf = realloc(ctx->sbuf, ctx->sbsz = new_sz);
	}

	/* stuff chunk into our buffer */
	memcpy(ctx->sbuf + ctx->sbix, src, consum);
	ctx->sbuf[ctx->sbix += consum] = '\0';
	PFIXML_DEBUG("pushed %zu\n", consum);
	return consum;
}

static void
sax_stuff_buf_AOU_push(__ctx_t ctx, const char *ch, int len)
{
/* should be called only in glue mode */
	size_t consumed;
	/* libxml2 specific! */
	size_t max_len = ctx->pp->input->end - ctx->pp->input->cur;

	/* push what we've got */
	consumed = __push_glue(ctx, ch, max_len);

	/* libxml2 specific stuff,
	 * HACK, cheat on our push parser */
	PFIXML_DEBUG("eating %zu bytes from libxml's buffer\n", consumed);
	if (consumed < max_len) {
		/* we mustn't wind too far */
		ctx->pp->input->cur += consumed - len;
	} else {
		/* everything's gone, just wind to the end */
		ctx->pp->input->cur += consumed;
	}
	/* put into parser end tag mode, libxml2 must see </ now */
	ctx->pp->instate = XML_PARSER_END_TAG;
	return;
}

/* hackery */
#define AOU_CONT_OFFS	(32)

static void
sax_bo_AOU_elt(
	__ctx_t ctx, umpf_ns_t UNUSED(ns),
	const char *name, const char **UNUSED(attrs))
{
	const umpf_tid_t tid = sax_tid_from_tag(name);

	switch (tid) {
	case UMPF_TAG_GLUE: {
		struct pfix_fixml_s *fix = ctx->fix;

		/* actually this is the only one we support */
		PFIXML_DEBUG("GLUE\n");

		if (UNLIKELY(fix == NULL)) {
			PFIXML_DEBUG("fix NULL, glue is meaningless\n");
			break;
		}
		switch (get_state_otype(ctx)) {
			/* all the nodes that have the glue */
		case UMPF_TAG_PTY:
		case UMPF_TAG_SUB: {
			struct pfix_sub_s *s = get_state_object(ctx);
			struct pfix_glu_s *g = &s->glu;

			if (UNLIKELY(g->data != NULL)) {
				/* someone else, prob us, was faster */
				break;
			}
			/* the glue code wants a pointer to the satellite */
			(void)push_state(ctx, UMPF_TAG_GLUE, g);
			goto glue_setup;
		}
		case UMPF_TAG_SEC_XML: {
			struct pfix_sec_xml_s *sx = get_state_object(ctx);
			struct pfix_glu_s *g;

			if (UNLIKELY(sx == NULL ||
				     (g = &sx->glu)->data != NULL)) {
				break;
			}
			/* the glue code wants a pointer to the satellite */
			(void)push_state(ctx, UMPF_TAG_GLUE, g);
			goto glue_setup;
		}
		default:
			PFIXML_DEBUG("no glue slot, glue is meaningless\n");
			break;
		}
		break;

		glue_setup:
		/* libxml specific */
		ctx->pp->sax->characters =
			(charactersSAXFunc)sax_stuff_buf_AOU_push;
		/* help the stuff buf pusher and
		 * construct the end tag for him */
		{
			char *tmp = ctx->sbuf + sizeof(size_t);

			*tmp++ = '<';
			*tmp++ = '/';
			tmp = stpcpy(tmp, ns->pref);
			*tmp++ = ':';
			*tmp++ = 'g';
			*tmp++ = 'l';
			*tmp++ = 'u';
			*tmp++ = 'e';
			*tmp++ = '>';
			*tmp = '\0';
			((size_t*)ctx->sbuf)[0] =
				tmp - ctx->sbuf - sizeof(size_t);
			/* reset our stuff buffer */
			ctx->sbix = AOU_CONT_OFFS;
		}
		break;
	}
	default:
		break;
	}
	return;
}

static void
sax_eo_AOU_elt(__ctx_t ctx, const char *name)
{
	const umpf_tid_t tid = sax_tid_from_tag(name);

	switch (tid) {
	case UMPF_TAG_GLUE: {
		struct pfix_glu_s *ptr;
		size_t len = ctx->sbix - AOU_CONT_OFFS;

		PFIXML_DEBUG("/GLUE\n");

		/* reset stuff buffer */
		ctx->sbix = 0;
		/* unsubscribe stuff buffer cb */
		ctx->pp->sax->characters = NULL;

		if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_GLUE ||
			     (ptr = get_state_object(ctx)) == NULL)) {
			break;
		}

		/* frob contents, eat whitespace and assign */
		__eat_ws_ass(ptr, ctx->sbuf + AOU_CONT_OFFS, len);
		/* job done, back to normal */
		pop_state(ctx);
		break;
	}
	default:
		break;
	}
	return;
}


static void
sax_bo_elt(__ctx_t ctx, const char *name, const char **attrs)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	umpf_ns_t ns = __pref_to_ns(ctx, name, rname - name);

	if (UNLIKELY(ns == NULL)) {
		PFIXML_DEBUG("unknown prefix in tag %s\n", name);
		return;
	}

	switch (ns->nsid) {
	case UMPF_NS_FIXML_5_0:
	case UMPF_NS_FIXML_4_4:
		sax_bo_FIXML_elt(ctx, rname, attrs);
		break;

	case UMPF_NS_AOU_0_1:
		sax_bo_AOU_elt(ctx, ns, rname, attrs);
		break;

	case UMPF_NS_MDDL_3_0:
		PFIXML_DEBUG("can't parse mddl yet (%s)\n", rname);
		break;

	case UMPF_NS_UNK:
	default:
		PFIXML_DEBUG("unknown namespace %s (%s)\n", name, ns->href);
		break;
	}
	return;
}

static void
sax_eo_elt(__ctx_t ctx, const char *name)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	umpf_ns_t ns = __pref_to_ns(ctx, name, rname - name);

	if (UNLIKELY(ns == NULL)) {
		PFIXML_DEBUG("unknown prefix in tag %s\n", name);
		return;
	}

	switch (ns->nsid) {
	case UMPF_NS_FIXML_5_0:
	case UMPF_NS_FIXML_4_4:
		sax_eo_FIXML_elt(ctx, rname);
		break;

	case UMPF_NS_AOU_0_1:
		sax_eo_AOU_elt(ctx, rname);
		break;

	case UMPF_NS_MDDL_3_0:
		PFIXML_DEBUG("can't parse mddl yet (%s)\n", rname);
		break;

	case UMPF_NS_UNK:
	default:
		PFIXML_DEBUG("unknown namespace %s (%s)\n", name, ns->href);
		break;
	}
	return;
}

static xmlEntityPtr
sax_get_ent(void *UNUSED(user_data), const xmlChar *name)
{
	return xmlGetPredefinedEntity(name);
}

/* the actual parser */
static int
parse_file(__ctx_t ctx, const char *file)
{
	int res;

	/* fill in the minimalistic sax handler to begin with */
	ctx->hdl->startElement = (startElementSAXFunc)sax_bo_elt;
	ctx->hdl->endElement = (endElementSAXFunc)sax_eo_elt;
	ctx->hdl->getEntity = sax_get_ent;

	res = xmlSAXUserParseFile(ctx->hdl, ctx, file);
	return res;
}

enum {
	BLOB_READY,
	BLOB_ERROR = -1,
	BLOB_M_PLZ = -2,
};

static int
final_blob_p(__ctx_t ctx)
{
/* return 1 if we need more blobs, 0 if this was the final blob, -1 on error */
	if (ctx->fix != NULL && ctx->state == NULL) {
		/* we're ready */
		PFIXML_DEBUG("seems ready\n");
		return xmlParseChunk(ctx->pp, ctx->sbuf, 0, 1);
	}
	PFIXML_DEBUG("%p %u\n", ctx->fix, get_state_otype(ctx));
	/* request more data */
	return BLOB_M_PLZ;
}

static int
parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	int res;

	switch (get_state_otype(ctx)) {
		size_t cns;
	case UMPF_TAG_GLUE:
		/* better not to push parse this guy
		 * call our stuff buf pusher instead */
		PFIXML_DEBUG("GLUE direct\n");
		if ((cns = __push_glue(ctx, buf, bsz)) < bsz) {
			/* oh, we need to wind our buffers */
			buf += cns;
			bsz -= cns;
		} else {
			res = 0;
		}
		PFIXML_DEBUG("GLUE consumed %zu\n", cns);
	default:
		res = (xmlParseChunk(ctx->pp, buf, bsz, bsz == 0) == 0) - 1;
	}
	return res;
}

static int
parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	ctx->pp = xmlCreatePushParserCtxt(ctx->hdl, ctx, buf, 0, NULL);
	return parse_more_blob(ctx, buf, bsz);
}


/* printers */
static void
pfix_print_txn_tm(__ctx_t ctx, idttz_t txn_tm)
{
	if (txn_tm > 0) {
		sputs(ctx, " TxnTm=\"");
		print_zulu(ctx, txn_tm);
		sputc(ctx, '"');
	}
	return;
}

static void
pfix_print_biz_dt(__ctx_t ctx, idate_t biz_dt)
{
	if (biz_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, biz_dt);
		sputc(ctx, '"');
	}
	return;
}

static void
pfix_print_glu(__ctx_t ctx, struct pfix_glu_s *g, size_t indent)
{
	static const char hdr[] = "<aou:glue content-type=\"text/plain\">\n";
	static const char ftr[] = "</aou:glue>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	snputs_enc(ctx, g->data, g->dlen);

	if (g->data[g->dlen - 1] != '\n') {
		sputc(ctx, '\n');
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_instrmt(__ctx_t ctx, struct pfix_instrmt_s *ins, size_t indent)
{
	print_indent(ctx, indent);
	csnprintf(ctx, "<Instrmt Sym=\"%s\"/>\n", ins->sym);
	return;
}

static void
pfix_print_qty(__ctx_t ctx, struct pfix_qty_s *qty, size_t indent)
{
	static const char hdr[] = "<Qty";
	static const char ftr[] = "/>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (qty->typ) {
		sputs(ctx, " Typ=\"");
		sputs_encq(ctx, qty->typ);
		sputc(ctx, '"');
	}

	if (qty->qty_dt) {
		sputs(ctx, " QtyDt=\"");
		print_zulu(ctx, qty->qty_dt);
	}

	csnprintf(ctx, " Long=\"%a\"", qty->long_);
	csnprintf(ctx, " Short=\"%a\"", qty->short_);
	csnprintf(ctx, " Stat=\"%d\"", qty->stat);

	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
__print_sub_attr(__ctx_t ctx, struct pfix_sub_s *sub)
{
	if (sub->id) {
		sputs(ctx, " ID=\"");
		sputs_encq(ctx, sub->id);
		sputc(ctx, '"');
	}

	if (sub->src) {
		sputs(ctx, " Sub=\"");
		sputc_encq(ctx, sub->src);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " R=\"%d\"", sub->r);
	return;
}

static void
pfix_print_sub(__ctx_t ctx, struct pfix_sub_s *sub, size_t indent)
{
	static const char hdr[] = "<Sub";
	static const char ftr[] = "</Sub>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	__print_sub_attr(ctx, sub);

	/* just finish the tag */
	if (LIKELY(sub->glu.data == NULL)) {
		sputs(ctx, "/>\n");
	} else {
		sputs(ctx, ">\n");
		pfix_print_glu(ctx, &sub->glu, indent + 2);
		snputs(ctx, ftr, countof_m1(ftr));
	}
	return;
}

static void
pfix_print_pty(__ctx_t ctx, struct pfix_pty_s *pty, size_t indent)
{
	static const char hdr[] = "<Pty";
	static const char ftr[] = "</Pty>\n";
	static const char altftr[] = "/>\n";
	bool big_ftr_p = pty->nsub > 0 || pty->prim.glu.data != NULL;

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	__print_sub_attr(ctx, &pty->prim);

	/* finish the tag preliminarily */
	if (big_ftr_p) {
		snputs(ctx, ">\n", 2);
	}

	for (size_t i = 0; i < pty->nsub; i++) {
		pfix_print_sub(ctx, pty->sub + i, indent + 2);
	}

	if (UNLIKELY(pty->prim.glu.data != NULL)) {
		pfix_print_glu(ctx, &pty->prim.glu, indent + 2);
	}

	if (big_ftr_p) {
		print_indent(ctx, indent);
		snputs(ctx, ftr, countof_m1(ftr));
	} else {
		/* just finish the tag */
		snputs(ctx, altftr, countof_m1(altftr));
	}
	return;
}

static void
pfix_print_rg_dtl(__ctx_t ctx, struct pfix_rg_dtl_s *rd, size_t indent)
{
	static const char hdr[] = "<RgDtl>\n";
	static const char ftr[] = "</RgDtl>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	for (size_t i = 0; i < rd->npty; i++) {
		pfix_print_pty(ctx, rd->pty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_sec_xml(__ctx_t ctx, struct pfix_sec_xml_s *sx, size_t indent)
{
	static const char hdr[] = "<SecXML>\n";
	static const char ftr[] = "</SecXML>\n";

	if (sx->glu.dlen > 0) {
		print_indent(ctx, indent);
		snputs(ctx, hdr, countof_m1(hdr));

		pfix_print_glu(ctx, &sx->glu, indent + 2);

		print_indent(ctx, indent);
		snputs(ctx, ftr, countof_m1(ftr));
	}
	return;
}

static void
pfix_print_appl_id_req_grp(
	__ctx_t ctx, struct pfix_appl_id_req_grp_s *air, size_t indent)
{
	static const char hdr[] = "<ApplIDReqGrp";
	static const char ftr[] = "</ApplIDReqGrp>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (air->ref_appl_id) {
		sputs(ctx, " RefApplID=\"");
		sputs(ctx, air->ref_appl_id);
		sputc(ctx, '"');
	}

	if (air->ref_id) {
		sputs(ctx, " RefID=\"");
		sputs(ctx, air->ref_id);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < air->npty; i++) {
		pfix_print_pty(ctx, air->pty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_appl_id_req_ack_grp(
	__ctx_t ctx, struct pfix_appl_id_req_grp_s *aira, size_t indent)
{
	static const char hdr[] = "<ApplIDReqAckGrp";
	static const char ftr[] = "</ApplIDReqAckGrp>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (aira->ref_appl_id) {
		sputs(ctx, " RefApplID=\"");
		sputs(ctx, aira->ref_appl_id);
		sputc(ctx, '"');
	}

	if (aira->ref_id) {
		sputs(ctx, " RefID=\"");
		sputs(ctx, aira->ref_id);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < aira->npty; i++) {
		pfix_print_pty(ctx, aira->pty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

/* top-level */
static void
pfix_print_req_for_poss(
	__ctx_t ctx, struct pfix_req_for_poss_s *rfp, size_t indent)
{
	static const char hdr[] = "<ReqForPoss";
	static const char ftr[] = "</ReqForPoss>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	csnprintf(ctx, " ReqTyp=\"%d\"", rfp->req_typ);

	pfix_print_biz_dt(ctx, rfp->biz_dt);
	pfix_print_txn_tm(ctx, rfp->txn_tm);

	/* finalise the tag */
	sputs(ctx, ">\n");

	for (size_t i = 0; i < rfp->npty; i++) {
		pfix_print_pty(ctx, rfp->pty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_req_for_poss_ack(
	__ctx_t ctx, struct pfix_req_for_poss_ack_s *rfpa, size_t indent)
{
	static const char hdr[] = "<ReqForPossAck";
	static const char ftr[] = "</ReqForPossAck>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	csnprintf(ctx, " ReqTyp=\"%d\"", rfpa->rfp.req_typ);
	csnprintf(ctx, " TotRpts=\"%d\"", rfpa->tot_rpts);

	pfix_print_biz_dt(ctx, rfpa->rfp.biz_dt);
	pfix_print_txn_tm(ctx, rfpa->rfp.txn_tm);

	/* finalise the tag */
	sputs(ctx, ">\n");

	for (size_t i = 0; i < rfpa->rfp.npty; i++) {
		pfix_print_pty(ctx, rfpa->rfp.pty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_pos_rpt(
	__ctx_t ctx, struct pfix_pos_rpt_s *pr, size_t indent)
{
	static const char hdr[] = "<PosRpt";
	static const char ftr[] = "</PosRpt>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	csnprintf(ctx, " Rslt=\"%d\"", pr->rslt);
	csnprintf(ctx, " ReqTyp=\"%d\"", pr->req_typ);

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < pr->npty; i++) {
		pfix_print_pty(ctx, pr->pty + i, indent + 2);
	}

	for (size_t i = 0; i < pr->ninstrmt; i++) {
		pfix_print_instrmt(ctx, pr->instrmt + i, indent + 2);
	}

	for (size_t i = 0; i < pr->nqty; i++) {
		pfix_print_qty(ctx, pr->qty + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_rgst_instrctns(
	__ctx_t ctx, struct pfix_rgst_instrctns_s *ri, size_t indent)
{
	static const char hdr[] = "<RgstInstrctns";
	static const char ftr[] = "</RgstInstrctns>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	csnprintf(ctx, " TransTyp=\"%d\"", ri->trans_typ);

	if (ri->ref_id) {
		sputs(ctx, " RefID=\"");
		sputs_encq(ctx, ri->ref_id);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < ri->nrg_dtl; i++) {
		pfix_print_rg_dtl(ctx, ri->rg_dtl + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_rgst_instrctns_rsp(
	__ctx_t ctx, struct pfix_rgst_instrctns_rsp_s *rir, size_t indent)
{
	static const char hdr[] = "<RgstInstrctnsRsp";
	static const char ftr[] = "</RgstInstrctnsRsp>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	csnprintf(ctx, " TransTyp=\"%d\"", rir->ri.trans_typ);

	if (rir->ri.id) {
		sputs(ctx, " ID=\"");
		sputs_encq(ctx, rir->ri.id);
		sputc(ctx, '"');
	}

	if (rir->reg_stat) {
		sputs(ctx, " RegStat=\"");
		sputc_encq(ctx, rir->reg_stat);
		sputc(ctx, '"');
	}

	if (rir->ri.ref_id) {
		sputs(ctx, " RefID=\"");
		sputs_encq(ctx, rir->ri.ref_id);
		sputc(ctx, '"');
	}

	if (rir->ri.nrg_dtl > 0) {
		/* finalise the tag */
		snputs(ctx, ">\n", 2);
	}
	for (size_t i = 0; i < rir->ri.nrg_dtl; i++) {
		pfix_print_rg_dtl(ctx, rir->ri.rg_dtl + i, indent + 2);
	}

	if (rir->ri.nrg_dtl > 0) {
		print_indent(ctx, indent);
		snputs(ctx, ftr, countof_m1(ftr));
	} else {
		snputs(ctx, "/>\n", 3);
	}
	return;
}

static void
__print_sec_def_all(
	__ctx_t ctx, struct pfix_sec_def_s *sd,
	const char *hdr, size_t nhdr, const char *ftr, size_t nftr,
	size_t indent)
{
	print_indent(ctx, indent);
	snputs(ctx, hdr, nhdr);

	if (sd->rpt_id) {
		sputs(ctx, " RptID=\"");
		sputs(ctx, sd->rpt_id);
		sputc(ctx, '"');
	}

	if (sd->req_id) {
		sputs(ctx, " ReqID=\"");
		sputs(ctx, sd->req_id);
		sputc(ctx, '"');
	}

	if (sd->rsp_id) {
		sputs(ctx, " RspID=\"");
		sputs(ctx, sd->rsp_id);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " ReqTyp=\"%d\"", sd->req_typ);
	csnprintf(ctx, " RspTyp=\"%d\"", sd->rsp_typ);

	if (sd->txt) {
		sputs(ctx, " Txt=\"");
		sputs(ctx, sd->txt);
		sputc(ctx, '"');
	}

	pfix_print_biz_dt(ctx, sd->biz_dt);
	pfix_print_txn_tm(ctx, sd->txn_tm);

	if (sd->ninstrmt != 0 || sd->sec_xml->glu.dlen > 0) {
		/* finalise the tag */
		snputs(ctx, ">\n", 2);

		pfix_print_sec_xml(ctx, sd->sec_xml, indent + 2);

		for (size_t i = 0; i < sd->ninstrmt; i++) {
			pfix_print_instrmt(ctx, sd->instrmt + i, indent + 2);
		}

		print_indent(ctx, indent);
		snputs(ctx, ftr, nftr);
	} else {
		/* finalise the tag */
		snputs(ctx, "/>\n", 3);
	}
	return;
}

static void
pfix_print_sec_def(__ctx_t ctx, struct pfix_sec_def_s *sd, size_t indent)
{
	static const char hdr[] = "<SecDef";
	static const char ftr[] = "</SecDef>\n";

	__print_sec_def_all(
		ctx, sd, hdr, countof_m1(hdr), ftr, countof_m1(ftr), indent);
	return;
}

static void
pfix_print_sec_def_req(__ctx_t ctx, struct pfix_sec_def_req_s *sdr, size_t ind)
{
	static const char hdr[] = "<SecDefReq";
	static const char ftr[] = "</SecDefReq>\n";

	__print_sec_def_all(
		ctx, &sdr->sec_def,
		hdr, countof_m1(hdr), ftr, countof_m1(ftr), ind);
	return;
}

static void
pfix_print_sec_def_upd(__ctx_t ctx, struct pfix_sec_def_upd_s *sdu, size_t ind)
{
	static const char hdr[] = "<SecDefUpd";
	static const char ftr[] = "</SecDefUpd>\n";

	__print_sec_def_all(
		ctx, &sdu->sec_def,
		hdr, countof_m1(hdr), ftr, countof_m1(ftr), ind);
	return;
}

static void
pfix_print_appl_msg_req(
	__ctx_t ctx, struct pfix_appl_msg_req_s *amr, size_t indent)
{
	static const char hdr[] = "<ApplMsgReq";
	static const char ftr[] = "</ApplMsgReq>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (amr->attr.appl_req_id) {
		sputs(ctx, " ApplReqID=\"");
		sputs(ctx, amr->attr.appl_req_id);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " ApplReqTyp=\"%d\"", amr->attr.appl_req_typ);

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < amr->nair_grp; i++) {
		pfix_print_appl_id_req_grp(
			ctx, amr->air_grp + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_appl_msg_req_ack(
	__ctx_t ctx, struct pfix_appl_msg_req_ack_s *amra, size_t indent)
{
	static const char hdr[] = "<ApplMsgReqAck";
	static const char ftr[] = "</ApplMsgReqAck>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (amra->attr.appl_req_id) {
		sputs(ctx, " ApplReqID=\"");
		sputs(ctx, amra->attr.appl_req_id);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " ApplReqTyp=\"%d\"", amra->attr.appl_req_typ);

	/* finalise the tag */
	snputs(ctx, ">\n", 2);

	for (size_t i = 0; i < amra->naira_grp; i++) {
		pfix_print_appl_id_req_ack_grp(
			ctx, amra->aira_grp + i, indent + 2);
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_batch(__ctx_t ctx, struct pfix_batch_s *b, size_t ind)
{
	switch (b->tag) {
	case UMPF_TAG_REQ_FOR_POSS:
		pfix_print_req_for_poss(ctx, &b->req_for_poss, ind);
		break;
	case UMPF_TAG_REQ_FOR_POSS_ACK:
		pfix_print_req_for_poss_ack(ctx, &b->req_for_poss_ack, ind);
		break;
	case UMPF_TAG_POS_RPT:
		pfix_print_pos_rpt(ctx, &b->pos_rpt, ind);
		break;
	case UMPF_TAG_RGST_INSTRCTNS:
		pfix_print_rgst_instrctns(ctx, &b->rgst_instrctns, ind);
		break;
	case UMPF_TAG_RGST_INSTRCTNS_RSP:
		pfix_print_rgst_instrctns_rsp(ctx, &b->rgst_instrctns_rsp, ind);
		break;
	case UMPF_TAG_SEC_DEF:
		pfix_print_sec_def(ctx, &b->sec_def, ind);
		break;
	case UMPF_TAG_SEC_DEF_REQ:
		pfix_print_sec_def_req(ctx, &b->sec_def_req, ind);
		break;
	case UMPF_TAG_SEC_DEF_UPD:
		pfix_print_sec_def_upd(ctx, &b->sec_def_upd, ind);
		break;
	case UMPF_TAG_APPL_MSG_REQ:
		pfix_print_appl_msg_req(ctx, &b->appl_msg_req, ind);
		break;
	case UMPF_TAG_APPL_MSG_REQ_ACK:
		pfix_print_appl_msg_req_ack(ctx, &b->appl_msg_req_ack, ind);
		break;
	default:
		PFIXML_DEBUG("cannot print tag %u\n", b->tag);
		break;
	}
	return;
}

static void
pfix_print_fixml(pfix_ctx_t ctx, struct pfix_fixml_s *f, size_t indent)
{
	static const char hdr[] = "\
<FIXML xmlns=\"http://www.fixprotocol.org/FIXML-5-0\"\n\
  xmlns:aou=\"http://www.ga-group.nl/aou-0.1\"\n\
  v=\"5.0\" aou:version=\"0.1\">\n";
	static const char ftr[] = "\
</FIXML>\n";
	static const char batch_hdr[] = "<Batch>\n";
	static const char batch_ftr[] = "</Batch>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	PFIXML_DEBUG("%zu\n", f->nbatch);

	if (f->nbatch > 1) {
		print_indent(ctx, indent + 2);
		snputs(ctx, batch_hdr, countof_m1(batch_hdr));
		indent += 2;
	}
		
	for (size_t i = 0; i < f->nbatch; i++) {
		pfix_print_batch(ctx, f->batch + i, indent + 2);
	}

	if (f->nbatch > 1) {
		print_indent(ctx, indent);
		snputs(ctx, batch_ftr, countof_m1(batch_ftr));
		indent -= 2;
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}


/* dtors */
static void
pfix_free_glu(struct pfix_glu_s *g)
{
	safe_xfree(g->data);
	g->dlen = 0UL;
	return;
}

static void
pfix_free_sub(struct pfix_sub_s *s)
{
	safe_xfree(s->id);
	pfix_free_glu(&s->glu);
	return;
}

static void
pfix_free_pty(struct pfix_pty_s *p)
{
	pfix_free_sub(&p->prim);
	for (size_t i = 0; i < p->nsub; i++) {
		pfix_free_sub(p->sub + i);
	}
	safe_xfree(p->sub);
	return;
}

static void
pfix_free_instrmt(struct pfix_instrmt_s *ins)
{
	safe_xfree(ins->sym);
	return;
}

static void
pfix_free_qty(struct pfix_qty_s *qty)
{
	safe_xfree(qty->typ);
	return;
}

static void
pfix_free_rg_dtl(struct pfix_rg_dtl_s *rd)
{
	for (size_t i = 0; i < rd->npty; i++) {
		pfix_free_pty(rd->pty + i);
	}
	safe_xfree(rd->pty);
	return;
}

static void
pfix_free_appl_id_req_grp(struct pfix_appl_id_req_grp_s *air)
{
	safe_xfree(air->ref_appl_id);
	safe_xfree(air->ref_id);
	for (size_t i = 0; i < air->npty; i++) {
		pfix_free_pty(air->pty + i);
	}
	safe_xfree(air->pty);
	return;
}

/* top levels */
static void
pfix_free_rgst_instrctns(struct pfix_rgst_instrctns_s *ri)
{
	safe_xfree(ri->ref_id);
	safe_xfree(ri->id);
	for (size_t i = 0; i < ri->nrg_dtl; i++) {
		pfix_free_rg_dtl(ri->rg_dtl + i);
	}
	safe_xfree(ri->rg_dtl);
	return;
}

static void
pfix_free_rgst_instrctns_rsp(struct pfix_rgst_instrctns_rsp_s *rir)
{
	pfix_free_rgst_instrctns(&rir->ri);
	return;
}

static void
pfix_free_sec_def(struct pfix_sec_def_s *sd)
{
	safe_xfree(sd->txt);
	safe_xfree(sd->rpt_id);
	safe_xfree(sd->req_id);
	safe_xfree(sd->rsp_id);
	pfix_free_glu(&sd->sec_xml->glu);
	for (size_t i = 0; i < sd->ninstrmt; i++) {
		pfix_free_instrmt(sd->instrmt + i);
	}
	safe_xfree(sd->instrmt);
	return;
}

static void
pfix_free_sec_def_upd(struct pfix_sec_def_upd_s *sdu)
{
	pfix_free_sec_def(&sdu->sec_def);
	return;
}

static void
pfix_free_sec_def_req(struct pfix_sec_def_req_s *sdr)
{
	pfix_free_sec_def(&sdr->sec_def);
	return;
}

static void
pfix_free_pos_rpt(struct pfix_pos_rpt_s *pr)
{
	for (size_t i = 0; i < pr->npty; i++) {
		pfix_free_pty(pr->pty + i);
	}
	safe_xfree(pr->pty);
	for (size_t i = 0; i < pr->ninstrmt; i++) {
		pfix_free_instrmt(pr->instrmt + i);
	}
	safe_xfree(pr->instrmt);
	for (size_t i = 0; i < pr->nqty; i++) {
		pfix_free_qty(pr->qty + i);
	}
	safe_xfree(pr->qty);
	return;
}

static void
pfix_free_req_for_poss(struct pfix_req_for_poss_s *rfp)
{
	for (size_t i = 0; i < rfp->npty; i++) {
		pfix_free_pty(rfp->pty + i);
	}
	safe_xfree(rfp->pty);
	return;
}

static void
pfix_free_req_for_poss_ack(struct pfix_req_for_poss_ack_s *rfpa)
{
	pfix_free_req_for_poss(&rfpa->rfp);
	return;
}

static void
pfix_free_appl_msg_req(struct pfix_appl_msg_req_s *amr)
{
	safe_xfree(amr->attr.appl_req_id);
	for (size_t i = 0; i < amr->nair_grp; i++) {
		pfix_free_appl_id_req_grp(amr->air_grp + i);
	}
	safe_xfree(amr->air_grp);
	return;
}

static void
pfix_free_appl_msg_req_ack(struct pfix_appl_msg_req_ack_s *amra)
{
	safe_xfree(amra->attr.appl_req_id);
	for (size_t i = 0; i < amra->naira_grp; i++) {
		pfix_free_appl_id_req_grp(amra->aira_grp + i);
	}
	safe_xfree(amra->aira_grp);
	return;
}

static void
pfix_free_batch(struct pfix_batch_s *b)
{
	switch (b->tag) {
	case UMPF_TAG_REQ_FOR_POSS:
		pfix_free_req_for_poss(&b->req_for_poss);
		break;
	case UMPF_TAG_REQ_FOR_POSS_ACK:
		pfix_free_req_for_poss_ack(&b->req_for_poss_ack);
		break;
	case UMPF_TAG_RGST_INSTRCTNS:
		pfix_free_rgst_instrctns(&b->rgst_instrctns);
		break;
	case UMPF_TAG_RGST_INSTRCTNS_RSP:
		pfix_free_rgst_instrctns_rsp(&b->rgst_instrctns_rsp);
		break;
	case UMPF_TAG_SEC_DEF:
		pfix_free_sec_def(&b->sec_def);
		break;
	case UMPF_TAG_SEC_DEF_REQ:
		pfix_free_sec_def_req(&b->sec_def_req);
		break;
	case UMPF_TAG_SEC_DEF_UPD:
		pfix_free_sec_def_upd(&b->sec_def_upd);
		break;
	case UMPF_TAG_POS_RPT:
		pfix_free_pos_rpt(&b->pos_rpt);
		break;
	case UMPF_TAG_APPL_MSG_REQ:
		pfix_free_appl_msg_req(&b->appl_msg_req);
		break;
	case UMPF_TAG_APPL_MSG_REQ_ACK:
		pfix_free_appl_msg_req_ack(&b->appl_msg_req_ack);
		break;
	default:
		break;
	}
	return;
}

static void
pfix_free_fixml(struct pfix_fixml_s *f)
{
	/* loads of leaks, do me properly */
	safe_xfree(f->ns);
	safe_xfree(f->attr);

	for (size_t i = 0; i < f->nbatch; i++) {
		pfix_free_batch(f->batch + i);
	}
	safe_xfree(f->batch);
	return;
}


/* external stuff and helpers */
static void
init(__ctx_t ctx)
{
	/* wipe some slots */
	ctx->fix = NULL;

	/* initialise the stuff buffer */
	ctx->sbuf = realloc(ctx->sbuf, ctx->sbsz = INITIAL_STUFF_BUF_SIZE);
	ctx->sbix = 0;

	/* we always reserve one name space slot for FIXML */
	ctx->nns = 1;

	/* fill in the minimalistic sax handler to begin with */
	ctx->hdl->startElement = (startElementSAXFunc)sax_bo_elt;
	ctx->hdl->endElement = (endElementSAXFunc)sax_eo_elt;
	ctx->hdl->getEntity = sax_get_ent;
	return;
}

static void
deinit(__ctx_t ctx)
{
	if (ctx->fix) {
		/* reset the document, leak if not free()'d */
		ctx->fix = NULL;
	}

	for (size_t i = 0; i < ctx->nns; i++) {
		if (ctx->ns[i].pref) {
			xfree(ctx->ns[i].pref);
		}
		ctx->ns[i].pref = NULL;
		if (ctx->ns[i].href) {
			xfree(ctx->ns[i].href);
		}
		ctx->ns[i].href = NULL;
	}
	ctx->nns = 1;

	if (ctx->pp) {
		xmlFreeParserCtxt(ctx->pp);
	}
	ctx->pp = NULL;
	return;
}

static void
free_ctx(__ctx_t ctx)
{
	if (ctx->pp) {
		xmlFreeParserCtxt(ctx->pp);
	}
	if (ctx->sbuf) {
		xfree(ctx->sbuf);
	}
	xfree(ctx);
	return;
}

static umpf_fix_t
__pfix_parse_file(__ctx_t ctx, const char *file)
{
	umpf_fix_t res;

	init(ctx);
	PFIXML_DEBUG("parsing %s\n", file);
	if (LIKELY(parse_file(ctx, file) == 0)) {
		PFIXML_DEBUG("done\n");
		res = ctx->fix;
	} else {
		PFIXML_DEBUG("failed\n");
		res = NULL;
	}
	deinit(ctx);
	return res;
}

umpf_fix_t
pfix_parse_file(const char *file)
{
	static struct __ctx_s ctx[1] = {{0}};
	return __pfix_parse_file(ctx, file);
}

umpf_fix_t
pfix_parse_file_r(const char *file)
{
	__ctx_t ctx = calloc(1, sizeof(*ctx));
	umpf_fix_t res = __pfix_parse_file(ctx, file);
	free_ctx(ctx);
	return res;
}

/* blob parsing */
static bool
ctx_deinitted_p(__ctx_t ctx)
{
	return ctx->pp == NULL;
}

static umpf_fix_t
check_ret(__ctx_t ctx, int ret)
{
	umpf_fix_t res;

	if (ret == 0) {
		ret = final_blob_p(ctx);
	} else {
		ret = BLOB_ERROR;
	}

	switch (ret) {
	case BLOB_READY:
		PFIXML_DEBUG("done\n");
		res = ctx->fix;
		break;
	case BLOB_M_PLZ:
		PFIXML_DEBUG("more\n");
		return NULL;
	default:
	case BLOB_ERROR:
		/* error of some sort */
		PFIXML_DEBUG("failed\n");
		res = NULL;
		break;
	}
	deinit(ctx);
	return res;
}

static umpf_fix_t
__pfix_parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	init(ctx);
	PFIXML_DEBUG("parsing blob of size %zu\n", bsz);
	return check_ret(ctx, parse_blob(ctx, buf, bsz));
}

static umpf_fix_t
__pfix_parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	PFIXML_DEBUG("parsing more blob of size %zu\n", bsz);
	return check_ret(ctx, parse_more_blob(ctx, buf, bsz));
}

umpf_fix_t
pfix_parse_blob(pfix_ctx_t *ctx, const char *buf, size_t bsz)
{
	static struct __ctx_s __ctx[1] = {{0}};
	umpf_fix_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = __ctx;
		res = __pfix_parse_blob(*ctx, buf, bsz);
	} else {
		res = __pfix_parse_more_blob(*ctx, buf, bsz);
	}

	if (ctx_deinitted_p(*ctx)) {
		*ctx = NULL;
	}
	return res;
}

umpf_fix_t
pfix_parse_blob_r(pfix_ctx_t *ctx, const char *buf, size_t bsz)
{
	umpf_fix_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = calloc(1, sizeof(struct __ctx_s));
		res = __pfix_parse_blob(*ctx, buf, bsz);
	} else {
		res = __pfix_parse_more_blob(*ctx, buf, bsz);
	}

	if (ctx_deinitted_p(*ctx)) {
		free_ctx(*ctx);
		*ctx = NULL;
	}
	return res;
}


/* serialiser (printer) */
static const char xml_hdr[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

size_t
pfix_seria_fix(char **tgt, size_t tsz, umpf_fix_t fix)
{
	struct __ctx_s ctx[1];

	ctx->sbuf = *tgt;
	ctx->sbsz = tsz;
	ctx->sbix = 0;

	snputs(ctx, xml_hdr, countof_m1(xml_hdr));
	pfix_print_fixml(ctx, fix, 0);

	/* finish off with a \nul byte */
	check_realloc(ctx, 1);
	ctx->sbuf[ctx->sbix] = '\0';
	*tgt = ctx->sbuf;
	return ctx->sbix;
}


/* dtors */
void
pfix_free_fix(umpf_fix_t fix)
{
	pfix_free_fixml(fix);
	xfree(fix);
	return;
}

/* proto-xml.c ends here */
