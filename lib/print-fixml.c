/*** print-fixml.c -- writer for fixml position messages
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
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#if defined HAVE_LIBXML2
# include <libxml/parser.h>
#endif	/* HAVE_LIBXML2 */
#include "nifty.h"
#include "umpf.h"
#include "umpf-private.h"

#define PFIXML_PRE	"mod/umpf/fixml"

#if defined __INTEL_COMPILER
# pragma warning (disable:424)
#endif	/* __INTEL_COMPILER */

#define INITIAL_GBUF_SIZE	(4096UL)

typedef struct __ctx_s {
	char *gbuf;
	size_t gbsz;
	size_t idx;
} *__ctx_t;


static void __attribute__((noinline))
check_realloc(__ctx_t ctx, size_t len)
{
	if (UNLIKELY(ctx->idx + len > ctx->gbsz)) {
		size_t new_sz = ctx->idx + len + INITIAL_GBUF_SIZE;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;
		/* realloc now */
		ctx->gbuf = realloc(ctx->gbuf, ctx->gbsz = new_sz);

	} else if (UNLIKELY(ctx->idx + len > -ctx->gbsz)) {
		/* now we need a malloc */
		char *old = ctx->gbuf;
		size_t new_sz = ctx->idx + len + INITIAL_GBUF_SIZE;

		/* round to multiple of 4096 */
		new_sz = (new_sz & ~0xfff) + 4096L;

		ctx->gbuf = malloc(ctx->gbsz = new_sz);
		memcpy(ctx->gbuf, old, ctx->idx);
	}
	return;
}

static size_t
sputs(__ctx_t ctx, const char *src)
{
	size_t len = strlen(src);

	check_realloc(ctx, len);
	memcpy(ctx->gbuf + ctx->idx, src, len);
	ctx->idx += len;
	return len;
}

static void
snputs(__ctx_t ctx, const char *src, size_t len)
{
	check_realloc(ctx, len);
	memcpy(ctx->gbuf + ctx->idx, src, len);
	ctx->idx += len;
	return;
}

static void
sputc(__ctx_t ctx, const char c)
{
	ctx->gbuf[ctx->idx++] = c;
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
		left = ctx->gbsz - ctx->idx;
		va_start(ap, fmt);
		len = vsnprintf(ctx->gbuf + ctx->idx, left, fmt, ap);
		va_end(ap);
	} while (UNLIKELY(len == -1 || (size_t)len >= left));

	ctx->idx += len;
	return len;
}

static void
print_indent(__ctx_t ctx, size_t indent)
{
	check_realloc(ctx, indent);
	memset(ctx->gbuf + ctx->idx, ' ', indent);
	ctx->idx += indent;
	return;
}

static void
print_zulu(__ctx_t ctx, time_t stamp)
{
	struct tm tm[1] = {{0}};

	check_realloc(ctx, 32);
	gmtime_r(&stamp, tm);
	ctx->idx += strftime(ctx->gbuf + ctx->idx, 32, "%FT%T%z", tm);
	return;
}

static void
print_date(__ctx_t ctx, time_t stamp)
{
	struct tm tm[1] = {{0}};

	check_realloc(ctx, 32);
	gmtime_r(&stamp, tm);
	ctx->idx += strftime(ctx->gbuf + ctx->idx, 32, "%F", tm);
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


/* printer */
static void
print_instrmt(__ctx_t ctx, struct __ins_s *i, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Instrmt");

	if (i->sym) {
		sputs(ctx, " Sym=\"");
		sputs_encq(ctx, i->sym);
		sputc(ctx, '"');
	}

#if 0
	if (i->id) {
		sputs(ctx, " ID=\"");
		sputs_encq(ctx, i->id);
		sputc(ctx, '"');
	}

	if (i->src) {
		sputs(ctx, " Src=\"");
		sputs_encq(ctx, i->src);
		sputc(ctx, '"');
	}
#endif

	/* finalise the tag */
	sputs(ctx, "/>\n");
	return;
}

static void
print_qty(__ctx_t ctx, struct __ins_qty_s *q, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Qty");

#if 0
	if (q->typ) {
		sputs(ctx, " Typ=\"");
		sputs_encq(ctx, q->typ);
		sputc(ctx, '"');
	}
#endif

	csnprintf(ctx, " Long=\"%.6f\"", q->qty->_long);
	csnprintf(ctx, " Short=\"%.6f\"", q->qty->_shrt);
#if 0
	csnprintf(ctx, " Stat=\"%u\"", q->stat);
#endif

#if 0
	if (q->qty_dt > 0) {
		sputs(ctx, " QtyDt=\"");
		print_date(ctx, q->qty_dt);
		sputc(ctx, '"');
	}
#endif

	/* finalise the tag */
	sputs(ctx, "/>\n");
	return;
}

#if 0
static void
print_amt(__ctx_t ctx, struct __amt_s *a, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Amt");

	if (a->typ) {
		sputs(ctx, " Typ=\"");
		sputs_encq(ctx, a->typ);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " Amt=\"%.6f\"", a->amt);
	csnprintf(ctx, " Ccy=\"%s\"", a->ccy);

	/* finalise the tag */
	sputs(ctx, "/>\n");
	return;
}
#endif

static void
print_pty(__ctx_t ctx, const char *id, unsigned int role, char src, size_t ind)
{
	print_indent(ctx, ind);
	sputs(ctx, "<Pty");

	if (id) {
		sputs(ctx, " ID=\"");
		sputs_encq(ctx, id);
		sputc(ctx, '"');
	}

	csnprintf(ctx, " R=\"%u\"", role);

	if (src) {
		sputs(ctx, " Src=\"");
		sputc(ctx, src);
		sputc(ctx, '"');
	}

#if 0
	/* finalise the tag */
	sputs(ctx, ">\n");

/* not supported */
	for (size_t j = 0; j < p->nsub; j++) {
		struct __sub_s *sub = p->sub + j;
		print_sub(sub, out, indent + 2);
	}

	print_indent(ctx, ind);
	sputs(ctx, "</Pty>\n");

#else
	/* finalise the tag */
	sputs(ctx, "/>\n");
#endif	/* 0 */
	return;
}

static void
print_rg_dtl(
	__ctx_t ctx, const char *name,
	const struct __satell_s satell, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<RgDtl>\n");

	/* do not use the pty printer as we need to go child-wards */
	print_indent(ctx, indent + 2);
	sputs(ctx, "<Pty");

	if (name) {
		sputs(ctx, " ID=\"");
		sputs_encq(ctx, name);
		sputc(ctx, '"');
	}

	if (satell.data) {
		/* finalise the tag */
		sputs(ctx, ">\n");

		print_indent(ctx, indent + 4);
		sputs(ctx, "<aou:glue content-type=\"text/plain\">\n");

		snputs(ctx, satell.data, satell.size);
		sputc(ctx, '\n');

		print_indent(ctx, indent + 4);
		sputs(ctx, "</aou:glue>\n");

		print_indent(ctx, indent + 2);
		sputs(ctx, "</Pty>\n");
	} else {
		/* finalise the tag */
		sputs(ctx, "/>\n");
	}

	print_indent(ctx, indent);
	sputs(ctx, "</RgDtl>\n");
	return;
}

static void
print_req_for_poss(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<ReqForPoss");

#if 0
	if (r->req_id) {
		sputs(ctx, " ReqId=\"");
		sputs_encq(ctx, r->req_id);
		sputc(ctx, '"');
	}
#endif

	if (msg->pf.clr_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, msg->pf.clr_dt);
		sputc(ctx, '"');
	}

	sputs(ctx, " ReqTyp=\"0\"");

#if 0
	if (r->set_ses_id) {
		sputs(ctx, " SetSesID=\"");
		sputs_encq(ctx, r->set_ses_id);
		sputc(ctx, '"');
	}
#endif

	if (msg->pf.stamp > 0) {
		sputs(ctx, " TxnTm=\"");
		print_zulu(ctx, msg->pf.stamp);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	sputs(ctx, ">\n");

	print_pty(ctx, msg->pf.name, 0, '\0', indent + 2);

	print_indent(ctx, indent);
	sputs(ctx, "</ReqForPoss>\n");
	return;
}

static void
print_req_for_poss_ack(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<ReqForPossAck");

#if 0
	if (r->rpt_id) {
		sputs(ctx, " RptId=\"");
		sputs_encq(ctx, r->rpt_id);
		sputc(ctx, '"');
	}
#endif

	if (msg->pf.clr_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, msg->pf.clr_dt);
		sputc(ctx, '"');
	}

	sputs(ctx, " ReqTyp=\"0\"");
	csnprintf(ctx, " TotRpts=\"%zu\"", msg->pf.nposs);
	sputs(ctx, " Rslt=\"0\" Stat=\"0\"");

#if 0
	if (r->set_ses_id) {
		sputs(ctx, " SetSesID=\"");
		sputs_encq(ctx, r->set_ses_id);
		sputc(ctx, '"');
	}
#endif

	if (msg->pf.stamp > 0) {
		sputs(ctx, " TxnTm=\"");
		print_zulu(ctx, msg->pf.stamp);
		sputc(ctx, '"');
	}
	/* finalise the tag */
	sputs(ctx, ">\n");

	print_pty(ctx, msg->pf.name, 0, '\0', indent + 2);

	print_indent(ctx, indent);
	sputs(ctx, "</ReqForPossAck>\n");
	return;
}

static void
print_rgst_instrctns(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<RgstInstrctns TransTyp=\"0\">\n");

	print_rg_dtl(ctx, msg->new_pf.name, *msg->new_pf.satellite, indent + 2);

	print_indent(ctx, indent);
	sputs(ctx, "</RgstInstrctns>\n");
	return;
}

static void
print_rgst_instrctns_rsp(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<RgstInstrctnsRsp TransTyp=\"0\"");

	if (msg->new_pf.name) {
		sputs(ctx, " RegStat=\"A\" ID=\"");
		sputs_encq(ctx, msg->new_pf.name);
		sputc(ctx, '"');
	} else {
		sputs(ctx, " RegStat=\"R\"");
	}

	sputs(ctx, "/>\n");
	return;
}

static void
print_pos_rpt(__ctx_t ctx, umpf_msg_t msg, size_t idx, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<PosRpt");

#if 0
	if (pr->rpt_id) {
		sputs(ctx, " RptId=\"");
		sputs_encq(ctx, pr->rpt_id);
		sputc(ctx, '"');
	}
	/* TotRpts makes no sense innit? */
#endif

	sputs(ctx, " Rslt=\"0\" ReqTyp=\"0\"");

	if (msg->pf.clr_dt > 0) {
		sputs(ctx, " BizDt=\"");
		print_date(ctx, msg->pf.clr_dt);
		sputc(ctx, '"');
	}

#if 0
	if (pr->set_ses_id) {
		sputs(ctx, " SetSesID=\"");
		sputs_encq(ctx, pr->set_ses_id);
		sputc(ctx, '"');
	}
#endif
	/* finalise the tag */
	sputs(ctx, ">\n");

	print_pty(ctx, msg->pf.name, 0, '\0', indent + 2);

	print_instrmt(ctx, msg->pf.poss[idx].ins, indent + 2);
	print_qty(ctx, msg->pf.poss + idx, indent + 2);

#if 0
	for (size_t j = 0; j < pr->namt; j++) {
		struct __amt_s *amt = pr->amt + j;
		print_amt(ctx, amt, indent + 2);
	}
#endif

	print_indent(ctx, indent);
	sputs(ctx, "</PosRpt>\n");
	return;
}

static void
print_sec_xml(__ctx_t ctx, const struct __satell_s satell, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<SecXML>\n");

	print_indent(ctx, indent + 2);
	sputs(ctx, "<aou:glue content-type=\"text/plain\">\n");

	snputs(ctx, satell.data, satell.size);

	print_indent(ctx, indent + 2);
	sputs(ctx, "</aou:glue>\n");

	print_indent(ctx, indent);
	sputs(ctx, "</SecXML>\n");
	return;
}

static void
print_sec_def(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<SecDef");

	if (msg->new_sec.pf_mnemo) {
		sputs(ctx, " Txt=\"");
		sputs_encq(ctx, msg->new_sec.pf_mnemo);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	sputs(ctx, ">\n");

	print_instrmt(ctx, msg->new_sec.ins, indent + 2);

	if (LIKELY(msg->new_sec.satellite->data != NULL)) {
		print_sec_xml(ctx, *msg->new_sec.satellite, indent + 2);
	}

	print_indent(ctx, indent);
	sputs(ctx, "</SecDef>\n");
	return;
}

static void
print_sec_def_req(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<SecDefReq");

	if (msg->new_sec.pf_mnemo) {
		sputs(ctx, " Txt=\"");
		sputs_encq(ctx, msg->new_sec.pf_mnemo);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	sputs(ctx, ">\n");

	print_instrmt(ctx, msg->new_sec.ins, indent + 2);

	print_indent(ctx, indent);
	sputs(ctx, "</SecDefReq>\n");
	return;
}

static void
print_sec_def_upd(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<SecDefUpd");

	if (msg->new_sec.pf_mnemo) {
		sputs(ctx, " Txt=\"");
		sputs_encq(ctx, msg->new_sec.pf_mnemo);
		sputc(ctx, '"');
	}

	/* finalise the tag */
	sputs(ctx, ">\n");

	print_instrmt(ctx, msg->new_sec.ins, indent + 2);

	if (LIKELY(msg->new_sec.satellite->data != NULL)) {
		print_sec_xml(ctx, *msg->new_sec.satellite, indent + 2);
	}

	print_indent(ctx, indent);
	sputs(ctx, "</SecDefUpd>\n");
	return;
}

static void
print_alloc(__ctx_t ctx, struct __qsd_s *qsd, const char *pf, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Alloc");

	if (LIKELY(pf != NULL)) {
		sputs(ctx, " Acct=\"");
		sputs_encq(ctx, pf);
		sputc(ctx, '"');
	}

	{
		double v;

		if (UNLIKELY(qsd->pos < 0)) {
			v = -qsd->pos;
		} else {
			v = qsd->pos;
		}
		csnprintf(ctx, " Qty=\"%.6f\"", v);
	}

	sputs(ctx, "/>\n");
	return;
}

static void
print_alloc_instrctn(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Batch>\n");

	for (size_t i = 0; i < msg->pf.nposs; i++) {
		print_indent(ctx, indent + 2);
		sputs(ctx, "<AllocInstrctn ID=\"0\" TransTyp=\"0\" Typ=\"5\"");

		if (msg->pf.name) {
			sputs(ctx, " Acct=\"");
			sputs_encq(ctx, msg->pf.name);
			sputc(ctx, '"');
		}

		if (msg->pf.stamp) {
			sputs(ctx, " TxnTm=\"");
			print_zulu(ctx, msg->pf.stamp);
			sputc(ctx, '"');
		}

		if (msg->pf.clr_dt) {
			sputs(ctx, " TrdDt=\"");
			print_date(ctx, msg->pf.clr_dt);
			sputs(ctx, "\" SettlDt=\"");
			print_date(ctx, msg->pf.clr_dt);
			sputc(ctx, '"');
		} else if (msg->pf.stamp) {
			sputs(ctx, " TrdDt=\"");
			print_date(ctx, msg->pf.stamp);
			sputs(ctx, "\" SettlDt=\"");
			print_date(ctx, msg->pf.stamp);
			sputc(ctx, '"');
		}
		{
			double v;

			if (UNLIKELY(msg->pf.poss[i].qsd->pos < 0)) {
				v = -msg->pf.poss[i].qsd->pos;
			} else {
				v = msg->pf.poss[i].qsd->pos;
			}
			csnprintf(ctx, " Qty=\"%.6f\"", v);
		}
		/* sigh, we can't serialise all sides, thanks FIXML */
		sputs(ctx, " Side=\"");
		switch (msg->pf.poss[i].qsd->sd) {
		case QSIDE_OPEN_LONG:
			sputc(ctx, '1');
			break;
		case QSIDE_CLOSE_LONG:
			sputc(ctx, '2');
			break;
		case QSIDE_OPEN_SHORT:
			sputc(ctx, '5');
			break;
		case QSIDE_CLOSE_SHORT:
			/* YES! thats our own marker to close short positions */
			sputc(ctx, 'X');
			break;
		case QSIDE_UNK:
		default:
			sputc(ctx, '0');
			break;
		}
		sputc(ctx, '"');

		/* finalise the attributes */
		sputs(ctx, ">\n");

		print_instrmt(ctx, msg->pf.poss[i].ins, indent + 4);
		print_alloc(ctx, msg->pf.poss[i].qsd, msg->pf.name, indent + 4);

		/* finalise the tag */
		print_indent(ctx, indent + 2);
		sputs(ctx, "</AllocInstrctn>\n");
	}

	print_indent(ctx, indent);
	sputs(ctx, "</Batch>\n");
	return;
}

static void __attribute__((unused))
print_alloc_instrctn_ack(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	print_indent(ctx, indent);
	sputs(ctx, "<Batch>\n");

	for (size_t i = 0; i < msg->pf.nposs; i++) {
		print_indent(ctx, indent + 2);
		sputs(ctx, "<AllocInstrctnAck ID=\"0\" Stat=\"0\"/>\n");
	}

	print_indent(ctx, indent);
	sputs(ctx, "</Batch>\n");
	return;
}

static void
print_msg(__ctx_t ctx, umpf_msg_t msg, size_t indent)
{
	static const char hdr[] = "\
<FIXML xmlns=\"http://www.fixprotocol.org/FIXML-5-0\"\n\
  xmlns:aou=\"http://www.ga-group.nl/aou-0.1\"\n\
  v=\"5.0\" aou:version=\"0.1\">\n";
	static const char ftr[] = "\
</FIXML>\n";

	print_indent(ctx, indent);
	snputs(ctx, hdr, countof_m1(hdr));

	switch (msg->hdr.mt) {
	case UMPF_MSG_NEW_PF * 2:
	case UMPF_MSG_SET_DESCR * 2:
	case UMPF_MSG_GET_DESCR * 2 + 1:
		print_rgst_instrctns(ctx, msg, indent + 2);
		break;
	case UMPF_MSG_NEW_PF * 2 + 1:
	case UMPF_MSG_SET_DESCR * 2 + 1:
	case UMPF_MSG_GET_DESCR * 2:
		print_rgst_instrctns_rsp(ctx, msg, indent + 2);
		break;

	case UMPF_MSG_SET_PF * 2 + 1:
		/* answer to SET_PF is a GET_PF */
	case UMPF_MSG_GET_PF * 2:
		print_req_for_poss(ctx, msg, indent + 2);
		break;

	case UMPF_MSG_GET_PF * 2 + 1:
		/* answer to GET_PF is a SET_PF */
	case UMPF_MSG_SET_PF * 2:
		/* more than one child, so Batch it */
		print_indent(ctx, indent + 2);
		sputs(ctx, "<Batch>\n");

		print_req_for_poss_ack(ctx, msg, indent + 4);
		for (size_t i = 0; i < msg->pf.nposs; i++) {
			print_pos_rpt(ctx, msg, i, indent + 4);
		}

		print_indent(ctx, indent + 2);
		sputs(ctx, "</Batch>\n");
		break;

	case UMPF_MSG_NEW_SEC * 2:
	case UMPF_MSG_NEW_SEC * 2 + 1:
	case UMPF_MSG_GET_SEC * 2 + 1:
	case UMPF_MSG_SET_SEC * 2 + 1:
		print_sec_def(ctx, msg, indent + 2);
		break;

	case UMPF_MSG_GET_SEC * 2:
		print_sec_def_req(ctx, msg, indent + 2);
		break;

	case UMPF_MSG_SET_SEC * 2:
		print_sec_def_upd(ctx, msg, indent + 2);
		break;

	case UMPF_MSG_PATCH * 2:
		print_alloc_instrctn(ctx, msg, indent + 2);
		break;
	case UMPF_MSG_PATCH * 2 + 1:
#if 0
/* should this be configurable? */
		print_alloc_instrctn_ack(ctx, msg, indent + 2);
#else  /* !0 */
		/* more than one child, so Batch it */
		print_indent(ctx, indent + 2);
		sputs(ctx, "<Batch>\n");

		print_req_for_poss_ack(ctx, msg, indent + 4);
		for (size_t i = 0; i < msg->pf.nposs; i++) {
			print_pos_rpt(ctx, msg, i, indent + 4);
		}

		print_indent(ctx, indent + 2);
		sputs(ctx, "</Batch>\n");
#endif	/* 0 */
		break;
	default:
		UMPF_DEBUG("Can't print message %u\n", msg->hdr.mt);
		break;
	}

	print_indent(ctx, indent);
	snputs(ctx, ftr, countof_m1(ftr));
	return;
}


/* external stuff and helpers */
static const char xml_hdr[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

size_t
umpf_seria_msg(char **tgt, size_t tsz, umpf_msg_t msg)
{
	struct __ctx_s ctx[1];

	ctx->gbuf = *tgt;
	ctx->gbsz = tsz;
	ctx->idx = 0;

	snputs(ctx, xml_hdr, countof_m1(xml_hdr));
	print_msg(ctx, msg, 0);

	/* finish off with a \nul byte */
	check_realloc(ctx, 1);
	ctx->gbuf[ctx->idx] = '\0';
	*tgt = ctx->gbuf;
	return ctx->idx;
}

size_t
umpf_print_msg(int out, umpf_msg_t msg)
{
	static char gbuf[INITIAL_GBUF_SIZE];
	static struct __ctx_s ctx[1];
	ssize_t wrt;
	size_t tot = 0;

	ctx->gbuf = gbuf;
	ctx->gbsz = -INITIAL_GBUF_SIZE;
	ctx->idx = 0;

	snputs(ctx, xml_hdr, countof_m1(xml_hdr));
	print_msg(ctx, msg, 0);

	/* finish off with a \nul byte */
	check_realloc(ctx, 1);
	ctx->gbuf[ctx->idx] = '\0';

	while ((wrt = write(out, ctx->gbuf + tot, ctx->idx - tot)) > 0 &&
	       (tot += wrt) < ctx->idx);

	/* check if we need to free stuff */
	if (ctx->gbsz != -INITIAL_GBUF_SIZE) {
		free(ctx->gbuf);
	}
	return tot;
}

/* print-fixml.c ends here */
