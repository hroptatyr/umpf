/*** fixml.h -- fixml position messages
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

#if !defined INCLUDED_fixml_h_
#define INCLUDED_fixml_h_

#include <stdio.h>
#include <stddef.h>
#include <time.h>

#if defined __cplusplus
extern "C" {
#endif	/* __cplusplus */

typedef void *pfd_ctx_t;

/* all tags we understand */
typedef enum {
	/* must be first */
	PFD_TAG_UNK,
	/* alphabetic list of tags */
	PFD_TAG_AMT,
	PFD_TAG_BATCH,
	PFD_TAG_FIXML,
	PFD_TAG_INSTRMT,
	PFD_TAG_POS_RPT,

	PFD_TAG_PTY,
	PFD_TAG_QTY,
	PFD_TAG_REQ_FOR_POSS,
	PFD_TAG_REQ_FOR_POSS_ACK,
	PFD_TAG_SUB,

} pfd_tid_t;

typedef enum {
	PFD_REQ_TYP_POSS,
	PFD_REQ_TYP_TRADES,
	PFD_REQ_TYP_EXER,
	PFD_REQ_TYP_ASSS,
	PFD_REQ_TYP_STLM_ACT,
	PFD_REQ_TYP_BACK_OUT_MSG,
	PFD_REQ_TYP_DELTA_POSS,
} pfd_req_typ_t;

typedef enum {
	PFD_RSLT_VALID,
	PFD_RSLT_INVALID,
	PFD_RSLT_NO_MATCH,
	PFD_RSLT_NOT_AUTH,
	PFD_RSLT_NOT_SUPP,
	PFD_RSLT_OTHER = 99,
} pfd_rslt_t;

typedef enum {
	PFD_STAT_COMPLETED,
	PFD_STAT_WARNINGS,
	PFD_STAT_REJECTED,
} pfd_stat_t;

typedef enum {
	PFD_POS_QTY_SUBMITTED,
	PFD_POS_QTY_ACCEPTED,
	PFD_POS_QTY_REJECTED,
} pfd_pos_qty_stat_t;

typedef struct __pfd_s *pfd_doc_t;


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
	pfd_pos_qty_stat_t stat;
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
	pfd_req_typ_t req_typ;
	char *req_id;
	time_t txn_tm;
	char *set_ses_id;

	/* variadic stuff */
	size_t npty;
	struct __pty_s *pty;
};

struct __req_for_poss_ack_s {
	char *rpt_id;
	time_t biz_dt;
	pfd_req_typ_t req_typ;
	size_t tot_rpts;
	pfd_rslt_t rslt;
	pfd_stat_t stat;
	char *set_ses_id;
	time_t txn_tm;

	/* variadic stuff */
	size_t npty;
	struct __pty_s *pty;
};

struct __pos_rpt_s {
	char *rpt_id;
	pfd_req_typ_t req_typ;
	size_t tot_rpts;
	pfd_rslt_t rslt;
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
	pfd_tid_t tid;
	union __g_msg_u msg;
};

struct __batch_s {
	size_t nmsg;
	struct __g_msg_s *msg;
};

/* top-level FIXML standard header */
struct __pfd_s {
	pfd_tid_t top;
	union {
		struct __batch_s batch[1];
		union __g_msg_u msg;
		void *ptr[1];
	};
};


/* some useful functions */

/**
 * Parse bla bla ... */
extern pfd_doc_t pfd_parse_file(const char *file);

/**
 * Like `pfd_parse_file()' but re-entrant (and thus slower). */
extern pfd_doc_t pfd_parse_file_r(const char *file);

/* blob parsing */
/**
 * Parse BSZ bytes in BUF and, by side effect, obtain a context.
 * That context can be reused in subsequent calls to
 * `pfd_parse_blob()' to parse fragments of XML documents in
 * several goes, use a NULL pointer upon the first go.
 * If CTX becomes NULL the document is either finished or
 * errors have occurred, the return value will be the document
 * in the former case or NULL in the latter. */
extern pfd_doc_t pfd_parse_blob(pfd_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Like `pfd_parse_blob()' but re-entrant (and thus slower). */
extern pfd_doc_t pfd_parse_blob_r(pfd_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Oh yes. */
extern void pfd_free_doc(pfd_doc_t);

/**
 * Print DOC to OUT. */
extern void pfd_print_doc(pfd_doc_t, FILE *out);

/**
 * Name space URI for FIXML 5.0 */
extern const char fixml50_ns_uri[];

/**
 * Name space URI for FIXML 4.4 */
extern const char fixml44_ns_uri[];

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_fixml_h_ */
