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
#include "nifty.h"
#include "umpf.h"
#include "umpf-private.h"

#define PFIXML_PRE	"mod/umpf/fixml"

/* gperf goodness */
#include "proto-fixml-tag.c"
#include "proto-fixml-attr.c"
#include "proto-fixml-ns.c"

#if defined __INTEL_COMPILER
# pragma warning (disable:424)
#endif	/* __INTEL_COMPILER */
#if defined DEBUG_FLAG
# include <assert.h>
#else
# define assert(args...)
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
					if (*sp >= '0' && *sp <= '9') {
						a += *sp++ - '0';
					} else {
						a += *sp++ - 'a' + 10;
					}
				}
				src = sp + 1;
				
			} else {
				/* dec */
				sp += 2;
				while (*sp != ';') {
					a *= 10;
					a += *sp++ - '0';
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
#define ADDF(__sup, __str, __slot)			\
static __str*						\
__sup##_add_##__slot(struct __##__sup##_s *o)		\
{							\
	size_t idx = (o)->n##__slot++;			\
	__str *res;					\
	(o)->__slot = realloc(				\
		(o)->__slot,				\
		(o)->n##__slot * sizeof(*(o)->__slot));	\
	res = (o)->__slot + idx;			\
	/* rinse */					\
	memset(res, 0, sizeof(*res));			\
	return res;					\
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
	UMPF_DEBUG(PFIXML_PRE ": reg'ging name space %s <- %s\n", pref, value);
	umpf_reg_ns(ctx, pref, value);
	return;
}

static void
proc_FIXML_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	umpf_aid_t aid;

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
	case UMPF_ATTR_V:
		/* we're so not interested in version mumbo jumbo */
		break;
	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_REQ_FOR_POSS_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);
	umpf_msg_t msg = ctx->msg;

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_REQ_ID:
		/* ignored */
		break;
	case UMPF_ATTR_SET_SES_ID:
		/* ignored */
		break;
	case UMPF_ATTR_REQ_TYP:
		/* ignored */
		break;
	case UMPF_ATTR_BIZ_DT:
		msg->pf.clr_dt = get_zulu(value);
		break;
	case UMPF_ATTR_TXN_TM:
		msg->pf.stamp = get_zulu(value);
		break;
	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_REQ_FOR_POSS_ACK_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);
	umpf_msg_t msg = ctx->msg;

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_RPT_ID:
		/* ignored */
		break;
	case UMPF_ATTR_SET_SES_ID:
		/* ignored */
		break;
	case UMPF_ATTR_REQ_TYP:
		/* ignored */
		break;
	case UMPF_ATTR_BIZ_DT:
		msg->pf.clr_dt = get_zulu(value);
		break;
	case UMPF_ATTR_TXN_TM:
		msg->pf.stamp = get_zulu(value);
		break;
	case UMPF_ATTR_TOT_RPTS:
		msg->pf.nposs = strtoul(value, NULL, 10);
		break;
	case UMPF_ATTR_RSLT:
		/* ignored */
		break;
	case UMPF_ATTR_STAT:
		/* ignored */
		break;
	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_PTY_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);
	umpf_msg_t msg = get_state_object(ctx);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_ID:
		/* dont overwrite stuff without free()ing
		 * actually this is a bit rich, too much knowledge in here */
		if (msg->pf.name == NULL) {
			msg->pf.name = unquot(value);
		}
		break;
	case UMPF_ATTR_S:
		/* ignored */
		break;
	case UMPF_ATTR_R:
		/* ignored */
		break;
	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_INSTRMT_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_SYM: {
		struct __ins_s *ins = get_state_object(ctx);
		ins->sym = unquot(value);
		break;
	}
	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_QTY_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);
	struct __ins_qty_s *iq = get_state_object(ctx);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

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
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

proc_SEC_DEF_all_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(rattr);
	umpf_msg_t msg = ctx->msg;

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_TXT:
		msg->new_sec.pf_mnemo = unquot(value);
		break;
	case UMPF_ATTR_TXN_TM:
		/* ignored */
		break;
	default:
		break;
	}
	return;
}


static void
sax_bo_top_level_elt(__ctx_t ctx, const umpf_tid_t tid, const char **attrs)
{

	umpf_msg_t msg;

	if (UNLIKELY(ctx->msg != NULL)) {
		return;
	} else if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_FIXML)) {
		return;
	}

	/* generate a massage */
	ctx->msg = msg = calloc(1, sizeof(*msg));
	/* sigh, subtle differences */
	switch (tid) {
	case UMPF_TAG_REQ_FOR_POSS:
		umpf_set_msg_type(msg, UMPF_MSG_GET_PF);
		for (size_t j = 0; attrs[j] != NULL; j += 2) {
			proc_REQ_FOR_POSS_attr(
				ctx, attrs[j], attrs[j + 1]);
		}
		(void)push_state(ctx, tid, msg);
		break;

	case UMPF_TAG_REQ_FOR_POSS_ACK:
		umpf_set_msg_type(msg, UMPF_MSG_SET_PF);
		for (size_t j = 0; attrs[j] != NULL; j += 2) {
			proc_REQ_FOR_POSS_ACK_attr(
				ctx, attrs[j], attrs[j + 1]);
		}
		if (msg->pf.nposs > 0) {
			size_t iqsz =
				sizeof(*msg->pf.poss) * msg->pf.nposs;
			ctx->msg = msg = realloc(
				msg, sizeof(*msg) + iqsz);
			memset(msg->pf.poss, 0, iqsz);
		}
		(void)push_state(ctx, tid, msg);
		break;

	case UMPF_TAG_RGST_INSTRCTNS:
		umpf_set_msg_type(msg, UMPF_MSG_NEW_PF);
		(void)push_state(ctx, tid, msg);
		break;

	case UMPF_TAG_SEC_DEF_REQ:
		umpf_set_msg_type(msg, UMPF_MSG_GET_SEC);
		for (size_t j = 0; attrs[j] != NULL; j += 2) {
			proc_SEC_DEF_all_attr(ctx, attrs[j], attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

	case UMPF_TAG_SEC_DEF_UPD:
		umpf_set_msg_type(msg, UMPF_MSG_SET_SEC);
		for (size_t j = 0; attrs[j] != NULL; j += 2) {
			proc_SEC_DEF_all_attr(ctx, attrs[j], attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

	case UMPF_TAG_SEC_DEF:
		umpf_set_msg_type(msg, UMPF_MSG_NEW_SEC);
		for (size_t j = 0; attrs[j] != NULL; j += 2) {
			proc_SEC_DEF_all_attr(ctx, attrs[j], attrs[j + 1]);
		}
		(void)push_state(ctx, tid, ctx->msg->new_sec.ins);
		break;

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
		for (int i = 0; attrs[i] != NULL; i += 2) {
			proc_FIXML_attr(ctx, attrs[i], attrs[i + 1]);
		}
		push_state(ctx, tid, ctx->msg);
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
	case UMPF_TAG_SEC_DEF_REQ:
		/* translate to get_sec */
	case UMPF_TAG_SEC_DEF:
		/* translate to new_sec */
	case UMPF_TAG_SEC_DEF_UPD:
		/* translate to set_sec */
		sax_bo_top_level_elt(ctx, tid, attrs);
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

	case UMPF_TAG_RG_DTL:
		(void)push_state(ctx, tid, NULL);
		break;

	case UMPF_TAG_PTY: {
		/* context sensitive node, bummer */
		umpf_msg_t msg = ctx->msg;

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_RG_DTL:
			/* we use a design fluke,
			 * msg_new_pf's name slot is at the same offset
			 * as msg_pf's, so just go with it */
		case UMPF_TAG_REQ_FOR_POSS:
		case UMPF_TAG_REQ_FOR_POSS_ACK:
			(void)push_state(ctx, tid, msg);
			for (size_t j = 0; attrs[j] != NULL; j += 2) {
				proc_PTY_attr(ctx, attrs[j], attrs[j + 1]);
			}
			break;
		default:
			(void)push_state(ctx, tid, NULL);
			break;
		}
		break;
	}

	case UMPF_TAG_SUB: {
		/* not supported */
		break;
	}

	case UMPF_TAG_INSTRMT: 

		switch (get_state_otype(ctx)) {
		case UMPF_TAG_POS_RPT:
		case UMPF_TAG_SEC_DEF:
		case UMPF_TAG_SEC_DEF_REQ:
		case UMPF_TAG_SEC_DEF_UPD:
			/* we use the fact that __ins_qty_s == __ins_s
			 * in posrpt mode and in sec-def mode we rely
			 * on the right push there */
			for (int j = 0; attrs[j] != NULL; j += 2) {
				proc_INSTRMT_attr(ctx, attrs[j], attrs[j + 1]);
			}
			break;
		default:
			UMPF_DEBUG(PFIXML_PRE " Warn: "
				   "Instrmt in unknown context\n");
			break;
		}
		break;

	case UMPF_TAG_QTY: {
		/* check that we're inside a PosRpt context */
		struct __ins_qty_s *iq =
			get_state_object_if(ctx, UMPF_TAG_POS_RPT);

		if (UNLIKELY(iq == NULL)) {
			UMPF_DEBUG(
				PFIXML_PRE
				" WARN: Qty outside of PosRpt\n");
			break;
		}

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_QTY_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	case UMPF_TAG_AMT:
		/* unsupported */
		break;

	case UMPF_TAG_SEC_XML:
		/* it's just a no-op */
		break;

	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown tag %s\n", name);
		break;
	}

	UMPF_DEBUG(PFIXML_PRE " STATE: %u <- %s\n", get_state_otype(ctx), name);
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
	case UMPF_TAG_RG_DTL:
	case UMPF_TAG_PTY:
	case UMPF_TAG_SEC_DEF:
	case UMPF_TAG_SEC_DEF_REQ:
	case UMPF_TAG_SEC_DEF_UPD:
		pop_state(ctx);
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

	UMPF_DEBUG(PFIXML_PRE " STATE: %s -> %u\n", name, get_state_otype(ctx));
	return;
}


static size_t
__eat_glue(const char *src, size_t len, const char *cookie, size_t cklen)
{
	const char *end;

	if ((end = memmem(src, len + cklen, cookie, cklen)) != NULL) {
		UMPF_DEBUG(PFIXML_PRE " found end tag, eating contents\n");
		return end - src;
	} else {
		UMPF_DEBUG(PFIXML_PRE " end tag not found, eating all\n");
		return len;
	}
}

static size_t
__push_glue(__ctx_t ctx, const char *src, size_t len)
{
	const char *cookie = ctx->sbuf + sizeof(size_t);
	size_t cookie_len = ((size_t*)ctx->sbuf)[0];
	size_t consum;

	UMPF_DEBUG("looking for %s %zu\n", cookie, cookie_len);
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
	UMPF_DEBUG("pushed %zu\n", consum);
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
	UMPF_DEBUG("eating %zu bytes from libxml's buffer\n", consumed);
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
		UMPF_DEBUG(PFIXML_PRE " GLUE\n");

		if (UNLIKELY(msg == NULL)) {
			UMPF_DEBUG("msg NULL, glue is meaningless\n");
			break;
		}

		switch (umpf_get_msg_type(msg)) {
		case UMPF_MSG_NEW_PF: {
			/* ah, finally, glue indeed is supported here */
			void *ptr;

			if (UNLIKELY(msg->new_pf.satellite != NULL)) {
				/* someone else, prob us, was faster */
				break;
			}
			/* the glue code wants a pointer to the satellite */
			ptr = &msg->new_pf.satellite;
			(void)push_state(ctx, UMPF_TAG_GLUE, ptr);
			goto glue_setup;
		}
		case UMPF_MSG_NEW_SEC:
		case UMPF_MSG_SET_SEC: {
			/* ah, finally, glue indeed is supported here */
			void *ptr;

			if (UNLIKELY(msg->new_sec.satellite != NULL)) {
				/* someone else, prob us, was faster */
				break;
			}
			/* the glue code wants a pointer to the satellite */
			ptr = &msg->new_sec.satellite;
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
		char **ptr;
		size_t len = ctx->sbix - AOU_CONT_OFFS;

		UMPF_DEBUG(PFIXML_PRE " /GLUE\n");

		/* reset stuff buffer */
		ctx->sbix = 0;
		/* unsubscribe stuff buffer cb */
		ctx->pp->sax->characters = NULL;

		if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_GLUE ||
			     (ptr = get_state_object(ctx)) == NULL)) {
			break;
		}

		/* frob contents */
		*ptr = strndup(ctx->sbuf + AOU_CONT_OFFS, len);
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
		UMPF_DEBUG(PFIXML_PRE ": unknown prefix in tag %s\n", name);
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
		UMPF_DEBUG(PFIXML_PRE ": can't parse mddl yet (%s)\n", rname);
		break;

	case UMPF_NS_UNK:
	default:
		UMPF_DEBUG(PFIXML_PRE
			   ": unknown namespace %s (%s)\n", name, ns->href);
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
		UMPF_DEBUG(PFIXML_PRE ": unknown prefix in tag %s\n", name);
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
		UMPF_DEBUG(PFIXML_PRE ": can't parse mddl yet (%s)\n", rname);
		break;

	case UMPF_NS_UNK:
	default:
		UMPF_DEBUG(PFIXML_PRE
			   ": unknown namespace %s (%s)\n", name, ns->href);
		break;
	}
	return;
}

static xmlEntityPtr
sax_get_ent(void *UNUSED(user_data), const xmlChar *name)
{
	return xmlGetPredefinedEntity(name);
}

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
		UMPF_DEBUG(PFIXML_PRE ": seems ready\n");
		return xmlParseChunk(ctx->pp, ctx->sbuf, 0, 1);
	}
	UMPF_DEBUG(PFIXML_PRE ": %p %u\n", ctx->msg, get_state_otype(ctx));
	/* request more data */
	return BLOB_M_PLZ;
}

static void
parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	switch (get_state_otype(ctx)) {
		size_t cns;
	case UMPF_TAG_GLUE:
		/* better not to push parse this guy
		 * call our stuff buf pusher instead */
		UMPF_DEBUG(PFIXML_PRE ": GLUE direct\n");
		if ((cns = __push_glue(ctx, buf, bsz)) < bsz) {
			/* oh, we need to wind our buffers */
			buf += cns;
			bsz -= cns;
		} else {
			break;
		}
		UMPF_DEBUG(PFIXML_PRE ": GLUE consumed %zu\n", cns);
	default:
		xmlParseChunk(ctx->pp, buf, bsz, bsz == 0);
		break;
	}
	return;
}

static void
parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	ctx->pp = xmlCreatePushParserCtxt(ctx->hdl, ctx, buf, 0, NULL);
	parse_more_blob(ctx, buf, bsz);
	return;
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
	UMPF_DEBUG(PFIXML_PRE ": parsing %s\n", file);
	if (LIKELY(parse_file(ctx, file) == 0)) {
		UMPF_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->msg;
	} else {
		UMPF_DEBUG(PFIXML_PRE ": failed\n");
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
check_ret(__ctx_t ctx)
{
	umpf_msg_t res;
	int ret = final_blob_p(ctx);

	switch (ret) {
	case BLOB_READY:
		UMPF_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->msg;
		break;
	case BLOB_M_PLZ:
		UMPF_DEBUG(PFIXML_PRE ": more\n");
		return NULL;
	default:
	case BLOB_ERROR:
		/* error of some sort */
		UMPF_DEBUG(PFIXML_PRE ": failed\n");
		res = NULL;
		break;
	}
	deinit(ctx);
	return res;
}

static void
__umpf_parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	init(ctx);
	UMPF_DEBUG(PFIXML_PRE ": parsing blob of size %zu\n", bsz);
	parse_blob(ctx, buf, bsz);
	return;
}

static void
__umpf_parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	UMPF_DEBUG(PFIXML_PRE ": parsing more blob of size %zu\n", bsz);
	parse_more_blob(ctx, buf, bsz);
	return;
}

umpf_msg_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	static struct __ctx_s __ctx[1] = {{0}};
	umpf_msg_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = __ctx;
		__umpf_parse_blob(*ctx, buf, bsz);
	} else {
		__umpf_parse_more_blob(*ctx, buf, bsz);
	}
	res = check_ret(*ctx);
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
		__umpf_parse_blob(*ctx, buf, bsz);
	} else {
		__umpf_parse_more_blob(*ctx, buf, bsz);
	}

	res = check_ret(*ctx);
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
		if (msg->new_pf.satellite) {
			xfree(msg->new_pf.satellite);
		}
		goto common;

	case UMPF_MSG_NEW_SEC:
	case UMPF_MSG_GET_SEC:
	case UMPF_MSG_SET_SEC:
		/* satellite and portfolio mnemo must be freed */
		if (msg->new_sec.satellite) {
			xfree(msg->new_sec.satellite);
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

/* proto-xml.c ends here */
