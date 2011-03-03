#include <string.h>
#include "pfd.h"

#define countof(x)	(sizeof(x) / sizeof(*x))

static const char blob1[] = "<?xml version=\"1.0\"?>\
<FIXML xmlns=\"http://www.fixprotocol.org/FIXML-4-4\"\
	r=\"20030618\" s=\"20040109\" v=\"4.4\">\
	<Batch>\
		<ReqForPossAck RptID=\"1234567\"\
			BizDt=\"2009-10-27\"\
			ReqTyp=\"0\"\
			TotRpts=\"2\"\
			Rslt=\"0\"\
			Sta";
static const char blob2[] = "t=\"0\"\
			SetSesID=\"ITD\"\
			TxnTm=\" 2010-02-25T14:40:31\">\
			<Pty ID=\"my_currencies\" R=\"4\">\
				<Sub ID=\"C\" Typ=\"26\"/>\
			</Pty>\
		</ReqForPossAck>\
";
static const char blob3[] = "\
	</Batch>\
</FIXML>\
";

static const char *blobs[] = {
	blob1,
	blob2,
	blob3,
	blob3
};
static const size_t blszs[] = {
	sizeof(blob1) - 1,
	sizeof(blob2) - 1,
	sizeof(blob3) - 1,
	0
};

int
main(int argc, const char *argv[])
{
	pfd_doc_t doc;
	pfd_ctx_t ctx;
	char altogether[4096] = {0};
	size_t len;
	size_t i;

	doc = pfd_parse_file(argv[1]);
	pfd_free_doc(doc);

	doc = pfd_parse_file(argv[1]);
	pfd_free_doc(doc);

	doc = pfd_parse_file_r(argv[1]);
	pfd_print_doc(doc, stdout);
	pfd_free_doc(doc);

	for (i = 0, ctx = NULL; i < countof(blobs); i++) {
		doc = pfd_parse_blob(&ctx, blobs[i], blszs[i]);
		if (doc != NULL) {
			/* definite success */
			fprintf(stderr, "finally\n");
			pfd_print_doc(doc, stdout);
			pfd_free_doc(doc);

		} else if (/* doc == NULL && */ctx == NULL) {
			/* error occurred */
			fprintf(stderr, "error in blob parsing\n");
		}
		/* otherwise everything's ok */
	}

	for (i = 0, ctx = NULL; i < countof(blobs); i++) {
		doc = pfd_parse_blob_r(&ctx, blobs[i], blszs[i]);
		if (doc != NULL) {
			/* definite success */
			pfd_print_doc(doc, stdout);
			pfd_free_doc(doc);

		} else if (/* doc == NULL && */ctx == NULL) {
			/* error occurred */
			fprintf(stderr, "error in reentrant blob parsing\n");
		}
		/* otherwise everything's ok */
	}

	/* send 'em altogether now */
	strcat(altogether, blob1);
	strcat(altogether, blob2);
	strcat(altogether, blob3);
	len = strlen(altogether);

	ctx = NULL;
	doc = pfd_parse_blob(&ctx, altogether, len);
	if (doc == NULL) {
		fprintf(stderr, "no doc %p\n", ctx);
	} else if (ctx != NULL) {
		fprintf(stderr, "wants more data we haven't got\n");
	} else {
		pfd_free_doc(doc);
	}

	ctx = NULL;
	doc = pfd_parse_blob_r(&ctx, altogether, len);
	if (doc == NULL) {
		fprintf(stderr, "no doc %p\n", ctx);
	} else if (ctx != NULL) {
		fprintf(stderr, "wants more data we haven't got\n");
	} else {
		pfd_free_doc(doc);
	}
	return 0;
}

/* testlib.c ends here */
