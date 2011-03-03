#include <string.h>
#include "umpf.h"

#define countof(x)	(sizeof(x) / sizeof(*x))

static const char blob1[] = "<?xml version=\"1.0\"?>\n\
<FIXML xmlns=\"http://www.fixprotocol.org/FIXML-4-4\"\n\
	r=\"20030618\" s=\"20040109\" v=\"4.4\">\n\
	<Batch>\n\
		<ReqForPossAck RptID=\"1234567\"\n\
			BizDt=\"2009-10-27\"\n\
			ReqTyp=\"0\"\n\
			TotRpts=\"2\"\n\
			Rslt=\"0\"\n\
			Sta";
static const char blob2[] = "t=\"0\"\n\
			SetSesID=\"ITD\"\n\
			TxnTm=\" 2010-02-25T14:40:31\">\n\
			<Pty ID=\"my_currencies\" R=\"4\">\n\
				<Sub ID=\"C\" Typ=\"26\"/>\n\
			</Pty>\n\
		</ReqForPossAck>\n\
";
static const char blob3[] = "\n\
	</Batch>\n\
</FIXML>\n\
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

static void
test_files(const char *file)
{
	umpf_msg_t msg;

	msg = umpf_parse_file(file);
	umpf_free_msg(msg);

	msg = umpf_parse_file(file);
	umpf_free_msg(msg);

	msg = umpf_parse_file_r(file);
	umpf_print_msg(msg, stdout);
	umpf_free_msg(msg);
	return;
}

static void
test_blobs(void)
{
	umpf_ctx_t ctx = NULL;
	umpf_msg_t msg;
	size_t i;

	for (i = 0, ctx = NULL; i < countof(blobs); i++) {
		msg = umpf_parse_blob(&ctx, blobs[i], blszs[i]);
		if (msg != NULL) {
			/* definite success */
			fprintf(stderr, "finally\n");
			umpf_print_msg(msg, stdout);
			umpf_free_msg(msg);

		} else if (/* msg == NULL && */ctx == NULL) {
			/* error occurred */
			fprintf(stderr, "error in blob parsing\n");
		}
		/* otherwise everything's ok */
	}

	for (i = 0, ctx = NULL; i < countof(blobs); i++) {
		msg = umpf_parse_blob_r(&ctx, blobs[i], blszs[i]);
		if (msg != NULL) {
			/* definite success */
			umpf_print_msg(msg, stdout);
			umpf_free_msg(msg);

		} else if (/* msg == NULL && */ctx == NULL) {
			/* error occurred */
			fprintf(stderr, "error in reentrant blob parsing\n");
		}
		/* otherwise everything's ok */
	}
	return;
}

static void
test_altogether(char *blob, size_t len)
{
	umpf_ctx_t ctx = NULL;
	umpf_msg_t msg;

	msg = umpf_parse_blob(&ctx, blob, len);
	if (msg == NULL) {
		fprintf(stderr, "no msg %p\n", ctx);
	} else if (ctx != NULL) {
		fprintf(stderr, "wants more data we haven't got\n");
	} else {
		umpf_free_msg(msg);
	}

	ctx = NULL;
	msg = umpf_parse_blob_r(&ctx, blob, len);
	if (msg == NULL) {
		fprintf(stderr, "no msg %p\n", ctx);
	} else if (ctx != NULL) {
		fprintf(stderr, "wants more data we haven't got\n");
	} else {
		umpf_free_msg(msg);
	}
	return;
}

static void
test_linewise(char *blob, size_t __attribute__((unused)) len)
{
	umpf_ctx_t ctx = NULL;
	umpf_msg_t msg;
	char *tmp, *old = blob;

	fprintf(stderr, "linewise\n");
	while ((tmp = strchr(old, '\n'))) {
		msg = umpf_parse_blob_r(&ctx, old, tmp - old + 1);

		if (msg == NULL && ctx != NULL) {
			/* more data */
			fprintf(stderr, "more\n");
		} else if (msg == NULL && ctx == NULL) {
			fprintf(stderr, "error\n");
			return;
		} else if (msg != NULL) {
			/* yippie */
			fprintf(stderr, "yippie\n");
			umpf_free_msg(msg);
		}
		old = tmp + 1;
	}
	/* now it should be there */
	msg = umpf_parse_blob_r(&ctx, old, len - (old - blob));

	if (msg == NULL && ctx != NULL) {
		/* more data */
		fprintf(stderr, "more\n");
	} else if (msg == NULL && ctx == NULL) {
		fprintf(stderr, "error\n");
	} else if (msg != NULL) {
		/* yippie */
		fprintf(stderr, "yippie\n");
		umpf_free_msg(msg);
	}
	return;
}

static void
test_blks(char *blob, size_t len)
{
	umpf_ctx_t ctx = NULL;
	umpf_msg_t msg;

	fprintf(stderr, "blkwise\n");
	for (size_t i = 0; i < len; i += 5) {
		msg = umpf_parse_blob_r(&ctx, blob + i, i + 5 < len ? 5 : len - i);

		if (msg == NULL && ctx != NULL) {
			/* more data */
			fprintf(stderr, "more\n");
		} else if (msg == NULL && ctx == NULL) {
			fprintf(stderr, "error\n");
			return;
		} else if (msg != NULL) {
			/* yippie */
			fprintf(stderr, "yippie\n");
			umpf_free_msg(msg);
		}
	}
	return;
}

#include <libxml/parser.h>

static void
se(void *ctx __attribute__((unused)), const xmlChar *name, const xmlChar **atts)
{
	int i;

	fprintf(stdout, "SAX.startElement(%s", (char*)name);
	if (atts != NULL) {
		for (i = 0;(atts[i] != NULL);i++) {
			fprintf(stdout, ", %s='", (char*)atts[i++]);
			if (atts[i] != NULL) {
				fprintf(stdout, "%s'", (char*)atts[i]);
			}
		}
	}
	fprintf(stdout, ")\n");
	return;
}

static void
ee(void *ctx __attribute__((unused)), const xmlChar *name)
{
	fprintf(stdout, "SAX.endElement(%s)\n", (char*)name);
	return;
}

static xmlSAXHandler hdl = {
	.startElement = se,
	.endElement = ee,
};

static void __attribute__((unused))
test_libxml2(char *blob, size_t len)
{
	void *pp = xmlCreatePushParserCtxt(&hdl, NULL, blob, 0, NULL);

	fprintf(stderr, "blkwise orig\n");
	for (size_t i = 0; i < len; i += 5) {
		int ret = xmlParseChunk(pp, blob + i, i + 5 < len ? 5 : len - i, 0);
		fprintf(stderr, "%i\n", ret);
	}
	return;
}

int
main(int argc, const char *argv[])
{
	char altogether[4096] = {0};
	size_t len;

	/* file tests */
	test_files(argv[1]);

	/* blob tests */
	test_blobs();

	/* send 'em altogether now */
	strcat(altogether, blob1);
	strcat(altogether, blob2);
	strcat(altogether, blob3);
	len = strlen(altogether);

	test_altogether(altogether, len);

	test_linewise(altogether, len);

	test_blks(altogether, len);

#if 0
	test_libxml2(altogether, len);
#endif	/* 0 */
	return 0;
}

/* testlib.c ends here */
