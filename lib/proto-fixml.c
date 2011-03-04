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
#include <time.h>
#include <ctype.h>
#if defined HAVE_LIBXML2 || 1
# include <libxml/parser.h>
#endif	/* HAVE_LIBXML2 */
#include "nifty.h"
#include "umpf.h"
#include "umpf-private.h"

#define PFIXML_PRE	"mod/umpf/fixml"

/* gperf goodness */
#include "proto-fixml-tag.c"
#include "proto-fixml-attr.c"

#if defined __INTEL_COMPILER
# pragma warning (disable:424)
#endif	/* __INTEL_COMPILER */

typedef struct __ctx_s *__ctx_t;
typedef xmlSAXHandler sax_hdl_s;
typedef sax_hdl_s *sax_hdl_t;
typedef struct umpf_ctxcb_s *umpf_ctxcb_t;

struct umpf_ns_s {
	char *pref;
	char *href;
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
	void *doc;
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


static void
init_ctxcb(__ctx_t ctx)
{
	memset(ctx->ctxcb_pool, 0, sizeof(ctx->ctxcb_pool));
	for (int i = 0; i < countof(ctx->ctxcb_pool) - 1; i++) {
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
	} else if (strcmp(href, fixml50_ns_uri) == 0 ||
		   strcmp(href, fixml44_ns_uri) == 0) {
		/* oh, it's our fave, copy the  */
		ctx->ns[0].pref = pref ? strdup(pref) : NULL;
		ctx->ns[0].href = strdup(href);
	} else {
		size_t i = ctx->nns++;
		ctx->ns[i].pref = pref ? strdup(pref) : NULL;
		ctx->ns[i].href = strdup(href);
	}
	return;
}

static void
umpf_init(__ctx_t ctx)
{
	/* initialise the ctxcb pool */
	init_ctxcb(ctx);
	/* alloc some space for our document */
	{
		umpf_ctxcb_t cc = pop_ctxcb(ctx);

		ctx->state = cc;
		cc->old_state = NULL;
		cc->object = NULL;
		cc->otype = UMPF_TAG_FIXML;
	}
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

/* stuff buf handling */
static void
stuff_buf_reset(__ctx_t ctx)
{
	ctx->sbix = 0;
	return;
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
	UMPF_DEBUG(PFIXML_PRE ": reg'ging name space %s\n", pref);
	umpf_reg_ns(ctx, pref, value);
	return;
}

static void
proc_FIXML_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const umpf_aid_t aid = sax_aid_from_attr(attr);

	if (UNLIKELY(rattr > attr && ctx->ns[0].href == NULL)) {
		const struct umpf_attr_s *a = __aiddify(attr, rattr - attr);

		if (a && a->aid == UMPF_ATTR_XMLNS) {
			proc_FIXML_xmlns(ctx, rattr, value);
			return;
		}
	} else if (rattr > attr && !umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_XMLNS:
		proc_FIXML_xmlns(ctx, NULL, value);
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
	const umpf_aid_t aid = sax_aid_from_attr(attr);
	umpf_msg_t msg = ctx->doc;

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
	const umpf_aid_t aid = sax_aid_from_attr(attr);
	umpf_msg_t msg = ctx->doc;

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
	const umpf_aid_t aid = sax_aid_from_attr(attr);
	umpf_msg_t msg = get_state_object(ctx);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_ID:
		msg->pf.name = strdup(value);
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
	const umpf_aid_t aid = sax_aid_from_attr(attr);

	if (!umpf_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case UMPF_ATTR_SYM: {
		struct __ins_qty_s *iq = get_state_object(ctx);
		iq->instr = strdup(value);
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
	const umpf_aid_t aid = sax_aid_from_attr(attr);
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


static void
sax_bo_elt(__ctx_t ctx, const char *name, const char **attrs)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	const umpf_tid_t tid = sax_tid_from_tag(rname);

	if (!umpf_pref_p(ctx, name, rname - name) && ctx->doc != NULL) {
		/* dont know what to do */
		UMPF_DEBUG(PFIXML_PRE ": unknown namespace %s\n", name);
		return;
	}

	/* all the stuff that needs a new sax handler */
	switch (tid) {
	case UMPF_TAG_FIXML: {
		umpf_init(ctx);
		for (int i = 0; attrs[i] != NULL; i += 2) {
			proc_FIXML_attr(ctx, attrs[i], attrs[i + 1]);
		}
		push_state(ctx, tid, ctx->doc);
		break;
	}

	case UMPF_TAG_BATCH:
		break;

	case UMPF_TAG_REQ_FOR_POSS:
		/* translate to get_pf/get_descr */
	case UMPF_TAG_REQ_FOR_POSS_ACK:
		/* translate to set_pf */
	case UMPF_TAG_RGST_INSTRCTNS: {
		/* translate to new_pf/set_descr */
		umpf_msg_t msg;

		if (UNLIKELY(ctx->doc != NULL)) {
			break;
		} else if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_FIXML)) {
			break;
		}

		/* generate a massage */
		ctx->doc = msg = calloc(sizeof(*msg), 1);
		/* sigh, subtle differences */
		switch (tid) {
		case UMPF_TAG_REQ_FOR_POSS:
			msg->hdr.mt = UMPF_MSG_GET_PF * 2;
			for (size_t j = 0; attrs[j] != NULL; j += 2) {
				proc_REQ_FOR_POSS_attr(
					ctx, attrs[j], attrs[j + 1]);
			}
			break;
		case UMPF_TAG_REQ_FOR_POSS_ACK:
			msg->hdr.mt = UMPF_MSG_SET_PF * 2;
			for (size_t j = 0; attrs[j] != NULL; j += 2) {
				proc_REQ_FOR_POSS_ACK_attr(
					ctx, attrs[j], attrs[j + 1]);
			}
			msg = realloc(
				msg, sizeof(*msg) +
				sizeof(struct __ins_qty_s) * msg->pf.nposs);
			break;
		case UMPF_TAG_RGST_INSTRCTNS:
			msg->hdr.mt = UMPF_MSG_NEW_PF * 2;
			break;
		default:
			break;
		}
		(void)push_state(ctx, tid, msg);
		break;
	}

	case UMPF_TAG_POS_RPT: {
		/* part of get/set pf */
		umpf_msg_t msg;
		struct __ins_qty_s *iq;

		if (UNLIKELY((msg = ctx->doc) == NULL)) {
			iq = NULL;
		} else if (UNLIKELY(get_state_otype(ctx) != UMPF_TAG_FIXML)) {
			iq = NULL;
		} else {
			iq = msg->pf.poss + get_state_objint(ctx);
		}
		(void)push_state(ctx, tid, iq);
		break;
	}

	case UMPF_TAG_PTY: {
		/* context sensitive node, bummer */
		umpf_msg_t msg;

		if (UNLIKELY((msg = ctx->doc) == NULL)) {
			break;
		}

		switch (get_state_otype(ctx)) {
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

	case UMPF_TAG_INSTRMT: {
		/* check that we're inside a PosRpt context */
		struct __ins_qty_s *iq =
			get_state_object_if(ctx, UMPF_TAG_POS_RPT);

		if (UNLIKELY(iq == NULL)) {
			UMPF_DEBUG(
				PFIXML_PRE
				" WARN: Instrmt outside of PosRpt\n");
			break;
		}

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_INSTRMT_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

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

	default:
		UMPF_DEBUG(PFIXML_PRE " WARN: unknown tag %s\n", name);
		break;
	}

	UMPF_DEBUG(PFIXML_PRE " STATE: %u <- %s\n", get_state_otype(ctx), name);
	return;
}


static void
sax_eo_elt(__ctx_t ctx, const char *name)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	umpf_tid_t tid = sax_tid_from_tag(rname);

	/* check if this is an umpf node */
	if (!umpf_pref_p(ctx, name, rname - name)) {
		/* dont know what to do */
		return;
	}

	/* stuff buf reset */
	stuff_buf_reset(ctx);

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
	case UMPF_TAG_PTY:
		pop_state(ctx);
		break;
	default:
		break;
	}

	if (UNLIKELY(tid == UMPF_TAG_FIXML)) {
		/* finalise the document */
		pop_state(ctx);
	}
	UMPF_DEBUG(PFIXML_PRE " STATE: %s -> %u\n", name, get_state_otype(ctx));
	return;
}

static void
stuff_buf_push(__ctx_t ctx, const char *ch, int len)
{
	if (UNLIKELY(ctx->sbix + len >= ctx->sbsz)) {
		size_t new_sz = ctx->sbix + len;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;
		/* realloc now */
		ctx->sbuf = realloc(ctx->sbuf, ctx->sbsz = new_sz);
	}
	/* now copy */
	memcpy(ctx->sbuf + ctx->sbix, ch, len);
	ctx->sbuf[ctx->sbix += len] = '\0';
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
	ctx->hdl->characters = (charactersSAXFunc)stuff_buf_push;
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
	if (ctx->doc != NULL && ctx->state == NULL) {
		/* we're ready */
		UMPF_DEBUG(PFIXML_PRE ": seems ready\n");
		return xmlParseChunk(ctx->pp, ctx->sbuf, 0, 1);
	}
	UMPF_DEBUG(PFIXML_PRE ": %p %u\n", ctx->doc, get_state_otype(ctx));
	/* request more data */
	return BLOB_M_PLZ;
}

static void
parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	xmlParseChunk(ctx->pp, buf, bsz, bsz == 0);
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
	ctx->doc = NULL;
#endif	/* 0 */

	/* initialise the stuff buffer */
	ctx->sbuf = realloc(ctx->sbuf, ctx->sbsz = INITIAL_STUFF_BUF_SIZE);
	ctx->sbix = 0;
	/* we always reserve one name space slot for FIXML */
	ctx->nns = 1;

	/* fill in the minimalistic sax handler to begin with */
	ctx->hdl->startElement = (startElementSAXFunc)sax_bo_elt;
	ctx->hdl->endElement = (endElementSAXFunc)sax_eo_elt;
	ctx->hdl->characters = (charactersSAXFunc)stuff_buf_push;
	ctx->hdl->getEntity = sax_get_ent;
	return;
}

static void
deinit(__ctx_t ctx)
{
	if (ctx->doc) {
		/* reset the document, leak if not free()'d */
		ctx->doc = NULL;
	}

	for (size_t i = 0; i < ctx->nns; i++) {
		if (ctx->ns[i].pref) {
			free(ctx->ns[i].pref);
		}
		ctx->ns[i].pref = NULL;
		if (ctx->ns[i].href) {
			free(ctx->ns[i].href);
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
		free(ctx->sbuf);
	}
	free(ctx);
	return;
}

static umpf_doc_t
__umpf_parse_file(__ctx_t ctx, const char *file)
{
	umpf_doc_t res;

	init(ctx);
	UMPF_DEBUG(PFIXML_PRE ": parsing %s\n", file);
	if (LIKELY(parse_file(ctx, file) == 0)) {
		UMPF_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->doc;
	} else {
		UMPF_DEBUG(PFIXML_PRE ": failed\n");
		res = NULL;
	}
	deinit(ctx);
	return res;
}

umpf_doc_t
umpf_parse_file(const char *file)
{
	static struct __ctx_s ctx[1] = {{0}};
	return __umpf_parse_file(ctx, file);
}

umpf_doc_t
umpf_parse_file_r(const char *file)
{
	__ctx_t ctx = calloc(sizeof(*ctx), 1);
	umpf_doc_t res = __umpf_parse_file(ctx, file);
	free_ctx(ctx);
	return res;
}

/* blob parsing */
static bool
ctx_deinitted_p(__ctx_t ctx)
{
	return ctx->pp == NULL;
}

static umpf_doc_t
check_ret(__ctx_t ctx)
{
	umpf_doc_t res;
	int ret = final_blob_p(ctx);

	switch (ret) {
	case BLOB_READY:
		UMPF_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->doc;
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
	UMPF_DEBUG(PFIXML_PRE ": parsing blob of size %zu\n", bsz);
	parse_more_blob(ctx, buf, bsz);
	return;
}

umpf_doc_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	static struct __ctx_s __ctx[1] = {{0}};
	umpf_doc_t res;

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

umpf_doc_t
umpf_parse_blob_r(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	umpf_doc_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = calloc(sizeof(struct __ctx_s), 1);
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
umpf_free_doc(umpf_doc_t doc)
{
/* do me properly */
	free(doc);
	return;
}

/* proto-xml.c ends here */
