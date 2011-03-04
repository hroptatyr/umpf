/*** umpf.h -- fixml position messages
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

#if !defined INCLUDED_umpf_private_h_
#define INCLUDED_umpf_private_h_

#include <stdio.h>
#include <stddef.h>
#include <time.h>

#if defined __cplusplus
extern "C" {
#endif	/* __cplusplus */

/* all tags we understand */
typedef enum {
	/* must be first */
	UMPF_TAG_UNK,
	/* alphabetic list of tags */
	UMPF_TAG_AMT,
	UMPF_TAG_BATCH,
	UMPF_TAG_FIXML,
	UMPF_TAG_GLUE,
	UMPF_TAG_INSTRMT,

	UMPF_TAG_POS_RPT,
	UMPF_TAG_PTY,
	UMPF_TAG_QTY,
	UMPF_TAG_REQ_FOR_POSS,
	UMPF_TAG_REQ_FOR_POSS_ACK,

	UMPF_TAG_RG_DTL,
	UMPF_TAG_RGST_INSTRCTNS,
	UMPF_TAG_RGST_INSTRCTNS_RSP,
	UMPF_TAG_SUB,

} umpf_tid_t;

typedef enum {
	UMPF_REQ_TYP_POSS,
	UMPF_REQ_TYP_TRADES,
	UMPF_REQ_TYP_EXER,
	UMPF_REQ_TYP_ASSS,
	UMPF_REQ_TYP_STLM_ACT,
	UMPF_REQ_TYP_BACK_OUT_MSG,
	UMPF_REQ_TYP_DELTA_POSS,
} umpf_req_typ_t;

typedef enum {
	UMPF_RSLT_VALID,
	UMPF_RSLT_INVALID,
	UMPF_RSLT_NO_MATCH,
	UMPF_RSLT_NOT_AUTH,
	UMPF_RSLT_NOT_SUPP,
	UMPF_RSLT_OTHER = 99,
} umpf_rslt_t;

typedef enum {
	UMPF_STAT_COMPLETED,
	UMPF_STAT_WARNINGS,
	UMPF_STAT_REJECTED,
} umpf_stat_t;

typedef enum {
	UMPF_POS_QTY_SUBMITTED,
	UMPF_POS_QTY_ACCEPTED,
	UMPF_POS_QTY_REJECTED,
} umpf_pos_qty_stat_t;


struct __sub_s {
	char *id;
	/* actually an enum */
	unsigned int typ;
};

struct __pty_s {
	char *id;
	char src;
	/* actually an enum */
	unsigned int role;

	size_t nsub;
	struct __sub_s *sub;
};

struct __instrmt_s {
	char *sym;
	char *id;
	char *src;

#if 0
	/* we do not support SecAltIDGrp ... yet */
	struct __sec_aid_s aid[1];
#endif	/* 0 */
};

struct __qty_s {
	/* actually an enum */
	char *typ;
	double _long;
	double _short;
	umpf_pos_qty_stat_t stat;
	time_t qty_dt;
};

struct __amt_s {
	/* actually an enum */
	char *typ;
	double amt;
	char ccy[3];
};


/* top level messages */
struct __req_for_poss_s {
	time_t biz_dt;
	time_t txn_tm;
	umpf_req_typ_t req_typ;
	char *req_id;
	char *set_ses_id;

	/* variadic stuff */
	size_t npty;
	struct __pty_s *pty;
};

struct __req_for_poss_ack_s {
	char *rpt_id;
	time_t biz_dt;
	time_t txn_tm;
	umpf_req_typ_t req_typ;
	size_t tot_rpts;
	umpf_rslt_t rslt;
	umpf_stat_t stat;
	char *set_ses_id;

	/* variadic stuff */
	size_t npty;
	struct __pty_s *pty;
};

struct __pos_rpt_s {
	char *rpt_id;
	umpf_req_typ_t req_typ;
	size_t tot_rpts;
	umpf_rslt_t rslt;
	time_t biz_dt;
	char *set_ses_id;

	struct __instrmt_s instrmt[1];

	/* variadic stuff */
	size_t npty;
	struct __pty_s *pty;

	size_t nqty;
	struct __qty_s *qty;

	size_t namt;
	struct __amt_s *amt;
};

union __g_msg_u {
	struct __req_for_poss_s req_for_poss[1];
	struct __req_for_poss_ack_s req_for_poss_ack[1];
	struct __pos_rpt_s pos_rpt[1];
	void *ptr[1];
};

struct __g_msg_s {
	umpf_tid_t tid;
	union __g_msg_u msg;
};

struct __batch_s {
	size_t nmsg;
	struct __g_msg_s *msg;
};

/* top-level FIXML standard header */
struct __umpf_s {
	umpf_tid_t top;
	union {
		struct __batch_s batch[1];
		union __g_msg_u msg;
		void *ptr[1];
	};
};

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_umpf_private_h_ */
