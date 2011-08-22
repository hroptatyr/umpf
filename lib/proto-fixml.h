/*** proto-fixml.h -- a subset of fixml
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

#if !defined INCLUDED_proto_fixml_h_
#define INCLUDED_proto_fixml_h_

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#if defined __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* to store 2001-12-31T12:34:56+0000 as 200112311234560000 */
typedef uint64_t idttz_t;
typedef uint32_t idate_t;
/* not happy about this but its easier for mysql */
typedef double qty_t;
/* defined as umpf_tid_t in proto-fixml-tag.c */
typedef unsigned int pfix_tid_t;

typedef void *pfix_ctx_t;
typedef void *umpf_fix_t;

struct pfix_glu_s {
	size_t dlen;
	char *data;
};

struct pfix_sub_s {
	char *id;
	char src;
	int r;

	struct pfix_glu_s glu;
};

struct pfix_pty_s {
	/* first sub is like the primary thing */
	struct pfix_sub_s prim;

	size_t nsub;
	struct pfix_sub_s *sub;
};

struct pfix_qty_s {
	char *typ;
	qty_t long_;
	qty_t short_;
	idttz_t qty_dt;
	int stat;
};

struct pfix_instrmt_s {
	char *sym;
};

/* top level elements */
struct pfix_req_for_poss_s {
	int req_typ;
	int rslt;
	int stat;
	idttz_t txn_tm;
	idate_t biz_dt;

	size_t npty;
	struct pfix_pty_s *pty;
};

struct pfix_req_for_poss_ack_s {
	struct pfix_req_for_poss_s rfp;

	int tot_rpts;
};

struct pfix_pos_rpt_s {
	int rslt;
	int req_typ;

	size_t npty;
	struct pfix_pty_s *pty;

	size_t ninstrmt;
	struct pfix_instrmt_s *instrmt;

	size_t nqty;
	struct pfix_qty_s *qty;
};

struct pfix_appl_id_req_grp_s {
	char *ref_appl_id;
	char *ref_id;

	size_t npty;
	struct pfix_pty_s *pty;
};

struct pfix_appl_msg_attr_s {
	char *appl_req_id;
	int appl_req_typ;
};

struct pfix_appl_msg_req_s {
	struct pfix_appl_msg_attr_s attr;

	size_t nair_grp;
	struct pfix_appl_id_req_grp_s *air_grp;
};

struct pfix_appl_msg_req_ack_s {
	struct pfix_appl_msg_attr_s attr;

	size_t naira_grp;
	struct pfix_appl_id_req_grp_s *aira_grp;
};

struct pfix_rg_dtl_s {
	size_t npty;
	struct pfix_pty_s *pty;
};

struct pfix_rgst_instrctns_s {
	int trans_typ;
	char *id;
	char *ref_id;

	size_t nrg_dtl;
	struct pfix_rg_dtl_s *rg_dtl;
};

struct pfix_rgst_instrctns_rsp_s {
	struct pfix_rgst_instrctns_s ri;
	char reg_stat;
};

struct pfix_sec_xml_s {
	struct pfix_glu_s glu;
};

struct pfix_sec_def_s {
	char *txt;
	idttz_t txn_tm;
	idate_t biz_dt;

	char *rpt_id;
	char *req_id;
	char *rsp_id;
	int req_typ;
	int rsp_typ;

	struct pfix_sec_xml_s sec_xml[1];

	size_t ninstrmt;
	struct pfix_instrmt_s *instrmt;
};

struct pfix_sec_def_req_s {
	struct pfix_sec_def_s sec_def;
};

struct pfix_sec_def_upd_s {
	struct pfix_sec_def_s sec_def;
};

struct pfix_batch_s {
	pfix_tid_t tag;
	union {
		struct pfix_req_for_poss_s req_for_poss;
		struct pfix_req_for_poss_ack_s req_for_poss_ack;
		struct pfix_sec_def_req_s sec_def_req;
		struct pfix_sec_def_s sec_def;
		struct pfix_sec_def_upd_s sec_def_upd;
		struct pfix_pos_rpt_s pos_rpt;
		struct pfix_rgst_instrctns_s rgst_instrctns;
		struct pfix_rgst_instrctns_rsp_s rgst_instrctns_rsp;
		struct pfix_appl_msg_req_s appl_msg_req;
		struct pfix_appl_msg_req_ack_s appl_msg_req_ack;
	};
};

struct pfix_ns_s {
	char *prefix;
	char *url;
};

struct pfix_cust_attr_s {
	struct pfix_ns_s *ns;
	char *attr;
	char *value;
};

struct pfix_fixml_s {
	size_t nns;
	struct pfix_ns_s *ns;

	/* version */
	char *v;

	/* custom attributes */
	size_t nattr;
	struct pfix_cust_attr_s *attr;

	size_t nbatch;
	struct pfix_batch_s *batch;
};


/* not-so public functions */
/**
 * Free resources associated with FIX. */
extern void pfix_free_fix(umpf_fix_t fix);

/**
 * much like umpf_parse_file() */
extern umpf_fix_t
pfix_parse_file(const char *file);

/**
 * much like umpf_parse_file_r() */
extern umpf_fix_t
pfix_parse_file_r(const char *file);

/**
 * much like umpf_parse_blob() */
extern umpf_fix_t
pfix_parse_blob(pfix_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * much like umpf_parse_blob_r() */
extern umpf_fix_t
pfix_parse_blob_r(pfix_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * much like umpf_seria_msg() */
extern size_t
pfix_seria_fix(char **tgt, size_t tsz, umpf_fix_t);


/* private stuff */
/* structure aware helpers, move to lib? */
#if !defined UNLIKELY
# define UNLIKELY(_x)	__builtin_expect((_x), 0)
#endif
#define ADDF(__sup, __str, __slot, __inc)		\
static __str*						\
__sup##_add_##__slot(struct pfix_##__sup##_s *o)	\
{							\
	size_t idx = (o)->n##__slot++;			\
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
struct pfix_##__sup##_##__slot##_meth_s {		\
	__str*(*add_f)(struct pfix_##__sup##_s *o);	\
}

/* adder for batches, step is 16 */
ADDF(fixml, struct pfix_batch_s, batch, 16);
ADDF(rgst_instrctns, struct pfix_rg_dtl_s, rg_dtl, 4);
ADDF(rg_dtl, struct pfix_pty_s, pty, 4);
ADDF(req_for_poss, struct pfix_pty_s, pty, 4);
ADDF(appl_msg_req, struct pfix_appl_id_req_grp_s, air_grp, 4);
ADDF(appl_msg_req_ack, struct pfix_appl_id_req_grp_s, aira_grp, 4);
ADDF(appl_id_req_grp, struct pfix_pty_s, pty, 4);

ADDF(pos_rpt, struct pfix_pty_s, pty, 4);
ADDF(pos_rpt, struct pfix_instrmt_s, instrmt, 4);
ADDF(pos_rpt, struct pfix_qty_s, qty, 4);

ADDF(pty, struct pfix_sub_s, sub, 4);
ADDF(sec_def, struct pfix_instrmt_s, instrmt, 4);

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_umpf_h_ */
