/*** umpf.c -- public message handling
 *
 * Copyright (C) 2011 Sebastian Freundt
 *
 * Author:  Sebastian Freundt <freundt@ga-group.nl>
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
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "nifty.h"
#include "umpf.h"
#include "umpf-private.h"

void
umpf_free_msg(umpf_msg_t msg)
{
	switch (umpf_get_msg_type(msg)) {
	case UMPF_MSG_NEW_PF:
		/* satellite only occurs in new pf */
		if (msg->new_pf.satellite->data) {
			xfree(msg->new_pf.satellite->data);
		}
		goto common;

	case UMPF_MSG_NEW_SEC:
	case UMPF_MSG_GET_SEC:
	case UMPF_MSG_SET_SEC:
		/* satellite and portfolio mnemo must be freed */
		if (msg->new_sec.satellite->data) {
			xfree(msg->new_sec.satellite->data);
		}
		if (msg->new_sec.pf_mnemo) {
			xfree(msg->new_sec.pf_mnemo);
		}
		goto common;

	case UMPF_MSG_GET_PF:
	case UMPF_MSG_SET_PF:
#if 0
		/* the ins_qty's must be freed too */
		/* wrong, the ins_qty's are contiguous and stem
		 * from realloc'ing */
		for (size_t j = 0; j < msg->pf.nposs; j++) {
			struct __ins_qty_s *iq = msg->pf.poss + j;
			if (iq->instr) {
				xfree(iq->instr);
			}
		}
#endif	/* 0 */
	common:
		/* common to all messages */
		if (msg->pf.name) {
			xfree(msg->pf.name);
		}
	default:
		break;
	}
	xfree(msg);
	return;
}

umpf_msg_t
umpf_msg_add_pos(umpf_msg_t msg, size_t npos)
{
	size_t cur_nposs = msg->pf.nposs;
	size_t new_nposs = cur_nposs + npos;

	msg = realloc(msg, sizeof(*msg) + new_nposs * sizeof(*msg->pf.poss));
	msg->pf.nposs = new_nposs;
	/* rinse */
	memset(msg->pf.poss + cur_nposs, 0, npos * sizeof(*msg->pf.poss));
	return msg;
}

/* umpf.c ends here */
