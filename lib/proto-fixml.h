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

struct pfix_sub_s {
	char *id;
	char src;
	int r;
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

struct pfix_rg_dtl_s {
	size_t npty;
	struct pfix_pty_s *pty;
};

struct pfix_rgst_instrctns_s {
	int trans_typ;

	size_t nrg_dtl;
	struct pfix_rg_dtl_s *rg_dtl;
};

struct pfix_rgst_instrctns_rsp_s {
	char *id;

	int trans_typ;

	size_t nrg_dtl;
	struct pfix_rg_dtl_s *rg_dtl;
};

struct pfix_batch_s {
	pfix_tid_t tag;
	union {
		struct pfix_req_for_poss_s req_for_poss;
		struct pfix_req_for_poss_ack_s req_for_poss_ack;
		struct pfix_pos_rpt_s pos_rpt;
		struct pfix_rgst_instrctns_s rgst_instrctns;
		struct pfix_rgst_instrctns_rsp_s rgst_instrctns_rsp;
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

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_umpf_h_ */
