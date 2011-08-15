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
#include "umpf.h"
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
	/* the main message we're building */
	umpf_msg_t msg;
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

static void*
get_state_object_if(__ctx_t ctx, umpf_tid_t otype)
{
/* like get_state_object() but return NULL if types do not match */
	if (LIKELY(get_state_otype(ctx) == otype)) {
		return get_state_object(ctx);
	}
	return NULL;
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

static char*
unquot(const char *src)
{
/* return a copy of SRC with all entities replaced */
	size_t len = strlen(src);
	char *res = malloc(len);
	const char *sp = src;
	char *rp = res;

	while (1) {
		size_t tmp;

		/* find next occurrence of stop set characters */
		sp = strchrnul(src, '&');
		/* write what we've got so far */
		tmp = sp - src;
		memcpy(rp, src, tmp);
		rp += tmp;

		if (LIKELY(*sp == '\0')) {
			*rp = '\0';
			return res;
		}

		/* check if it's an entity */
		tmp = (len - tmp < 8 ? len - tmp - 1 : 7);
		if (UNLIKELY(memchr(sp + 1, ';', tmp) == NULL)) {
			/* just copy the next TMP bytes */
			memcpy(rp, sp, tmp + 1);
			rp += tmp + 1;
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
		case 'q':
			if (sp[2] == 'u' && sp[3] == 'o' &&
			    sp[4] == 't' && sp[5] == ';') {
				*rp++ = '"';
				src = sp + 6;
			}
		default:
			/* um */
			*rp++ = *sp++;
			src = sp;
			break;
		}
	}
	/* not reached */
}


/* structure aware helpers, move to lib? */
#define ADDF(__sup, __str, __slot, __inc)		\
static __str*						\
__sup##_add_##__slot(struct pfix_##__sup##_s *o)	\
{							\
	size_t idx = (o)->n##__slot;			\
	__str *res;					\
	if (UNLIKELY(idx % (__inc) == 0)) {		\
		(o)->__slot = realloc(			\
			(o)->__slot,			\
			(idx + (__inc)) *		\
			sizeof(*(o)->__slot));		\
		/* rinse */				\
		res = (o)->__slot + idx;		\
		memset(					\
			res, 0,				\
			(__inc) *			\
			sizeof(*(o)->__slot));		\
	} else {					\
		res = (o)->__slot + idx;		\
	}						\
	return res;					\
}							\
struct pfix_##__sup##_meth_s {				\
	__str*(*add_f)(struct pfix_##__sup##_s *o);	\
}


static void
__eat_ws_ass(struct __satell_s *sat, const char *d, size_t l)
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

	sat->data = malloc((sat->size = l) + 1);
	memcpy(sat->data, p, l);
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

static void __attribute__((unused))
sputs_enc(__ctx_t ctx, const char *s)
{
	static const char stpset[] = "<>&";
	size_t idx;

	while (1) {
		/* find next occurrence of stop set characters */
		idx = strcspn(s, stpset);
		/* write what we've got */
		snputs(ctx, s, idx);
		/* inspect the character */
		switch (s[idx]) {
		default:
		case '\0':
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
		/* advance our buffer */
		s += idx + sizeof(*s);
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
proc_RGST_INSTRCTNS_RSP_attr(
	struct pfix_rgst_instrctns_rsp_s *rsp,
	const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_ID:
		rsp->id = unquot(value);
		break;
	case UMPF_ATTR_TRANS_TYP:
		rsp->trans_typ = strtol(value, NULL, 10);
		break;
	case UMPF_ATTR_REG_STAT:
		/* ignored */
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
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
proc_INSTRMT_attr(__ctx_t ctx, const umpf_aid_t aid, const char *value)
{
	switch (aid) {
	case UMPF_ATTR_SYM: {
		struct __ins_s *ins = get_state_object(ctx);
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
proc_QTY_attr(__ctx_t ctx, const umpf_aid_t aid, const char *value)
{
	struct __ins_qty_s *iq = get_state_object(ctx);

	switch (aid) {
	case UMPF_ATTR_TYP:
		/* ignored */
		break;
	case UMPF_ATTR_LONG:
		iq->qty->_long = strtod(value, NULL);
		break;
	case UMPF_ATTR_SHORT:
		iq->qty->_shrt = strtod(value, NULL);
		break;
	case UMPF_ATTR_QTY_DT:
		/* ignored */
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

static void
proc_SEC_DEF_all_attr(__ctx_t ctx, const umpf_aid_t aid, const char *value)
{
	umpf_msg_t msg = ctx->msg;

	switch (aid) {
	case UMPF_ATTR_TXT:
		msg->new_sec.pf_mnemo = unquot(value);
		break;
	case UMPF_ATTR_TXN_TM:
		/* ignored */
		break;
	default:
		PFIXML_DEBUG("WARN: unknown attr %u\n", aid);
		break;
	}
	return;
}

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

/* adder for batches, step is 16 */
ADDF(fixml, struct pfix_batch_s, batch, 16);
ADDF(rgst_instrctns, struct pfix_rg_dtl_s, rg_dtl, 4);
ADDF(rg_dtl, struct pfix_pty_s, pty, 4);


static void
sax_bo_top_level_elt(__ctx_t ctx, const umpf_tid_t tid, const char **attrs)
{
	umpf_msg_t msg = ctx->msg;
	struct pfix_fixml_s *fix = ctx->fix;

	assert(ctx->msg != NULL);
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
	case UMPF_TAG_SEC_DEF_REQ:
		umpf_set_msg_type(msg, UMPF_MSG_GET_SEC);
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_SEC_DEF_all_attr(ctx, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

	case UMPF_TAG_SEC_DEF_UPD:
		umpf_set_msg_type(msg, UMPF_MSG_SET_SEC);
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_SEC_DEF_all_attr(ctx, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

	case UMPF_TAG_SEC_DEF:
		umpf_set_msg_type(msg, UMPF_MSG_NEW_SEC);
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_SEC_DEF_all_attr(ctx, aid, attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

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

		/* generate a massage */
		ctx->msg = calloc(1, sizeof(*ctx->msg));
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
		sax_bo_top_level_elt(ctx, tid, attrs);
		break;

	case UMPF_TAG_APPL_MSG_REQ:
	case UMPF_TAG_APPL_MSG_REQ_ACK:
		/* just a container around the really interesting bit */
		break;

	case UMPF_TAG_APPL_ID_REQ_GRP:
	case UMPF_TAG_APPL_ID_REQ_ACK_GRP:
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			/* only listen for RefApplID */
			if (aid != UMPF_ATTR_REF_APPL_ID) {
				continue;
			}
			PFIXML_DEBUG("found %s\n", attrs[j + 1]);
			if (strcmp(attrs[j + 1], "lst_tag") == 0) {
				umpf_set_msg_type(ctx->msg, UMPF_MSG_LST_TAG);
			}
		}
		(void)push_state(ctx, tid, ctx->msg);
		break;

	case UMPF_TAG_POS_RPT: {
		/* part of get/set pf */
		umpf_msg_t msg;
		struct __ins_qty_s *iq = NULL;

		if (UNLIKELY((msg = ctx->msg) == NULL)) {
			;
		} else if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_FIXML)) {
			;
		} else if ((size_t)get_state_objint(ctx) < msg->pf.nposs) {
			iq = msg->pf.poss + get_state_objint(ctx);
		}
		(void)push_state(ctx, tid, iq);
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

		case UMPF_TAG_REQ_FOR_POSS:
		case UMPF_TAG_REQ_FOR_POSS_ACK:
		case UMPF_TAG_APPL_ID_REQ_GRP:
		case UMPF_TAG_APPL_ID_REQ_ACK_GRP:
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
		tag_t tag;

		/* context sensitive node, bummer */
		if (ctx->state == NULL || ctx->state->old_state == NULL) {
			break;
		}

		tag = get_SUB_ID_u64(ctx, attrs);

		/* we need to look up our grand-parent because our parent
		 * is a Pty */
		switch (ctx->state->old_state->otype) {
		case UMPF_TAG_REQ_FOR_POSS:
		case UMPF_TAG_REQ_FOR_POSS_ACK:
			ctx->msg->pf.tag_id = tag;
			break;
		case UMPF_TAG_APPL_ID_REQ_GRP:
		case UMPF_TAG_APPL_ID_REQ_ACK_GRP: 
			/* add tid */
			__lst_tag_add_tag(ctx, tag);
			break;
		default:
			break;
		}
		break;
	}

	case UMPF_TAG_INSTRMT: 

		if (UNLIKELY(attrs == NULL)) {
			break;
		}

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_POS_RPT:
		case UMPF_TAG_SEC_DEF:
		case UMPF_TAG_SEC_DEF_REQ:
		case UMPF_TAG_SEC_DEF_UPD:
		case UMPF_TAG_ALLOC_INSTRCTN:
			/* we use the fact that __ins_qty_s == __ins_s
			 * in posrpt mode and in sec-def mode we rely
			 * on the right push there */
			for (int j = 0; attrs && attrs[j] != NULL; j += 2) {
				const umpf_aid_t a = check_attr(ctx, attrs[j]);
				proc_INSTRMT_attr(ctx, a, attrs[j + 1]);
			}
			break;
		default:
			PFIXML_DEBUG("WARN: Instrmt in unknown context\n");
			break;
		}
		break;

	case UMPF_TAG_QTY: {
		/* check that we're inside a PosRpt context */
		struct __ins_qty_s *iq =
			get_state_object_if(ctx, UMPF_TAG_POS_RPT);

		if (UNLIKELY(iq == NULL)) {
			PFIXML_DEBUG("WARN: Qty outside of PosRpt\n");
			break;
		}

		for (int j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_QTY_attr(ctx, aid, attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_AMT:
		/* unsupported */
		PFIXML_DEBUG("Amt tags are currently unsupported\n");
		break;

	case UMPF_TAG_SEC_XML:
		/* it's just a no-op */
		break;

	case UMPF_TAG_ALLOC:
		/* just go through the attrs again */
		for (size_t j = 0; attrs && attrs[j] != NULL; j += 2) {
			const umpf_aid_t aid = check_attr(ctx, attrs[j]);
			proc_ALLOC_all_attr(ctx, aid, attrs[j + 1]);
		}
		break;

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
	case UMPF_TAG_REQ_FOR_POSS_ACK:
		/* need to zilch out the objid as PosRpt relies on it */
		pop_state(ctx);
		set_state_objint(ctx, 0);
		break;
	case UMPF_TAG_POS_RPT:
		/* need to zilch out the objid as PosRpt relies on it */
		pop_state(ctx);
		set_state_objint(ctx, get_state_objint(ctx) + 1);
		break;
	case UMPF_TAG_REQ_FOR_POSS:
	case UMPF_TAG_RGST_INSTRCTNS:
	case UMPF_TAG_RGST_INSTRCTNS_RSP:
	case UMPF_TAG_RG_DTL:
	case UMPF_TAG_PTY:
	case UMPF_TAG_SEC_DEF:
	case UMPF_TAG_SEC_DEF_REQ:
	case UMPF_TAG_SEC_DEF_UPD:
	case UMPF_TAG_ALLOC_INSTRCTN:
	case UMPF_TAG_APPL_ID_REQ_GRP:
	case UMPF_TAG_APPL_ID_REQ_ACK_GRP:
		pop_state(ctx);
		break;
	case UMPF_TAG_SUB:
		/* noone dare push this */
		break;
	case UMPF_TAG_APPL_MSG_REQ:
	case UMPF_TAG_APPL_MSG_REQ_ACK:
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
	__ctx_t ctx, umpf_ns_t ns, const char *name, const char **UNUSED(attrs))
{
	const umpf_tid_t tid = sax_tid_from_tag(name);

	switch (tid) {
	case UMPF_TAG_GLUE: {
		umpf_msg_t msg = ctx->msg;

		/* actually this is the only one we support */
		PFIXML_DEBUG("GLUE\n");

		if (UNLIKELY(msg == NULL)) {
			PFIXML_DEBUG("msg NULL, glue is meaningless\n");
			break;
		}

		switch (umpf_get_msg_type(msg)) {
		case UMPF_MSG_NEW_PF: {
			/* ah, finally, glue indeed is supported here */
			void *ptr;

			if (UNLIKELY(msg->new_pf.satellite->data != NULL)) {
				/* someone else, prob us, was faster */
				break;
			}
			/* the glue code wants a pointer to the satellite */
			ptr = msg->new_pf.satellite;
			(void)push_state(ctx, UMPF_TAG_GLUE, ptr);
			goto glue_setup;
		}
		case UMPF_MSG_NEW_SEC:
		case UMPF_MSG_SET_SEC: {
			/* ah, finally, glue indeed is supported here */
			void *ptr;

			if (UNLIKELY(msg->new_sec.satellite->data != NULL)) {
				/* someone else, prob us, was faster */
				break;
			}
			/* the glue code wants a pointer to the satellite */
			ptr = msg->new_sec.satellite;
			(void)push_state(ctx, UMPF_TAG_GLUE, ptr);
			goto glue_setup;
		}
		default:
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
		struct __satell_s *ptr;
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


/* printers */
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
	print_indent(ctx, indent);
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

	csnprintf(ctx, " Long=\"%.8g\"", qty->long_);
	csnprintf(ctx, " Short=\"%.8g\"", qty->short_);
	csnprintf(ctx, " Stat=\"%d\"", qty->stat);

	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
__print_sub(__ctx_t ctx, struct pfix_sub_s *sub)
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
	static const char ftr[] = "/>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	__print_sub(ctx, sub);

	/* just finish the tag */
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_pty(__ctx_t ctx, struct pfix_pty_s *pty, size_t indent)
{
	static const char hdr[] = "<Pty";
	static const char ftr[] = "</Pty>\n";
	static const char altftr[] = "/>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	__print_sub(ctx, &pty->prim);

	/* finish the tag preliminarily */
	if (pty->nsub > 0) {
		snputs(ctx, ">\n", 2);
	}

	for (size_t i = 0; i < pty->nsub; i++) {
		pfix_print_sub(ctx, pty->sub + i, indent + 2);
	}

	if (pty->nsub > 0) {
		print_indent(ctx, indent);
		snputs(ctx, ftr, countof_m1(ftr));
	} else {
		/* just finish the tag */
		snputs(ctx, altftr, countof_m1(altftr));
	}
	return;
}

static void
pfix_print_req_for_poss(
	__ctx_t ctx, struct pfix_req_for_poss_s *rfp, size_t indent)
{
	static const char hdr[] = "<ReqForPoss";
	static const char ftr[] = "</ReqForPoss>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (rfp->req_typ) {
		csnprintf(ctx, " ReqTyp=\"%d\"", rfp->req_typ);
	}

	if (rfp->biz_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, rfp->biz_dt);
		sputc(ctx, '"');
	}

	if (rfp->txn_tm > 0) {
		sputs(ctx, " TxnTm=\"");
		print_zulu(ctx, rfp->txn_tm);
		sputc(ctx, '"');
	}

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

	if (rfpa->rfp.req_typ) {
		csnprintf(ctx, " ReqTyp=\"%d\"", rfpa->rfp.req_typ);
	}
	csnprintf(ctx, " TotRpts=\"%d\"", rfpa->tot_rpts);

	if (rfpa->rfp.biz_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, rfpa->rfp.biz_dt);
		sputc(ctx, '"');
	}

	if (rfpa->rfp.txn_tm > 0) {
		sputs(ctx, " TxnTm=\"");
		print_zulu(ctx, rfpa->rfp.txn_tm);
		sputc(ctx, '"');
	}

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
	static const char hdr[] = "<ReqForPoss";
	static const char ftr[] = "</ReqForPoss>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	if (pr->rslt) {
		csnprintf(ctx, " Rslt=\"%d\"", pr->rslt);
	}

	if (pr->req_typ) {
		csnprintf(ctx, " ReqTyp=\"%d\"", pr->req_typ);
	}

	for (size_t i = 0; i < pr->npty; i++) {
		pfix_print_pty(ctx, pr->pty + i, indent + 2);
	}

	for (size_t i = 0; i < pr->ninstrmt; i++) {
		pfix_print_instrmt(ctx, pr->instrmt + i, indent + 2);
	}

	for (size_t i = 0; i < pr->nqty; i++) {
		pfix_print_qty(ctx, pr->qty + i, indent + 2);
	}

	/* finalise the tag */
	sputs(ctx, ">\n");

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}

static void
pfix_print_fixml(__ctx_t ctx, struct pfix_fixml_s *f, size_t indent)
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

	if (f->nbatch > 1) {
		print_indent(ctx, indent + 2);
		snputs(ctx, batch_hdr, countof_m1(batch_hdr));
		indent += 2;
	}
		
	for (size_t i = 0; i < f->nbatch; i++) {
		switch (f->batch[i].tag) {
		case UMPF_TAG_REQ_FOR_POSS:
			pfix_print_req_for_poss(
				ctx, &f->batch[i].req_for_poss, indent + 2);
			break;
		case UMPF_TAG_REQ_FOR_POSS_ACK:
			pfix_print_req_for_poss_ack(
				ctx, &f->batch[i].req_for_poss_ack, indent + 2);
			break;
		case UMPF_TAG_POS_RPT:
			pfix_print_pos_rpt(
				ctx, &f->batch[i].pos_rpt, indent + 2);
			break;
		default:
			break;
		}
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
	if (ctx->msg != NULL && ctx->state == NULL) {
		/* we're ready */
		PFIXML_DEBUG("seems ready\n");
		return xmlParseChunk(ctx->pp, ctx->sbuf, 0, 1);
	}
	PFIXML_DEBUG("%p %u\n", ctx->msg, get_state_otype(ctx));
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


/* external stuff and helpers */
static void
init(__ctx_t ctx)
{
#if 0
/* total wipeout would be daft if this is meant to be a singleton */
	/* total wipeout */
	memset(ctx, 0, sizeof(*ctx));
#else
	/* wipe some slots */
	ctx->msg = NULL;
#endif	/* 0 */

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
	if (ctx->msg) {
		/* reset the document, leak if not free()'d */
		ctx->msg = NULL;
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

static umpf_msg_t
__umpf_parse_file(__ctx_t ctx, const char *file)
{
	umpf_msg_t res;

	init(ctx);
	PFIXML_DEBUG("parsing %s\n", file);
	if (LIKELY(parse_file(ctx, file) == 0)) {
		PFIXML_DEBUG("done\n");
		res = ctx->msg;
	} else {
		PFIXML_DEBUG("failed\n");
		res = NULL;
	}
	deinit(ctx);
	return res;
}

umpf_msg_t
umpf_parse_file(const char *file)
{
	static struct __ctx_s ctx[1] = {{0}};
	return __umpf_parse_file(ctx, file);
}

umpf_msg_t
umpf_parse_file_r(const char *file)
{
	__ctx_t ctx = calloc(1, sizeof(*ctx));
	umpf_msg_t res = __umpf_parse_file(ctx, file);
	free_ctx(ctx);
	return res;
}

/* blob parsing */
static bool
ctx_deinitted_p(__ctx_t ctx)
{
	return ctx->pp == NULL;
}

static umpf_msg_t
check_ret(__ctx_t ctx, int ret)
{
	umpf_msg_t res;

	if (ret == 0) {
		ret = final_blob_p(ctx);
	} else {
		ret = BLOB_ERROR;
	}

	switch (ret) {
	case BLOB_READY:
		PFIXML_DEBUG("done\n");
		res = ctx->msg;
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

static umpf_msg_t
__umpf_parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	init(ctx);
	PFIXML_DEBUG("parsing blob of size %zu\n", bsz);
	return check_ret(ctx, parse_blob(ctx, buf, bsz));
}

static umpf_msg_t
__umpf_parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	PFIXML_DEBUG("parsing more blob of size %zu\n", bsz);
	return check_ret(ctx, parse_more_blob(ctx, buf, bsz));
}

umpf_msg_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	static struct __ctx_s __ctx[1] = {{0}};
	umpf_msg_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = __ctx;
		res = __umpf_parse_blob(*ctx, buf, bsz);
	} else {
		res = __umpf_parse_more_blob(*ctx, buf, bsz);
	}

	if (ctx_deinitted_p(*ctx)) {
		*ctx = NULL;
	}
	return res;
}

umpf_msg_t
umpf_parse_blob_r(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	umpf_msg_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = calloc(1, sizeof(struct __ctx_s));
		res = __umpf_parse_blob(*ctx, buf, bsz);
	} else {
		res = __umpf_parse_more_blob(*ctx, buf, bsz);
	}

	if (ctx_deinitted_p(*ctx)) {
		free_ctx(*ctx);
		*ctx = NULL;
	}
	return res;
}

void
umpf_free_msg(umpf_msg_t msg)
{
	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF:
		/* satellite only occurs in new pf */
		if (msg->new_pf.satellite->data) {
			xfree(msg->new_pf.satellite->data);
		}
		goto common;

	case UMPF_MSG_NEW_SEC:
	case UMPF_MSG_GET_SEC:
	case UMPF_MSG_SET_SEC:
		/* satellite and portfolio mnemo must be freed */
		if (msg->new_sec.satellite->data) {
			xfree(msg->new_sec.satellite->data);
		}
		if (msg->new_sec.pf_mnemo) {
			xfree(msg->new_sec.pf_mnemo);
		}
		goto common;

	case UMPF_MSG_GET_PF:
	case UMPF_MSG_SET_PF:
#if 0
		/* the ins_qty's must be freed too */
		/* wrong, the ins_qty's are contiguous and stem
		 * from realloc'ing */
		for (size_t j = 0; j < msg->pf.nposs; j++) {
			struct __ins_qty_s *iq = msg->pf.poss + j;
			if (iq->instr) {
				xfree(iq->instr);
			}
		}
#endif	/* 0 */
	common:
		/* common to all messages */
		if (msg->pf.name) {
			xfree(msg->pf.name);
		}
	default:
		break;
	}
	xfree(msg);
	return;
}

umpf_msg_t
umpf_msg_add_pos(umpf_msg_t msg, size_t npos)
{
	size_t cur_nposs = msg->pf.nposs;
	size_t new_nposs = cur_nposs + npos;

	msg = realloc(msg, sizeof(*msg) + new_nposs * sizeof(*msg->pf.poss));
	msg->pf.nposs = new_nposs;
	/* rinse */
	memset(msg->pf.poss + cur_nposs, 0, npos * sizeof(*msg->pf.poss));
	return msg;
}

size_t
umpf_seria_fix(char **tgt, size_t tsz, void *fix)
{
	static struct __ctx_s __ctx[1] = {{0}};

	if (UNLIKELY(tgt == NULL)) {
		return 0U;
	}

	memset(__ctx, 0, sizeof(*__ctx));
	if (*tgt) {
		__ctx->sbuf = *tgt;
		__ctx->sbsz = tsz;
	} else {
		__ctx->sbuf = calloc(1, __ctx->sbsz = INITIAL_STUFF_BUF_SIZE);
	}
	__ctx->sbix = 0;
	__ctx->fix = fix;

	/* the actual printing */
	pfix_print_fixml(__ctx, fix, 0);

	*tgt = __ctx->sbuf;
	return __ctx->sbix;
}

/* proto-xml.c ends here */
