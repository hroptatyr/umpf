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
#if defined HAVE_LIBXML2 || 1
# include <libxml/parser.h>
#endif	/* HAVE_LIBXML2 */
#include "nifty.h"
#include "pfd.h"

#define PFIXML_PRE	"mod/pfd/fixml"

#if defined __INTEL_COMPILER
# pragma warning (disable:424)
#endif	/* __INTEL_COMPILER */


static void
print_indent(FILE *out, size_t indent)
{
	for (size_t i = 0; i < indent; i++) {
		fputc(' ', out);
	}
	return;
}

static void
print_zulu(FILE *out, time_t stamp)
{
	struct tm tm[1] = {{0}};
	char buf[32];
	gmtime_r(&stamp, tm);
	strftime(buf, sizeof(buf), "%FT%T%z", tm);
	fputs(buf, out);
	return;
}

static void
print_date(FILE *out, time_t stamp)
{
	struct tm tm[1] = {{0}};
	char buf[32];
	gmtime_r(&stamp, tm);
	strftime(buf, sizeof(buf), "%F", tm);
	fputs(buf, out);
	return;
}

static void __attribute__((unused))
fputs_enc(const char *s, FILE *out)
{
/* like fputs() but encode special chars */
	static const char stpset[] = "<>&";

	for (size_t idx; idx = strcspn(s, stpset); s += idx + sizeof(*s)) {
		/* write what we've got */
		fwrite(s, sizeof(*s), idx, out);
		/* inspect the character */
		switch (s[idx]) {
		default:
		case '\0':
			return;
		case '<':
			fputs("&lt;", out);
			break;
		case '>':
			fputs("&gt;", out);
			break;
		case '&':
			fputs("&amp;", out);
			break;
		}
	}
	return;
}

static void
fputs_encq(const char *s, FILE *out)
{
/* like fputs() but encode special chars */
	static const char stpset[] = "<>&'\"";

	for (size_t idx; idx = strcspn(s, stpset); s += idx + sizeof(*s)) {
		/* write what we've got */
		fwrite(s, sizeof(*s), idx, out);
		/* inspect the character */
		switch (s[idx]) {
		default:
		case '\0':
			return;
		case '<':
			fputs("&lt;", out);
			break;
		case '>':
			fputs("&gt;", out);
			break;
		case '&':
			fputs("&amp;", out);
			break;
		case '\'':
			fputs("&apos;", out);
			break;
		case '"':
			fputs("&quot;", out);
			break;
		}
	}
	return;
}


/* printer */
static void
print_instrmt(struct __instrmt_s *i, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Instrmt", out);

	if (i->sym) {
		fputs(" Sym=\"", out);
		fputs_encq(i->sym, out);
		fputc('"', out);
	}

	if (i->id) {
		fputs(" ID=\"", out);
		fputs_encq(i->id, out);
		fputc('"', out);
	}

	if (i->src) {
		fputs(" Src=\"", out);
		fputs_encq(i->src, out);
		fputc('"', out);
	}

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

static void
print_qty(struct __qty_s *q, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Qty", out);

	if (q->typ) {
		fputs(" Typ=\"", out);
		fputs_encq(q->typ, out);
		fputc('"', out);
	}

	fprintf(out, " Long=\"%.6f\"", q->_long);
	fprintf(out, " Short=\"%.6f\"", q->_short);
	fprintf(out, " Stat=\"%u\"", q->stat);

	if (q->qty_dt > 0) {
		fputs(" QtyDt=\"", out);
		print_date(out, q->qty_dt);
		fputc('"', out);
	}

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

static void
print_amt(struct __amt_s *a, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Amt", out);

	if (a->typ) {
		fputs(" Typ=\"", out);
		fputs_encq(a->typ, out);
		fputc('"', out);
	}

	fprintf(out, " Amt=\"%.6f\"", a->amt);
	fprintf(out, " Ccy=\"%s\"", a->ccy);

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

static void
print_sub(struct __sub_s *s, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Sub", out);

	if (s->id) {
		fputs(" ID=\"", out);
		fputs_encq(s->id, out);
		fputc('"', out);
	}

	fprintf(out, " Typ=\"%u\"", s->typ);

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

static void
print_pty(struct __pty_s *p, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Pty", out);

	if (p->id) {
		fputs(" ID=\"", out);
		fputs_encq(p->id, out);
		fputc('"', out);
	}

	if (p->src) {
		fputs(" Src=\"", out);
		fputc(p->src, out);
		fputc('"', out);
	}
	fprintf(out, " R=\"%u\"", p->role);

	/* finalise the tag */
	fputs(">\n", out);

	for (size_t j = 0; j < p->nsub; j++) {
		struct __sub_s *sub = p->sub + j;
		print_sub(sub, out, indent + 2);
	}

	print_indent(out, indent);
	fputs("</Pty>\n", out);
	return;
}

static void
print_req_for_poss(struct __req_for_poss_s *r, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<ReqForPoss", out);

	if (r->req_id) {
		fputs(" ReqId=\"", out);
		fputs_encq(r->req_id, out);
		fputc('"', out);
	}

	if (r->biz_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, r->biz_dt);
		fputc('"', out);
	}

	fprintf(out, " ReqTyp=\"%u\"", r->req_typ);

	if (r->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(r->set_ses_id, out);
		fputc('"', out);
	}

	if (r->txn_tm > 0) {
		fputs(" TxnTm=\"", out);
		print_zulu(out, r->txn_tm);
		fputc('"', out);
	}

	/* finalise the tag */
	fputs(">\n", out);

	for (size_t j = 0; j < r->npty; j++) {
		struct __pty_s *pty = r->pty + j;
		print_pty(pty, out, indent + 2);
	}

	print_indent(out, indent);
	fputs("</ReqForPoss>\n", out);
	return;
}

static void
print_req_for_poss_ack(struct __req_for_poss_ack_s *r, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<ReqForPossAck", out);

	if (r->rpt_id) {
		fputs(" RptId=\"", out);
		fputs_encq(r->rpt_id, out);
		fputc('"', out);
	}

	if (r->biz_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, r->biz_dt);
		fputc('"', out);
	}

	fprintf(out, " ReqTyp=\"%u\"", r->req_typ);
	fprintf(out, " TotRpts=\"%zu\"", r->tot_rpts);
	fprintf(out, " Rslt=\"%u\"", r->rslt);
	fprintf(out, " Stat=\"%u\"", r->stat);

	if (r->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(r->set_ses_id, out);
		fputc('"', out);
	}

	if (r->txn_tm > 0) {
		fputs(" TxnTm=\"", out);
		print_zulu(out, r->txn_tm);
		fputc('"', out);
	}
	/* finalise the tag */
	fputs(">\n", out);

	for (size_t j = 0; j < r->npty; j++) {
		struct __pty_s *pty = r->pty + j;
		print_pty(pty, out, indent + 2);
	}

	print_indent(out, indent);
	fputs("</ReqForPossAck>\n", out);
	return;
}

static void
print_pos_rpt(struct __pos_rpt_s *pr, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<PosRpt", out);

	if (pr->rpt_id) {
		fputs(" RptId=\"", out);
		fputs_encq(pr->rpt_id, out);
		fputc('"', out);
	}
	/* TotRpts makes no sense innit? */

	fprintf(out, " Rslt=\"%u\"", pr->rslt);

	fprintf(out, " ReqTyp=\"%u\"", pr->req_typ);

	if (pr->biz_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, pr->biz_dt);
		fputc('"', out);
	}

	if (pr->rpt_id) {
		fputs(" RptId=\"", out);
		fputs_encq(pr->rpt_id, out);
		fputc('"', out);
	}

	if (pr->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(pr->set_ses_id, out);
		fputc('"', out);
	}
	/* finalise the tag */
	fputs(">\n", out);

	print_instrmt(pr->instrmt, out, indent + 2);

	for (size_t j = 0; j < pr->npty; j++) {
		struct __pty_s *pty = pr->pty + j;
		print_pty(pty, out, indent + 2);
	}

	for (size_t j = 0; j < pr->nqty; j++) {
		struct __qty_s *qty = pr->qty + j;
		print_qty(qty, out, indent + 2);
	}

	for (size_t j = 0; j < pr->namt; j++) {
		struct __amt_s *amt = pr->amt + j;
		print_amt(amt, out, indent + 2);
	}

	print_indent(out, indent);
	fputs("<PosRpt>\n", out);
	return;
}

static void
print_batch(struct __batch_s *b, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Batch>\n", out);

	for (size_t j = 0; j < b->nmsg; j++) {
		struct __g_msg_s *m = b->msg + j;
		switch (m->tid) {
		case PFD_TAG_REQ_FOR_POSS:
			print_req_for_poss(
				m->msg.req_for_poss, out, indent + 2);
			break;
		case PFD_TAG_REQ_FOR_POSS_ACK:
			print_req_for_poss_ack(
				m->msg.req_for_poss_ack, out, indent + 2);
			break;
		case PFD_TAG_POS_RPT:
			print_pos_rpt(m->msg.pos_rpt, out, indent + 2);
			break;
		default:
			break;
		}
	}

	print_indent(out, indent);
	fputs("</Batch>\n", out);
	return;
}

static void
print_doc(pfd_doc_t doc, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<FIXML xmlns=\"", out);
	fputs(fixml50_ns_uri, out);
	fputs("\" v=\"5.0\">\n", out);

	switch (doc->top) {
	case PFD_TAG_BATCH:
		print_batch(doc->batch, out, indent + 2);
		break;
	case PFD_TAG_REQ_FOR_POSS:
		print_req_for_poss(doc->msg.req_for_poss, out, indent + 2);
		break;
	case PFD_TAG_REQ_FOR_POSS_ACK:
		print_req_for_poss_ack(
			doc->msg.req_for_poss_ack, out, indent + 2);
		break;
	case PFD_TAG_POS_RPT:
		print_pos_rpt(doc->msg.pos_rpt, out, indent + 2);
		break;
	}

	print_indent(out, indent);
	fputs("</FIXML>\n", out);
	return;
}


/* external stuff and helpers */
void
pfd_print_doc(pfd_doc_t doc, FILE *out)
{
	fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", out);
	print_doc(doc, out, 0);
	return;
}

/* print-xml.c ends here */
