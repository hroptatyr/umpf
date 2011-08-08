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

#if !defined INCLUDED_umpf_h_
#define INCLUDED_umpf_h_

#include <stddef.h>
#include <time.h>

#if defined __cplusplus
extern "C" {
#endif	/* __cplusplus */

typedef void *umpf_ctx_t;
typedef struct __umpf_s *umpf_doc_t;
typedef union umpf_msg_u *umpf_msg_t;

/* message types */
typedef enum {
	UMPF_MSG_UNK,
	/* portfolio transactions */
	UMPF_MSG_LST_PF,
	UMPF_MSG_CLO_PF,
	UMPF_MSG_NEW_PF,
	UMPF_MSG_GET_PF,
	UMPF_MSG_SET_PF,
	/* portoflio descriptions */
	UMPF_MSG_GET_DESCR,
	UMPF_MSG_SET_DESCR,
	/* security definitions */
	UMPF_MSG_NEW_SEC,
	UMPF_MSG_GET_SEC,
	UMPF_MSG_SET_SEC,
	/* will be coalesced with od/uschi messages one day
	 * diff takes 2 portfolios and returns a stream of od/uschi msgs
	 * patch takes a portfolio and a stream of od/uschi msgs */
	UMPF_MSG_DIFF,
	UMPF_MSG_PATCH,
	/* tag transactions */
	UMPF_MSG_LST_TAG,
	UMPF_MSG_DEL_TAG,
} umpf_msg_type_t;

typedef enum {
	QSIDE_UNK,
	QSIDE_OPEN_LONG,
	QSIDE_CLOSE_LONG,
	QSIDE_OPEN_SHORT,
	QSIDE_CLOSE_SHORT,
} qside_t;

/* stuff we need to identify an instrument */
struct __ins_s {
	char *sym;
};

/* naked portfolios */
struct __qty_s {
	double _long;
	double _shrt;
};

/* qsides are positions plus side */
struct __qsd_s {
	double pos;
	qside_t sd;
};

struct __ins_qty_s {
	struct __ins_s ins[1];
	union {
		struct __qty_s qty[1];
		struct __qsd_s qsd[1];
	};
};

struct __satell_s {
	char *data;
	size_t size;
};

struct umpf_msg_hdr_s {
	/* this is generally msg_type * 2 */
	unsigned int mt;
	void *p;
};

/* RgstInstrctns -> new_pf */
struct umpf_msg_new_pf_s {
	struct umpf_msg_hdr_s hdr;

	char *name;
	struct __satell_s satellite[1];
};

/* RgstInstrctnsRsp without reg name -> lst_pf */
struct umpf_msg_lst_pf_s {
	struct umpf_msg_hdr_s hdr;

	size_t npfs;
	char *pfs[];
};

/* SecDefUpd -> new_sec */
struct umpf_msg_new_sec_s {
	struct umpf_msg_hdr_s hdr;

	struct __ins_s ins[1];
	struct __satell_s satellite[1];

	char *pf_mnemo;
};

/* ReqForPoss[Ack] -> get_pf/set_pf */
struct umpf_msg_pf_s {
	struct umpf_msg_hdr_s hdr;

	char *name;
	time_t stamp;
	time_t clr_dt;

	size_t nposs;
	struct __ins_qty_s poss[];
};

union umpf_msg_u {
	struct umpf_msg_hdr_s hdr;
	struct umpf_msg_pf_s pf;
	struct umpf_msg_new_pf_s new_pf;
	struct umpf_msg_new_sec_s new_sec;
	struct umpf_msg_lst_pf_s lst_pf;
};


/* some useful functions */

/**
 * Parse bla bla ... */
extern umpf_msg_t umpf_parse_file(const char *file);

/**
 * Like `umpf_parse_file()' but re-entrant (and thus slower). */
extern umpf_msg_t umpf_parse_file_r(const char *file);

/* blob parsing */
/**
 * Parse BSZ bytes in BUF and, by side effect, obtain a context.
 * That context can be reused in subsequent calls to
 * `umpf_parse_blob()' to parse fragments of XML documents in
 * several goes, use a NULL pointer upon the first go.
 * If CTX becomes NULL the document is either finished or
 * errors have occurred, the return value will be the document
 * in the former case or NULL in the latter. */
extern umpf_msg_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Like `umpf_parse_blob()' but re-entrant (and thus slower). */
extern umpf_msg_t
umpf_parse_blob_r(umpf_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Free resources associated with MSG. */
extern void umpf_free_msg(umpf_msg_t);

/**
 * Print DOC to OUTFD. */
extern size_t umpf_print_msg(int outfd, umpf_msg_t);

/**
 * Print DOC to *TGT of size TSZ, return the final size.
 * If TSZ bytes are not enough to hold the entire contents
 * the buffer *TGT will be resized using realloc(). */
extern size_t umpf_seria_msg(char **tgt, size_t tsz, umpf_msg_t);

/**
 * Resize message to take NPOS additional positions. */
extern umpf_msg_t umpf_msg_add_pos(umpf_msg_t msg, size_t npos);

/**
 * Name space URI for FIXML 5.0 */
extern const char fixml50_ns_uri[];

/**
 * Name space URI for FIXML 4.4 */
extern const char fixml44_ns_uri[];


/* some convenience inlines */
static inline void
umpf_set_msg_type(umpf_msg_t msg, umpf_msg_type_t mt)
{
	msg->hdr.mt = mt * 2;
	return;
}

static inline umpf_msg_type_t
umpf_get_msg_type(umpf_msg_t msg)
{
	return (umpf_msg_type_t)(msg->hdr.mt / 2);
}

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_umpf_h_ */
