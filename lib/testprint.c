#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "umpf.h"
#include "proto-fixml.h"
#include "proto-fixml-tag.c"

int
main(void)
{
	struct pfix_pty_s pty = {
		.prim.id = "mf70_test_freundt",
		.prim.src = '\'',
		.prim.r = 0,

		.nsub = 0,
		.sub = NULL,
	};
	struct pfix_batch_s batch[] = {
		{
			.tag = UMPF_TAG_REQ_FOR_POSS,
			.req_for_poss.req_typ = 0,
			.req_for_poss.txn_tm = 1234567890,
			.req_for_poss.biz_dt = 1234567890,
			.req_for_poss.npty = 1,
			.req_for_poss.pty = &pty,
		}, {
			.tag = UMPF_TAG_REQ_FOR_POSS_ACK,
			.req_for_poss_ack.rfp.req_typ = 0,
			.req_for_poss_ack.rfp.txn_tm = 1234567890,
			.req_for_poss_ack.rfp.biz_dt = 1234567890,
			.req_for_poss_ack.rfp.npty = 1,
			.req_for_poss_ack.rfp.pty = &pty,
			.req_for_poss_ack.tot_rpts = 0,
		}
	};
	struct pfix_fixml_s fix = {
		.nns = 0,
		.ns = NULL,

		.v = NULL,

		.nattr = 0,
		.attr = NULL,

		.nbatch = 2,
		.batch = batch,
	};
	char *outb = NULL;
	size_t outs;

	outs = umpf_seria_fix(&outb, 0, &fix);
	fwrite(outb, outs, 1, stdout);
	return 0;
}

/* testprint.c ends here */
