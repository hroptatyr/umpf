/*** just to focus on the essential stuff in the dso-umpf module */
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <errno.h>
/* we just need the headers hereof and hope that unserding used the same ones */
#include <ev.h>
#include "nifty.h"
#include "con6ity.h"

#if defined __INTEL_COMPILER
# pragma warning (disable:2259)
# pragma warning (disable:177)
#endif	/* __INTEL_COMPILER */

#define C10Y_PRE	"mod/umpf/con6ity"


/* services for includers that need not know about libev */
#define FD_MAP_TYPE	ev_io*

DEFUN int __attribute__((unused))
get_fd(umpf_conn_t ctx)
{
	FD_MAP_TYPE io = ctx;
	return io->fd;
}

DEFUN void* __attribute__((unused))
get_fd_data(umpf_conn_t ctx)
{
	FD_MAP_TYPE io = ctx;
	return io->data;
}

DEFUN void __attribute__((unused))
put_fd_data(umpf_conn_t ctx, void *data)
{
	FD_MAP_TYPE io = ctx;
	io->data = data;
	return;
}


/* connection mumbo-jumbo */
size_t nwio = 0;
static ev_io __wio[2];

static void
__shut_sock(int s)
{
	shutdown(s, SHUT_RDWR);
	close(s);
	return;
}

static void
clo_wio(EV_P_ ev_io *w)
{
	fsync(w->fd);
	ev_io_stop(EV_A_ w);
	__shut_sock(w->fd);
	xfree(w);
	return;
}

static inline int
getsockopt_int(int s, int level, int optname)
{
	int res[1];
	socklen_t rsz = sizeof(*res);
	if (getsockopt(s, level, optname, res, &rsz) >= 0) {
		return *res;
	}
	return -1;
}

static inline int
setsockopt_int(int s, int level, int optname, int value)
{
	return setsockopt(s, level, optname, &value, sizeof(value));
}

/**
 * Mark address behind socket S as reusable. */
static inline int
setsock_reuseaddr(int s)
{
#if defined SO_REUSEADDR
	return setsockopt_int(s, SOL_SOCKET, SO_REUSEADDR, 1);
#else  /* !SO_REUSEADDR */
	return 0;
#endif	/* SO_REUSEADDR */
}

/* probably only available on BSD */
static inline int
setsock_reuseport(int __attribute__((unused)) s)
{
#if defined SO_REUSEPORT
	return setsockopt_int(s, SOL_SOCKET, SO_REUSEPORT, 1);
#else  /* !SO_REUSEPORT */
	return 0;
#endif	/* SO_REUSEPORT */
}

/* we could take args like listen address and port number */
DEFUN int
conn_listener_net(uint16_t port)
{
#if defined IPPROTO_IPV6
	static struct sockaddr_in6 __sa6 = {
		.sin6_family = AF_INET6,
		.sin6_addr = IN6ADDR_ANY_INIT
	};
	volatile int s;

	/* non-constant slots of __sa6 */
	__sa6.sin6_port = htons(port);

	if (LIKELY((s = socket(PF_INET6, SOCK_STREAM, 0)) >= 0)) {
		/* likely case upfront */
		;
	} else {
		UMPF_DEBUG(C10Y_PRE ": socket() failed ... I'm clueless now\n");
		return s;
	}

#if defined IPV6_V6ONLY
	setsockopt_int(s, IPPROTO_IPV6, IPV6_V6ONLY, 0);
#endif	/* IPV6_V6ONLY */
#if defined IPV6_USE_MIN_MTU
	/* use minimal mtu */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_USE_MIN_MTU, 1);
#endif
#if defined IPV6_DONTFRAG
	/* rather drop a packet than to fragment it */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_DONTFRAG, 1);
#endif
#if defined IPV6_RECVPATHMTU
	/* obtain path mtu to send maximum non-fragmented packet */
	setsockopt_int(s, IPPROTO_IPV6, IPV6_RECVPATHMTU, 1);
#endif
	setsock_reuseaddr(s);
	setsock_reuseport(s);

	/* we used to retry upon failure, but who cares */
	if (bind(s, (struct sockaddr*)&__sa6, sizeof(__sa6)) < 0 ||
	    listen(s, 2) < 0) {
		UMPF_DEBUG(C10Y_PRE ": bind() failed, errno %d\n", errno);
		close(s);
		return -1;
	}
	return s;

#else  /* !IPPROTO_IPV6 */
	return -1;
#endif	/* IPPROTO_IPV6 */
}

DEFUN int
conn_listener_uds(const char *sock_path)
{
	static struct sockaddr_un __s = {
		.sun_family = AF_LOCAL,
	};
	volatile int s;
	size_t sz;

	if (LIKELY((s = socket(PF_LOCAL, SOCK_DGRAM, 0)) >= 0)) {
		/* likely case upfront */
		;
	} else {
		UMPF_DEBUG(C10Y_PRE ": socket() failed ... I'm clueless now\n");
		return s;
	}

	/* bind a name now */
	strncpy(__s.sun_path, sock_path, sizeof(__s.sun_path));
	__s.sun_path[sizeof(__s.sun_path) - 1] = '\0';

	/* The size of the address is
	   the offset of the start of the filename,
	   plus its length,
	   plus one for the terminating null byte.
	   Alternatively you can just do:
	   size = SUN_LEN (&name); */
	sz = offsetof(struct sockaddr_un, sun_path) + strlen(__s.sun_path) + 1;

	/* just unlink the socket */
	unlink(sock_path);
	/* we used to retry upon failure, but who cares */
	if (bind(s, (struct sockaddr*)&__s, sz) < 0) {
		UMPF_DEBUG(C10Y_PRE ": bind() failed: %s\n", strerror(errno));
		close(s);
		return -1;
	}
	return s;
}


/* weak functions first */
#if !defined HAVE_handle_data
DEFUN_W int
handle_data(umpf_conn_t UNUSED(c), char *UNUSED(msg), size_t UNUSED(msglen))
{
	return 0;
}
#endif	/* !HAVE_handle_data */

#if !defined HAVE_handle_close
DEFUN_W void
handle_close(umpf_conn_t UNUSED(c))
{
	return;
}
#endif	/* !HAVE_handle_close */

static void
data_cb(EV_P_ ev_io *w, int UNUSED(re))
{
	char buf[4096];
	ssize_t nrd;

	if ((nrd = read(w->fd, buf, sizeof(buf))) <= 0) {
		UMPF_DEBUG(C10Y_PRE ": no data, closing socket %d\n", w->fd);
		handle_close(w);
		clo_wio(EV_A_ w);
		return;
	}
	UMPF_DEBUG(C10Y_PRE ": new data in sock %d\n", w->fd);
	if (handle_data(w, buf, nrd) < 0) {
		UMPF_DEBUG(C10Y_PRE ": negative, closing down\n");
		handle_close(w);
		clo_wio(EV_A_ w);
	}
	return;
}

static void
inco_cb(EV_P_ ev_io *w, int UNUSED(re))
{
/* we're tcp so we've got to accept() the bugger, don't forget :) */
	volatile int ns;
	ev_io *aw;
	struct sockaddr_storage sa;
	socklen_t sa_size = sizeof(sa);

	UMPF_DEBUG(C10Y_PRE ": they got back to us...");
	if ((ns = accept(w->fd, (struct sockaddr*)&sa, &sa_size)) < 0) {
		UMPF_DBGCONT("accept() failed\n");
		return;
	}

        /* make an io watcher and watch the accepted socket */
	aw = xnew(ev_io);
        ev_io_init(aw, data_cb, ns, EV_READ);
	aw->data = NULL;
        ev_io_start(EV_A_ aw);
	UMPF_DBGCONT("success, new sock %d\n", ns);
	return;
}

static void
clo_evsock(EV_P_ int UNUSED(type), void *w)
{
	ev_io *wp = w;

        /* deinitialise the io watcher */
        ev_io_stop(EV_A_ wp);
	/* properly shut the socket */
	__shut_sock(wp->fd);
	return;
}


DEFUN void
init_conn_watchers(void *loop, int s)
{
	struct ev_io *wio = __wio + nwio++;

	if (s < 0) {
		return;
	}

        /* initialise an io watcher, then start it */
        ev_io_init(wio, inco_cb, s, EV_READ);
        ev_io_start(EV_A_ wio);
	return;
}

DEFUN void
deinit_conn_watchers(void *UNUSED(loop))
{
#if defined EV_WALK_ENABLE && EV_WALK_ENABLE
	/* properly close all sockets */
	ev_walk(EV_A_ EV_IO, clo_evsock);
#else  /* !EV_WALK_ENABLE */
	for (int i = 0; i < nwio; i++) {
		clo_evsock(EV_A_ EV_IO, __wio + i);
	}
#endif	/* EV_WALK_ENABLE */
	return;
}

/* con6ity.c ends here */
