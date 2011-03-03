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
#include "pfd.h"

#define PFIXML_PRE	"mod/pfd/fixml"

/* gperf goodness */
#include "proto-fixml-tag.c"
#include "proto-fixml-attr.c"

#if defined __INTEL_COMPILER
# pragma warning (disable:424)
#endif	/* __INTEL_COMPILER */

typedef struct __ctx_s *__ctx_t;
typedef xmlSAXHandler sax_hdl_s;
typedef sax_hdl_s *sax_hdl_t;
typedef struct pfd_ctxcb_s *pfd_ctxcb_t;

struct pfd_ns_s {
	char *pref;
	char *href;
};

/* contextual callbacks */
struct __ctxcb_s {
	/* principal types callbacks,
	 * sf is for strings (and mdStrings)
	 * dt is for dateTime (and mdDateTime) */
	void(*sf)(pfd_ctxcb_t ctx, const char *str, size_t len);
	void(*dtf)(pfd_ctxcb_t ctx, time_t date_time);
	void(*df)(pfd_ctxcb_t ctx, double decimal);
};

struct pfd_ctxcb_s {
	/* for a linked list */
	pfd_ctxcb_t next;

	struct __ctxcb_s cb[1];
	/* navigation info, stores the context */
	pfd_tid_t otype;
	void *object;
	pfd_ctxcb_t old_state;
};

struct __ctx_s {
	pfd_doc_t doc;
	struct pfd_ns_s ns[16];
	size_t nns;
	/* stuff buf */
#define INITIAL_STUFF_BUF_SIZE	(4096)
	char *sbuf;
	size_t sbsz;
	size_t sbix;
	/* the current sax handler */
	sax_hdl_s hdl[1];
	/* parser state, for contextual callbacks */
	pfd_ctxcb_t state;
	/* pool of context trackers, implies maximum parsing depth */
	struct pfd_ctxcb_s ctxcb_pool[16];
	pfd_ctxcb_t ctxcb_head;

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

static pfd_ctxcb_t
pop_ctxcb(__ctx_t ctx)
{
	pfd_ctxcb_t res = ctx->ctxcb_head;

	if (LIKELY(res != NULL)) {
		ctx->ctxcb_head = res->next;
		memset(res, 0, sizeof(*res));
	}
	return res;
}

static void
push_ctxcb(__ctx_t ctx, pfd_ctxcb_t ctxcb)
{
	ctxcb->next = ctx->ctxcb_head;
	ctx->ctxcb_head = ctxcb;
	return;
}

static void
pop_state(__ctx_t ctx)
{
/* restore the previous current state */
	pfd_ctxcb_t curr = ctx->state;

	ctx->state = curr->old_state;
	/* queue him in our pool */
	push_ctxcb(ctx, curr);
	return;
}

static pfd_ctxcb_t
push_state(__ctx_t ctx, pfd_tid_t otype, void *object)
{
	pfd_ctxcb_t res = pop_ctxcb(ctx);

	/* stuff it with the object we want to keep track of */
	res->object = object;
	res->otype = otype;
	/* fiddle with the states in our context */
	res->old_state = ctx->state;
	ctx->state = res;
	return res;
}

static pfd_tid_t
get_state_otype(__ctx_t ctx)
{
	return ctx->state ? ctx->state->otype : PFD_TAG_UNK;
}

static void*
get_state_object(__ctx_t ctx)
{
	return ctx->state->object;
}

static void*
get_state_object_if(__ctx_t ctx, pfd_tid_t otype)
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
pfd_reg_ns(__ctx_t ctx, const char *pref, const char *href)
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
pfd_init(__ctx_t ctx)
{
	/* initialise the ctxcb pool */
	init_ctxcb(ctx);
	/* alloc some space for our document */
	{
		struct __pfd_s *m = calloc(sizeof(*m), 1);
		pfd_ctxcb_t cc = pop_ctxcb(ctx);

		ctx->doc = m;
		ctx->state = cc;
		cc->old_state = NULL;
		cc->object = m;
		cc->otype = PFD_TAG_FIXML;
	}
	return;
}

static bool
pfd_pref_p(__ctx_t ctx, const char *pref, size_t pref_len)
{
	/* we sorted our namespaces so that pfd is always at index 0 */
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

ADDF(batch, struct __g_msg_s, msg);
ADDF(pty, struct __sub_s, sub);
ADDF(req_for_poss, struct __pty_s, pty);
ADDF(req_for_poss_ack, struct __pty_s, pty);
ADDF(pos_rpt, struct __pty_s, pty);
ADDF(pos_rpt, struct __qty_s, qty);
ADDF(pos_rpt, struct __amt_s, amt);


static pfd_tid_t
sax_tid_from_tag(const char *tag)
{
	size_t tlen = strlen(tag);
	const struct pfd_tag_s *t = __tiddify(tag, tlen);
	return t ? t->tid : PFD_TAG_UNK;
}

static pfd_aid_t
sax_aid_from_attr(const char *attr)
{
	size_t alen = strlen(attr);
	const struct pfd_attr_s *a = __aiddify(attr, alen);
	return a ? a->aid : PFD_ATTR_UNK;
}

static void
proc_FIXML_xmlns(__ctx_t ctx, const char *pref, const char *value)
{
	PFD_DEBUG(PFIXML_PRE ": reg'ging name space %s\n", pref);
	pfd_reg_ns(ctx, pref, value);
	return;
}

static void
proc_FIXML_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);

	if (UNLIKELY(rattr > attr && ctx->ns[0].href == NULL)) {
		const struct pfd_attr_s *a = __aiddify(attr, rattr - attr);

		if (a && a->aid == PFD_ATTR_XMLNS) {
			proc_FIXML_xmlns(ctx, rattr, value);
			return;
		}
	} else if (rattr > attr && !pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_XMLNS:
		proc_FIXML_xmlns(ctx, NULL, value);
		break;
	case PFD_ATTR_S:
	case PFD_ATTR_R:
	case PFD_ATTR_V:
		/* we're so not interested in version mumbo jumbo */
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_REQ_FOR_POSS_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __req_for_poss_s *r =
		get_state_object_if(ctx, PFD_TAG_REQ_FOR_POSS);

	if (UNLIKELY(r == NULL)) {
		return;
	} else if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_REQ_ID:
		r->req_id = strdup(value);
		break;
	case PFD_ATTR_SET_SES_ID:
		r->set_ses_id = strdup(value);
		break;
	case PFD_ATTR_REQ_TYP:
		r->req_typ = (pfd_req_typ_t)strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_BIZ_DT:
		r->biz_dt = get_zulu(value);
		break;
	case PFD_ATTR_TXN_TM:
		r->txn_tm = get_zulu(value);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_REQ_FOR_POSS_ACK_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __req_for_poss_ack_s *r =
		get_state_object_if(ctx, PFD_TAG_REQ_FOR_POSS_ACK);

	if (UNLIKELY(r == NULL)) {
		return;
	} else if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_RPT_ID:
		r->rpt_id = strdup(value);
		break;
	case PFD_ATTR_SET_SES_ID:
		r->set_ses_id = strdup(value);
		break;
	case PFD_ATTR_REQ_TYP:
		r->req_typ = (pfd_req_typ_t)strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_BIZ_DT:
		r->biz_dt = get_zulu(value);
		break;
	case PFD_ATTR_TXN_TM:
		r->txn_tm = get_zulu(value);
		break;
	case PFD_ATTR_TOT_RPTS:
		r->tot_rpts = strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_RSLT:
		r->rslt = (pfd_rslt_t)strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_STAT:
		r->stat = (pfd_stat_t)strtoul(value, NULL, 10);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_POS_RPT_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __pos_rpt_s *p = get_state_object_if(ctx, PFD_TAG_POS_RPT);

	if (UNLIKELY(p == NULL)) {
		return;
	} else if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_RPT_ID:
		p->rpt_id = strdup(value);
		break;
	case PFD_ATTR_SET_SES_ID:
		p->set_ses_id = strdup(value);
		break;
	case PFD_ATTR_REQ_TYP:
		p->req_typ = (pfd_req_typ_t)strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_TOT_RPTS:
		p->tot_rpts = strtoul(value, NULL, 10);
		break;
	case PFD_ATTR_BIZ_DT:
		p->biz_dt = get_zulu(value);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_PTY_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __pty_s *p = get_state_object(ctx);

	if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_ID:
		p->id = strdup(value);
		break;
	case PFD_ATTR_S:
		p->src = (char)(value ? value[0] : '\0');
		break;
	case PFD_ATTR_R:
		p->role = strtoul(value, NULL, 10);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_SUB_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __sub_s *s = get_state_object(ctx);

	if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_ID:
		s->id = strdup(value);
		break;
	case PFD_ATTR_TYP:
		s->typ = strtoul(value, NULL, 10);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_INSTRMT_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);

	if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_SYM: {
		struct __instrmt_s *i = get_state_object(ctx);
		i->sym = strdup(value);
		break;
	}
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_QTY_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __qty_s *q = get_state_object(ctx);

	if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_TYP:
		q->typ = strdup(value);
		break;
	case PFD_ATTR_LONG:
		q->_long = strtod(value, NULL);
		break;
	case PFD_ATTR_SHORT:
		q->_short = strtod(value, NULL);
		break;
	case PFD_ATTR_QTY_DT:
		q->qty_dt = get_zulu(value);
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}

static void
proc_AMT_attr(__ctx_t ctx, const char *attr, const char *value)
{
	const char *rattr = tag_massage(attr);
	const pfd_aid_t aid = sax_aid_from_attr(attr);
	struct __amt_s *a = get_state_object(ctx);

	if (!pfd_pref_p(ctx, attr, rattr - attr)) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", attr);
		return;
	}

	switch (aid) {
	case PFD_ATTR_TYP:
		a->typ = strdup(value);
		break;
	case PFD_ATTR_AMT:
		a->amt = strtod(value, NULL);
		break;
	case PFD_ATTR_CCY:
		strncpy(a->ccy, value, sizeof(a->ccy));
		break;
	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown attr %s\n", attr);
		break;
	}
	return;
}


static void
sax_bo_elt(__ctx_t ctx, const char *name, const char **attrs)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	const pfd_tid_t tid = sax_tid_from_tag(rname);

	if (!pfd_pref_p(ctx, name, rname - name) && ctx->doc != NULL) {
		/* dont know what to do */
		PFD_DEBUG(PFIXML_PRE ": unknown namespace %s\n", name);
		return;
	}

	/* all the stuff that needs a new sax handler */
	switch (tid) {
	case PFD_TAG_FIXML: {
		pfd_init(ctx);
		for (int i = 0; attrs[i] != NULL; i += 2) {
			proc_FIXML_attr(ctx, attrs[i], attrs[i + 1]);
		}
		push_state(ctx, tid, ctx->doc);
		break;
	}

	case PFD_TAG_POS_RPT:
	case PFD_TAG_REQ_FOR_POSS:
	case PFD_TAG_REQ_FOR_POSS_ACK: {
		/* check that we're inside a FIXML context */
		struct __batch_s *b = get_state_object_if(ctx, PFD_TAG_BATCH);

		if (LIKELY(b != NULL)) {
			struct __g_msg_s *new_msg = batch_add_msg(b);
			new_msg->tid = tid;
			(void)push_state(ctx, tid, new_msg->msg.ptr);
			break;
		}
		/* fallthrough otherwise */
	}
	case PFD_TAG_BATCH: {
		/* check that we're inside a FIXML context */
		pfd_doc_t m = get_state_object_if(ctx, PFD_TAG_FIXML);

		if (UNLIKELY(m == NULL)) {
			break;
		}

		m->top = tid;
		(void)push_state(ctx, tid, m->ptr);
		break;
	}

	case PFD_TAG_PTY: {
		void *o = get_state_object(ctx);
		pfd_tid_t oty = get_state_otype(ctx);
		struct __pty_s *p;

		switch (oty) {
		case PFD_TAG_REQ_FOR_POSS:
			p = req_for_poss_add_pty(o);
			break;
		case PFD_TAG_REQ_FOR_POSS_ACK:
			p = req_for_poss_ack_add_pty(o);
			break;
		case PFD_TAG_POS_RPT:
			p = pos_rpt_add_pty(o);
			break;
		default:
			PFD_DEBUG(PFIXML_PRE " WARN: Pty's father empty?\n");
			break;
		}
		(void)push_state(ctx, tid, p);

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_PTY_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	case PFD_TAG_SUB: {
		/* check that we're inside a Pty context */
		struct __pty_s *p = get_state_object_if(ctx, PFD_TAG_PTY);
		struct __sub_s *s;

		if (UNLIKELY(p == NULL)) {
			PFD_DEBUG(PFIXML_PRE " WARN: Sub outside of Pty\n");
			break;
		}

		s = pty_add_sub(p);
		(void)push_state(ctx, tid, s);

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_SUB_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	case PFD_TAG_INSTRMT: {
		/* check that we're inside a PosRpt context */
		struct __pos_rpt_s *pr =
			get_state_object_if(ctx, PFD_TAG_POS_RPT);
		struct __instrmt_s *i;

		if (UNLIKELY(pr == NULL)) {
			PFD_DEBUG(PFIXML_PRE
				  " WARN: Instrmt outside of PosRpt\n");
			break;
		}

		i = pr->instrmt;
		(void)push_state(ctx, tid, i);

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_INSTRMT_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	case PFD_TAG_QTY: {
		/* check that we're inside a PosRpt context */
		struct __pos_rpt_s *pr =
			get_state_object_if(ctx, PFD_TAG_POS_RPT);
		struct __qty_s *q;

		if (UNLIKELY(pr == NULL)) {
			PFD_DEBUG(PFIXML_PRE
				  " WARN: Instrmt outside of PosRpt\n");
			break;
		}

		q = pos_rpt_add_qty(pr);
		(void)push_state(ctx, tid, q);

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_QTY_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	case PFD_TAG_AMT: {
		/* check that we're inside a PosRpt context */
		struct __pos_rpt_s *pr =
			get_state_object_if(ctx, PFD_TAG_POS_RPT);
		struct __amt_s *a;

		if (UNLIKELY(pr == NULL)) {
			PFD_DEBUG(PFIXML_PRE
				  " WARN: Instrmt outside of PosRpt\n");
			break;
		}

		a = pos_rpt_add_amt(pr);
		(void)push_state(ctx, tid, a);

		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_AMT_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	}

	default:
		PFD_DEBUG(PFIXML_PRE " WARN: unknown tag %s\n", name);
		break;
	}

	switch (tid) {
	case PFD_TAG_POS_RPT:
		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_POS_RPT_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	case PFD_TAG_REQ_FOR_POSS:
		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_REQ_FOR_POSS_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	case PFD_TAG_REQ_FOR_POSS_ACK:
		for (int j = 0; attrs[j] != NULL; j += 2) {
			proc_REQ_FOR_POSS_ACK_attr(ctx, attrs[j], attrs[j + 1]);
		}
		break;
	default:
		break;
	}

	PFD_DEBUG(PFIXML_PRE " STATE: %u <- %s\n", get_state_otype(ctx), name);
	return;
}


static void
sax_eo_elt(__ctx_t ctx, const char *name)
{
	/* where the real element name starts, sans ns prefix */
	const char *rname = tag_massage(name);
	pfd_tid_t tid = sax_tid_from_tag(rname);

	/* check if this is an pfd node */
	if (!pfd_pref_p(ctx, name, rname - name)) {
		/* dont know what to do */
		return;
	}

	/* stuff buf reset */
	stuff_buf_reset(ctx);

	/* restore old handler */
	if (LIKELY(tid == get_state_otype(ctx))) {
		pop_state(ctx);
	}
	if (UNLIKELY(tid == PFD_TAG_FIXML)) {
		/* finalise the document */
		pop_state(ctx);
	}
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
		PFD_DEBUG(PFIXML_PRE ": seems ready\n");
		return xmlParseChunk(ctx->pp, ctx->sbuf, 0, 1);
	}
	PFD_DEBUG(PFIXML_PRE ": %p %u\n", ctx->doc, get_state_otype(ctx));
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

static pfd_doc_t
__pfd_parse_file(__ctx_t ctx, const char *file)
{
	pfd_doc_t res;

	init(ctx);
	PFD_DEBUG(PFIXML_PRE ": parsing %s\n", file);
	if (LIKELY(parse_file(ctx, file) == 0)) {
		PFD_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->doc;
	} else {
		PFD_DEBUG(PFIXML_PRE ": failed\n");
		res = NULL;
	}
	deinit(ctx);
	return res;
}

pfd_doc_t
pfd_parse_file(const char *file)
{
	static struct __ctx_s ctx[1] = {{0}};
	return __pfd_parse_file(ctx, file);
}

pfd_doc_t
pfd_parse_file_r(const char *file)
{
	__ctx_t ctx = calloc(sizeof(*ctx), 1);
	pfd_doc_t res = __pfd_parse_file(ctx, file);
	free_ctx(ctx);
	return res;
}

/* blob parsing */
static bool
ctx_deinitted_p(__ctx_t ctx)
{
	return ctx->pp == NULL;
}

static pfd_doc_t
check_ret(__ctx_t ctx)
{
	pfd_doc_t res;
	int ret = final_blob_p(ctx);

	switch (ret) {
	case BLOB_READY:
		PFD_DEBUG(PFIXML_PRE ": done\n");
		res = ctx->doc;
		break;
	case BLOB_M_PLZ:
		PFD_DEBUG(PFIXML_PRE ": more\n");
		return NULL;
	default:
	case BLOB_ERROR:
		/* error of some sort */
		PFD_DEBUG(PFIXML_PRE ": failed\n");
		res = NULL;
		break;
	}
	deinit(ctx);
	return res;
}

static void
__pfd_parse_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	init(ctx);
	PFD_DEBUG(PFIXML_PRE ": parsing blob of size %zu\n", bsz);
	parse_blob(ctx, buf, bsz);
	return;
}

static void
__pfd_parse_more_blob(__ctx_t ctx, const char *buf, size_t bsz)
{
	PFD_DEBUG(PFIXML_PRE ": parsing blob of size %zu\n", bsz);
	parse_more_blob(ctx, buf, bsz);
	return;
}

pfd_doc_t
pfd_parse_blob(pfd_ctx_t *ctx, const char *buf, size_t bsz)
{
	static struct __ctx_s __ctx[1] = {{0}};
	pfd_doc_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = __ctx;
		__pfd_parse_blob(*ctx, buf, bsz);
	} else {
		__pfd_parse_more_blob(*ctx, buf, bsz);
	}
	res = check_ret(*ctx);
	if (ctx_deinitted_p(*ctx)) {
		*ctx = NULL;
	}
	return res;
}

pfd_doc_t
pfd_parse_blob_r(pfd_ctx_t *ctx, const char *buf, size_t bsz)
{
	pfd_doc_t res;

	if (UNLIKELY(*ctx == NULL)) {
		*ctx = calloc(sizeof(struct __ctx_s), 1);
		__pfd_parse_blob(*ctx, buf, bsz);
	} else {
		__pfd_parse_more_blob(*ctx, buf, bsz);
	}

	res = check_ret(*ctx);
	if (ctx_deinitted_p(*ctx)) {
		free_ctx(*ctx);
		*ctx = NULL;
	}
	return res;
}

void
pfd_free_doc(pfd_doc_t doc)
{
/* do me properly */
	free(doc);
	return;
}

/* proto-xml.c ends here */
