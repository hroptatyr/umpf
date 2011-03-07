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
#include "umpf.h"
#include "umpf-private.h"

#define PFIXML_PRE	"mod/umpf/fixml"

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
print_instrmt(struct __ins_qty_s *i, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Instrmt", out);

	if (i->instr) {
		fputs(" Sym=\"", out);
		fputs_encq(i->instr, out);
		fputc('"', out);
	}

#if 0
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
#endif

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

static void
print_qty(struct __ins_qty_s *q, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Qty", out);

#if 0
	if (q->typ) {
		fputs(" Typ=\"", out);
		fputs_encq(q->typ, out);
		fputc('"', out);
	}
#endif

	fprintf(out, " Long=\"%.6f\"", q->qty->_long);
	fprintf(out, " Short=\"%.6f\"", q->qty->_shrt);
#if 0
	fprintf(out, " Stat=\"%u\"", q->stat);
#endif

#if 0
	if (q->qty_dt > 0) {
		fputs(" QtyDt=\"", out);
		print_date(out, q->qty_dt);
		fputc('"', out);
	}
#endif

	/* finalise the tag */
	fputs("/>\n", out);
	return;
}

#if 0
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
#endif

static void
print_pty(const char *id, unsigned int role, char src, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<Pty", out);

	if (id) {
		fputs(" ID=\"", out);
		fputs_encq(id, out);
		fputc('"', out);
	}

	fprintf(out, " R=\"%u\"", role);

	if (src) {
		fputs(" Src=\"", out);
		fputc(src, out);
		fputc('"', out);
	}

	/* finalise the tag */
	fputs(">\n", out);

#if 0
/* not supported */
	for (size_t j = 0; j < p->nsub; j++) {
		struct __sub_s *sub = p->sub + j;
		print_sub(sub, out, indent + 2);
	}
#endif	/* 0 */

	print_indent(out, indent);
	fputs("</Pty>\n", out);
	return;
}

static void
print_rg_dtl(const char *name, const char *satellite, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<RgDtl>\n", out);

	/* do not use the pty printer as we need to go child-wards */
	print_indent(out, indent + 2);
	fputs("<Pty", out);

	if (name) {
		fputs(" ID=\"", out);
		fputs_encq(name, out);
		fputc('"', out);
	}

	if (satellite) {
		/* finalise the tag */
		fputs(">\n", out);

		fputs_enc(satellite, out);

		print_indent(out, indent + 2);
		fputs("</Pty>\n", out);
	} else {
		/* finalise the tag */
		fputs("/>\n", out);
	}

	print_indent(out, indent);
	fputs("</RgDtl>\n", out);
	return;
}

static void
print_req_for_poss(umpf_msg_t msg, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<ReqForPoss", out);

#if 0
	if (r->req_id) {
		fputs(" ReqId=\"", out);
		fputs_encq(r->req_id, out);
		fputc('"', out);
	}
#endif

	if (msg->pf.clr_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, msg->pf.clr_dt);
		fputc('"', out);
	}

	fputs(" ReqTyp=\"0\"", out);

#if 0
	if (r->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(r->set_ses_id, out);
		fputc('"', out);
	}
#endif

	if (msg->pf.stamp > 0) {
		fputs(" TxnTm=\"", out);
		print_zulu(out, msg->pf.stamp);
		fputc('"', out);
	}

	/* finalise the tag */
	fputs(">\n", out);

	print_pty(msg->pf.name, 0, '\0', out, indent + 2);

	print_indent(out, indent);
	fputs("</ReqForPoss>\n", out);
	return;
}

static void
print_req_for_poss_ack(umpf_msg_t msg, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<ReqForPossAck", out);

#if 0
	if (r->rpt_id) {
		fputs(" RptId=\"", out);
		fputs_encq(r->rpt_id, out);
		fputc('"', out);
	}
#endif

	if (msg->pf.clr_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, msg->pf.clr_dt);
		fputc('"', out);
	}

	fputs(" ReqTyp=\"0\"", out);
	fprintf(out, " TotRpts=\"%zu\"", msg->pf.nposs);
	fputs(" Rslt=\"0\" Stat=\"0\"", out);

#if 0
	if (r->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(r->set_ses_id, out);
		fputc('"', out);
	}
#endif

	if (msg->pf.stamp > 0) {
		fputs(" TxnTm=\"", out);
		print_zulu(out, msg->pf.stamp);
		fputc('"', out);
	}
	/* finalise the tag */
	fputs(">\n", out);

	print_pty(msg->pf.name, 0, '\0', out, indent + 2);

	print_indent(out, indent);
	fputs("</ReqForPossAck>\n", out);
	return;
}

static void
print_rgst_instrctns(umpf_msg_t msg, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<RgstInstrctns TrsnTyp=\"0\">\n", out);

	print_rg_dtl(msg->new_pf.name, msg->new_pf.satellite, out, indent + 2);

	print_indent(out, indent);
	fputs("</RgstInstrctns>\n", out);
	return;
}

static void
print_rgst_instrctns_rsp(umpf_msg_t msg, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<RgstInstrctnsRsp TrsnTyp=\"0\"", out);

	if (msg->new_pf.name) {
		fputs(" RegStat=\"A\" ID=\"", out);
		fputs_encq(msg->new_pf.name, out);
		fputc('"', out);
	} else {
		fputs(" RegStat=\"R\"", out);
	}

	fputs("/>\n", out);
	return;
}

static void
print_pos_rpt(umpf_msg_t msg, size_t idx, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<PosRpt", out);

#if 0
	if (pr->rpt_id) {
		fputs(" RptId=\"", out);
		fputs_encq(pr->rpt_id, out);
		fputc('"', out);
	}
	/* TotRpts makes no sense innit? */
#endif

	fputs(" Rslt=\"0\" ReqTyp=\"0\"", out);

	if (msg->pf.clr_dt > 0) {
		fputs(" BizDt=\"", out);
		print_date(out, msg->pf.clr_dt);
		fputc('"', out);
	}

#if 0
	if (pr->set_ses_id) {
		fputs(" SetSesID=\"", out);
		fputs_encq(pr->set_ses_id, out);
		fputc('"', out);
	}
#endif
	/* finalise the tag */
	fputs(">\n", out);

	print_pty(msg->pf.name, 0, '\0', out, indent + 2);

	print_instrmt(msg->pf.poss + idx, out, indent + 2);
	print_qty(msg->pf.poss + idx, out, indent + 2);

#if 0
	for (size_t j = 0; j < pr->namt; j++) {
		struct __amt_s *amt = pr->amt + j;
		print_amt(amt, out, indent + 2);
	}
#endif

	print_indent(out, indent);
	fputs("<PosRpt>\n", out);
	return;
}

static void
print_msg(umpf_msg_t msg, FILE *out, size_t indent)
{
	print_indent(out, indent);
	fputs("<FIXML xmlns=\"", out);
	fputs(fixml50_ns_uri, out);
	fputs("\" v=\"5.0\">\n", out);

	switch (msg->hdr.mt) {
	case UMPF_MSG_NEW_PF * 2:
		print_rgst_instrctns(msg, out, indent + 2);
		break;
	case UMPF_MSG_NEW_PF * 2 + 1:
		print_rgst_instrctns_rsp(msg, out, indent + 2);
		break;

	case UMPF_MSG_SET_PF * 2 + 1:
		/* answer to SET_PF is a GET_PF */
	case UMPF_MSG_GET_PF * 2:
		print_req_for_poss(msg, out, indent + 2);
		break;

	case UMPF_MSG_GET_PF * 2 + 1:
		/* answer to GET_PF is a SET_PF */
	case UMPF_MSG_SET_PF * 2:
		/* more than one child, so Batch it */
		print_indent(out, indent + 2);
		fputs("<Batch>\n", out);

		print_req_for_poss_ack(msg, out, indent + 4);
		for (size_t i = 0; i < msg->pf.nposs; i++) {
			print_pos_rpt(msg, i, out, indent + 4);
		}

		print_indent(out, indent + 2);
		fputs("</Batch>\n", out);
		break;
	default:
		break;
	}

	print_indent(out, indent);
	fputs("</FIXML>\n", out);
	return;
}


/* external stuff and helpers */
void
umpf_print_msg(umpf_msg_t msg, FILE *out)
{
	fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n", out);
	print_msg(msg, out, 0);
	return;
}

/* print-fixml.c ends here */
