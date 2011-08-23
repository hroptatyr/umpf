/* this is glue code to convert our messages to pfix fixml structures
 * and vice versa */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "umpf.h"
#include "proto-fixml.h"
#include "nifty.h"
#include "umpf-private.h"

#include "proto-fixml-tag.h"

#if defined DEBUG_FLAG
# include <stdio.h>
# define GLUE_DEBUG(args...)			\
	fprintf(stderr, "[umpf/fixml/glue] " args)
# define GLUE_DBGCONT(args...)			\
	fprintf(stderr, args)
#else  /* !DEBUG_FLAG */
# define GLUE_DEBUG(args...)
# define GLUE_DBGCONT(args...)
#endif	/* DEBUG_FLAG */

static char*
safe_strdup(const char *in)
{
	if (LIKELY(in != NULL)) {
		return strdup(in);
	}
	return NULL;
}

static umpf_msg_t
make_SET_DESCR_msg(umpf_msg_t m, struct pfix_rg_dtl_s *rd)
{
	umpf_set_msg_type(m, UMPF_MSG_SET_DESCR);

	if (LIKELY(rd->npty > 0)) {
		struct pfix_pty_s *p = rd->pty;

		m->new_pf.name = safe_strdup(p->prim.id);
		if (p->prim.glu.data && m->new_pf.satellite->data == NULL) {
			struct __satell_s *sat = m->new_pf.satellite;
			size_t dlen = p->prim.glu.dlen;

			sat->size = dlen;
			sat->data = malloc(dlen + 1);
			memcpy(sat->data, p->prim.glu.data, dlen);
			sat->data[dlen] = '\0';
		}
	}
	return m;
}

static umpf_msg_t
make_LST_PF_msg(umpf_msg_t m, struct pfix_rg_dtl_s *rd, size_t nrd)
{
	umpf_set_msg_reply(m, UMPF_MSG_LST_PF);

	m = realloc(m, sizeof(*m) + nrd * sizeof(*m->lst_pf.pfs));
	for (size_t i = 0; i < nrd; i++) {
		struct pfix_pty_s *p = rd[i].pty;

		if (p != NULL && p->prim.id) {
			m->lst_pf.pfs[i] = safe_strdup(p->prim.id);
			m->lst_pf.npfs++;
		}
	}
	return m;
}

static umpf_msg_t
make_LST_TAG_msg(umpf_msg_t m, struct pfix_pty_s *p)
{
	umpf_set_msg_type(m, UMPF_MSG_LST_TAG);
	if (LIKELY(p == NULL)) {
		return m;
	}
	/* otherwise */
	m->lst_tag.name = safe_strdup(p->prim.id);
	for (size_t i = 0; i < p->nsub; i++) {
		struct pfix_sub_s *s = p->sub + i;
		if (m->lst_tag.ntags % 16 == 0) {
			m = realloc(
				m,
				sizeof(*m) +
				(m->lst_tag.ntags + 16) *
				sizeof(*m->lst_tag.tags));
		}
		m->lst_tag.tags[m->lst_tag.ntags++] = strtol(s->id, NULL, 10);
	}
	return m;
}


static umpf_msg_t
make_umpf_msg(struct pfix_fixml_s *fix)
{
	umpf_msg_t msg = calloc(1, sizeof(*msg));
	pfix_tid_t tid;

	/* look at the very first batch element */
	if (fix->nbatch == 0) {
		return msg;
	}
	switch ((tid = fix->batch[0].tag)) {
	case UMPF_TAG_REQ_FOR_POSS:
	case UMPF_TAG_REQ_FOR_POSS_ACK: {
		/* translate to get_pf/set_pf */
		struct pfix_req_for_poss_ack_s *rfpa =
			&fix->batch[0].req_for_poss_ack;
		struct pfix_req_for_poss_s *rfp =
			&fix->batch[0].req_for_poss;
		size_t nposs = 0;

		if (tid == UMPF_TAG_REQ_FOR_POSS) {
			umpf_set_msg_type(msg, UMPF_MSG_GET_PF);
		} else {
			umpf_set_msg_type(msg, UMPF_MSG_SET_PF);
		}
		if (rfp->npty > 0) {
			struct pfix_pty_s *p = rfp->pty;
			msg->pf.name = safe_strdup(p->prim.id);
			if (p->nsub > 0) {
				msg->pf.tag_id = strtoul(p->sub->id, NULL, 10);
			}
		}
		msg->pf.stamp = rfp->txn_tm;
		msg->pf.clr_dt = rfp->biz_dt;
		if (tid == UMPF_TAG_REQ_FOR_POSS) {
			break;
		}

		/* otherwise fill in positions */
		for (size_t i = 1; i < fix->nbatch; i++) {
			if (fix->batch[i].tag == UMPF_TAG_POS_RPT) {
				nposs++;
			}
		}
		if (nposs == 0) {
			break;
		} else if (rfpa->tot_rpts > (ssize_t)nposs) {
			/* obviously something's missing */
			break;
		}

		msg = umpf_msg_add_pos(msg, nposs);
		for (size_t i = 1, j = 0; i < fix->nbatch; i++) {
			struct __ins_qty_s *iq;
			struct pfix_pos_rpt_s *pr;

			if (fix->batch[i].tag != UMPF_TAG_POS_RPT) {
				continue;
			}
			/* get the pos_rpt */
			pr = &fix->batch[i].pos_rpt;
			if (pr->npty == 0 ||
			    pr->ninstrmt == 0 ||
			    pr->nqty == 0) {
				continue;
			}

			/* get our position */
			iq = msg->pf.poss + j;
			iq->ins->sym = safe_strdup(pr->instrmt->sym);
			iq->qty->_long = pr->qty->long_;
			iq->qty->_shrt = pr->qty->short_;
			j++;
		}
		break;
	}
	case UMPF_TAG_RGST_INSTRCTNS: {
		/* translate to new_pf/set_descr */
		struct pfix_rgst_instrctns_s *ri =
			&fix->batch[0].rgst_instrctns;

		if (ri->nrg_dtl > 0) {
			msg = make_SET_DESCR_msg(msg, ri->rg_dtl);
		} else {
			msg = make_LST_PF_msg(msg, ri->rg_dtl, ri->nrg_dtl);
		}
		break;
	}
	case UMPF_TAG_RGST_INSTRCTNS_RSP: {
		/* translate to get_descr */
		struct pfix_rgst_instrctns_rsp_s *rir =
			&fix->batch[0].rgst_instrctns_rsp;
		if (rir->reg_stat != 'R') {
			umpf_set_msg_type(msg, UMPF_MSG_GET_DESCR);
		} else {
			umpf_set_msg_type(msg, UMPF_MSG_LST_PF);
		}
		msg->new_pf.name = safe_strdup(rir->ri.id);
		break;
	}
	case UMPF_TAG_SEC_DEF_REQ:
	case UMPF_TAG_SEC_DEF_UPD:
	case UMPF_TAG_SEC_DEF: {
		/* translate to get_sec */
		struct pfix_sec_def_s *sd = &fix->batch[0].sec_def;

		switch (tid) {
		case UMPF_TAG_SEC_DEF_REQ:
			umpf_set_msg_type(msg, UMPF_MSG_GET_SEC);
			break;
		case UMPF_TAG_SEC_DEF_UPD:
			umpf_set_msg_type(msg, UMPF_MSG_SET_SEC);
			break;
		case UMPF_TAG_SEC_DEF:
			umpf_set_msg_type(msg, UMPF_MSG_NEW_SEC);
			break;
		}

		msg->new_sec.pf_mnemo = safe_strdup(sd->txt);
		if (sd->ninstrmt > 0) {
			msg->new_sec.ins->sym = safe_strdup(sd->instrmt->sym);
		}
		if (sd->sec_xml->glu.dlen > 0) {
			struct pfix_glu_s *g = &sd->sec_xml->glu;
			msg->new_sec.satellite->size = g->dlen;
			msg->new_sec.satellite->data =
				malloc(g->dlen + 1);
			memcpy(msg->new_sec.satellite->data, g->data, g->dlen);
			msg->new_sec.satellite->data[g->dlen] = '\0';
		}
		break;
	}
	case UMPF_TAG_ALLOC_INSTRCTN:
	case UMPF_TAG_ALLOC_INSTRCTN_ACK:
		/* translate to apply */
		umpf_set_msg_type(msg, UMPF_MSG_PATCH);
		break;
	case UMPF_TAG_APPL_MSG_REQ:
	case UMPF_TAG_APPL_MSG_REQ_ACK: {
		struct pfix_appl_msg_req_s *amr =
			&fix->batch[0].appl_msg_req;
		struct pfix_appl_msg_req_ack_s *amra =
			&fix->batch[0].appl_msg_req_ack;
		struct pfix_appl_id_req_grp_s *rg;

		switch (tid) {
		case UMPF_TAG_APPL_MSG_REQ:
			rg = amr->air_grp;
			break;
		case UMPF_TAG_APPL_MSG_REQ_ACK:
			rg = amra->aira_grp;
			break;
		default:
			rg = NULL;
			break;
		}

		if (UNLIKELY(rg == NULL)) {
			break;
		}

		if (strcmp(rg->ref_appl_id, "lst_tag") == 0) {
			msg = make_LST_TAG_msg(msg, rg->pty);
		}
		if (tid == UMPF_TAG_APPL_MSG_REQ_ACK) {
			/* advance by one if it is a reply */
			msg->hdr.mt++;
		}
		break;
	}
	case UMPF_TAG_POS_RPT:
		/* should not be batch[0].tag */
		;
	default:
		GLUE_DEBUG("cannot interpret top-level %u\n", tid);
		break;
	}
	return msg;
}

static umpf_fix_t
make_umpf_fix(umpf_msg_t msg)
{
	struct pfix_fixml_s *fix = calloc(1, sizeof(*fix));

	switch (msg->hdr.mt) {
	case UMPF_MSG_NEW_PF * 2:
	case UMPF_MSG_SET_DESCR * 2:
	case UMPF_MSG_GET_DESCR * 2 + 1: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_rgst_instrctns_s *ri = &b->rgst_instrctns;
		struct pfix_rg_dtl_s *rd = rgst_instrctns_add_rg_dtl(ri);
		struct pfix_pty_s *p = rg_dtl_add_pty(rd);

		b->tag = UMPF_TAG_RGST_INSTRCTNS;
		p->prim.id = safe_strdup(msg->new_pf.name);

		if (msg->new_pf.satellite->size > 0) {
#if 0
			struct pfix_sub_s *s = pty_add_sub(p);
			s->id = safe_strdup("descr");
#else
			struct pfix_sub_s *s = &p->prim;
#endif
			struct pfix_glu_s *g = &s->glu;

			g->dlen = msg->new_pf.satellite->size;
			g->data = malloc(g->dlen + 1);
			memcpy(g->data, msg->new_pf.satellite->data, g->dlen);
			g->data[g->dlen] = '\0';
		}
		break;
	}

	case UMPF_MSG_LST_PF * 2:
	case UMPF_MSG_NEW_PF * 2 + 1:
	case UMPF_MSG_SET_DESCR * 2 + 1:
	case UMPF_MSG_GET_DESCR * 2: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_rgst_instrctns_rsp_s *rir = &b->rgst_instrctns_rsp;

		b->tag = UMPF_TAG_RGST_INSTRCTNS_RSP;
		if (msg->hdr.mt != UMPF_MSG_LST_PF * 2) {
			rir->reg_stat = 'A';
		} else {
			rir->reg_stat = 'R';
		}
		rir->ri.id = safe_strdup(msg->new_pf.name);
		rir->ri.trans_typ = 0;
		break;
	}

	case UMPF_MSG_GET_PF * 2:
	case UMPF_MSG_GET_PF * 2 + 1:
	case UMPF_MSG_SET_PF * 2:
	case UMPF_MSG_SET_PF * 2+ 1: {
		/* get-poss/set-poss */
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_req_for_poss_s *rfp = &b->req_for_poss;
		struct pfix_req_for_poss_ack_s *rfpa = &b->req_for_poss_ack;
		struct pfix_pty_s *p = req_for_poss_add_pty(rfp);

		if (msg->hdr.mt == UMPF_MSG_GET_PF * 2 ||
		    msg->hdr.mt == UMPF_MSG_SET_PF * 2 + 1) {
			b->tag = UMPF_TAG_REQ_FOR_POSS;
		} else {
			b->tag = UMPF_TAG_REQ_FOR_POSS_ACK;
		}
		rfp->txn_tm = msg->pf.stamp;
		rfp->biz_dt = msg->pf.clr_dt;
		p->prim.id = safe_strdup(msg->pf.name);
		if (msg->pf.tag_id > 0) {
			struct pfix_sub_s *s = pty_add_sub(p);
			asprintf(&s->id, "%lu", msg->pf.tag_id);
		}
		if (msg->hdr.mt == UMPF_MSG_GET_PF * 2 ||
		    msg->hdr.mt == UMPF_MSG_SET_PF * 2 + 1) {
			break;
		}
		/* otherwise we have to fill in pos_rpts */
		rfpa->tot_rpts = msg->pf.nposs;
		for (size_t i = 0; i < msg->pf.nposs; i++) {
			struct pfix_batch_s *this = fixml_add_batch(fix);
			struct pfix_pos_rpt_s *pr = &this->pos_rpt;
			struct pfix_instrmt_s *ins;
			struct pfix_qty_s *qty;

			this->tag = UMPF_TAG_POS_RPT;
			p = pos_rpt_add_pty(pr);
			p->prim.id = safe_strdup(msg->pf.name);
			ins = pos_rpt_add_instrmt(pr);
			ins->sym = safe_strdup(msg->pf.poss[i].ins->sym);
			qty = pos_rpt_add_qty(pr);
			qty->long_ = msg->pf.poss[i].qty->_long;
			qty->short_ = msg->pf.poss[i].qty->_shrt;
		}
		break;
	}
	case UMPF_MSG_NEW_SEC * 2:
	case UMPF_MSG_SET_SEC * 2:
	case UMPF_MSG_GET_SEC * 2 + 1:
		/* new-sec/set-sec */
	case UMPF_MSG_GET_SEC * 2:
	case UMPF_MSG_SET_SEC * 2 + 1:
	case UMPF_MSG_NEW_SEC * 2 + 1: {
		/* get-sec/<replies> */
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_sec_def_s *sd = &b->sec_def;
		struct pfix_instrmt_s *ins;

		sd->txt = safe_strdup(msg->new_sec.pf_mnemo);
		ins = sec_def_add_instrmt(sd);
		ins->sym = safe_strdup(msg->new_sec.ins->sym);

		if (msg->hdr.mt == UMPF_MSG_GET_SEC * 2 ||
			   msg->hdr.mt == UMPF_MSG_SET_SEC * 2 + 1 ||
			   msg->hdr.mt == UMPF_MSG_NEW_SEC * 2 + 1) {
			b->tag = UMPF_TAG_SEC_DEF_REQ;
			break;
		} else if (msg->hdr.mt == UMPF_MSG_NEW_SEC * 2) {
			b->tag = UMPF_TAG_SEC_DEF;
		} else if (msg->hdr.mt == UMPF_MSG_SET_SEC * 2 ||
			   msg->hdr.mt == UMPF_MSG_GET_SEC * 2 + 1) {
			b->tag = UMPF_TAG_SEC_DEF_UPD;
		}

		if (msg->new_sec.satellite->data != NULL) {
			struct pfix_glu_s *g = &sd->sec_xml->glu;

			g->dlen = msg->new_sec.satellite->size;
			g->data = malloc(g->dlen + 1);
			memcpy(g->data, msg->new_sec.satellite->data, g->dlen);
			g->data[g->dlen] = '\0';
		}
		break;
	}
		/* custom messages */
	case UMPF_MSG_LST_TAG * 2:
	case UMPF_MSG_LST_TAG * 2 + 1: {
		struct pfix_batch_s *b = fixml_add_batch(fix);
		struct pfix_appl_msg_req_s *amr = &b->appl_msg_req;
		struct pfix_appl_msg_req_ack_s *amra = &b->appl_msg_req_ack;
		struct pfix_appl_id_req_grp_s *rg;
		struct pfix_pty_s *p;


		switch (msg->hdr.mt) {
		case UMPF_MSG_LST_TAG * 2:
			rg = appl_msg_req_add_air_grp(amr);
			b->tag = UMPF_TAG_APPL_MSG_REQ;
			break;
		case UMPF_MSG_LST_TAG * 2 + 1:
			rg = appl_msg_req_ack_add_aira_grp(amra);
			b->tag = UMPF_TAG_APPL_MSG_REQ_ACK;
			break;
		}
		/* add appl id */
		rg->ref_appl_id = safe_strdup("lst_tag");
		/* add pf name */
		p = appl_id_req_grp_add_pty(rg);
		p->prim.id = safe_strdup(msg->lst_tag.name);

		/* add tags */
		for (size_t i = 0; i < msg->lst_tag.ntags; i++) {
			struct pfix_sub_s *s = pty_add_sub(p);
			asprintf(&s->id, "%lu", msg->lst_tag.tags[i]);
		}
		break;
	}
	default:
		GLUE_DEBUG("cannot convert message %u\n", msg->hdr.mt);
		break;
	}
	return fix;
}


/* main exposed functions */
umpf_msg_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	umpf_fix_t rpl;
	umpf_msg_t res;

	if ((rpl = pfix_parse_blob(ctx, buf, bsz)) == NULL) {
		/* better luck next time */
		return NULL;
	}
	/* bingo otherwise */
	*ctx = NULL;
	res = make_umpf_msg(rpl);
	pfix_free_fix(rpl);
	return res;
}

umpf_msg_t
umpf_parse_blob_r(umpf_ctx_t *ctx, const char *buf, size_t bsz)
{
	umpf_fix_t rpl;
	umpf_msg_t res;

	if ((rpl = pfix_parse_blob_r(ctx, buf, bsz)) == NULL) {
		/* better luck next time */
		return NULL;
	}
	/* bingo otherwise */
	*ctx = NULL;
	res = make_umpf_msg(rpl);
	pfix_free_fix(rpl);
	return res;
}


/* printers */
size_t
umpf_seria_msg(char **tgt, size_t tsz, umpf_msg_t msg)
{
	umpf_fix_t fix = make_umpf_fix(msg);
	return pfix_seria_fix(tgt, tsz, fix);
}

#define INITIAL_GBUF_SIZE	(4096UL)

size_t
umpf_print_msg(int out, umpf_msg_t msg)
{
	static char gbuf[INITIAL_GBUF_SIZE];
	char *sbuf;
	size_t sbsz;
	size_t sbix;
	ssize_t wrt;
	size_t tot = 0;

	sbuf = gbuf;
	sbsz = -INITIAL_GBUF_SIZE;
	sbix = umpf_seria_msg(&sbuf, sbsz, msg);

	while ((wrt = write(out, sbuf + tot, sbix - tot)) > 0 &&
	       (tot += wrt) < sbix);

	/* check if we need to free stuff */
	if (sbix > INITIAL_GBUF_SIZE) {
		free(sbuf);
	}
	return tot;
}

/* umpf-msg-glue-fixml.c ends here */
