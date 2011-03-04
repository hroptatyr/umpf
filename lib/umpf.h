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

#include <stdio.h>
#include <stddef.h>
#include <time.h>

#if defined __cplusplus
extern "C" {
#endif	/* __cplusplus */

typedef void *umpf_ctx_t;
typedef struct __umpf_s *umpf_doc_t;


/* some useful functions */

/**
 * Parse bla bla ... */
extern umpf_doc_t umpf_parse_file(const char *file);

/**
 * Like `umpf_parse_file()' but re-entrant (and thus slower). */
extern umpf_doc_t umpf_parse_file_r(const char *file);

/* blob parsing */
/**
 * Parse BSZ bytes in BUF and, by side effect, obtain a context.
 * That context can be reused in subsequent calls to
 * `umpf_parse_blob()' to parse fragments of XML documents in
 * several goes, use a NULL pointer upon the first go.
 * If CTX becomes NULL the document is either finished or
 * errors have occurred, the return value will be the document
 * in the former case or NULL in the latter. */
extern umpf_doc_t
umpf_parse_blob(umpf_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Like `umpf_parse_blob()' but re-entrant (and thus slower). */
extern umpf_doc_t
umpf_parse_blob_r(umpf_ctx_t *ctx, const char *buf, size_t bsz);

/**
 * Oh yes. */
extern void umpf_free_doc(umpf_doc_t);

/**
 * Print DOC to OUT. */
extern void umpf_print_doc(umpf_doc_t, FILE *out);

/**
 * Name space URI for FIXML 5.0 */
extern const char fixml50_ns_uri[];

/**
 * Name space URI for FIXML 4.4 */
extern const char fixml44_ns_uri[];

#if defined __cplusplus
}
#endif	/* __cplusplus */

#endif	/* INCLUDED_umpf_h_ */
