/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/9/11
 *
 * net/socket, none-block fds only
 * */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if !defined(_MSC_VER)  && !defined(_WIN32)

#include "Win32_Interop.h"
#include "hp_net.h"
#include "hp_sock_t.h"  /* hp_sock_t */
#include "hp_log.h"  /* hp_log */
#include <stdio.h>
#include <assert.h>     /* define NDEBUG to disable assertion */

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>  /* ioctl */
#include <sys/fcntl.h>  /* fcntl */
#include <arpa/inet.h>	/* inet_pton */
#include <sys/socket.h>	/* basic socket definitions */
#include <netinet/in.h>	/* sockaddr_in{} and other Internet defns */
#include <sys/uio.h>      /* iovec, writev, ... */
#include <stdlib.h>     /* free */
#include <string.h>     /* strerror */
#include <errno.h>
#if defined(_MSC_VER)
#include <WS2tcpip.h>   /* inet_pton */
#endif
#define LISTENQ 512  /* for listen() */

ssize_t hp_net_sendto(int fd, char const * ip, int port, char const * buf, size_t len)
{
	if (!(ip && buf))
		return -1;

	struct sockaddr_in servaddr = { 0 };
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &servaddr.sin_addr);

	ssize_t n = sendto(fd, buf, len, 0, (struct sockaddr *)&servaddr, sizeof(servaddr));
	if (n <= 0) {
		fprintf(stderr, "%s: sendto: errno=%d/'%s'\n", __FUNCTION__, errno, strerror(errno));
	}
	return n;
}

ssize_t hp_net_sendmsg2(int fd, char const * ip, int port, struct iovec * iov, size_t iovlen)
{
	if (!(ip && iov))
		return -1;

	struct sockaddr_in servaddr = { 0 };
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	inet_pton(AF_INET, ip, &servaddr.sin_addr);

	struct msghdr msg = { 0 };
	msg.msg_name = &servaddr;
	msg.msg_namelen = sizeof(servaddr);
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	ssize_t n = sendmsg(fd, &msg, 0);
	if (n <= 0) {
		fprintf(stderr, "%s: sendmsg: errno=%d/'%s'\n", __FUNCTION__, errno, strerror(errno));
	}
	return n;
}

ssize_t hp_net_sendmsg(int fd, struct sockaddr_in * servaddr, socklen_t len, struct iovec * iov, size_t iovlen)
{
	if (!(servaddr && iov))
		return -1;

	int i;

	struct msghdr msg = { 0 };
	msg.msg_name = servaddr;
	msg.msg_namelen = len;
	msg.msg_iov = iov;
	msg.msg_iovlen = iovlen;

	ssize_t nbytes = 0;	/* must first figure out what return value should be */
	for (i = 0; i < iovlen; ++i)
		nbytes += iov[i].iov_len;

	ssize_t n = sendmsg(fd, &msg, 0);
	if (n != nbytes) {
		fprintf(stderr, "%s: sendmsg: errno=%d/'%s'\n", __FUNCTION__, errno, strerror(errno));
	}
	return n;
}

#if defined(_MSC_VER)
/*
 * NOTE: the source code is mainly from book <unpv13e>,
 * sample url: https://github.com/k84d/unpv13e
 * advio/recvfromflags.c
 * advio/dgechoaddr.c
 * */
ssize_t
hp_net_recvmsg(int fd, void *ptr, size_t nbytes, int *flagsp,
	struct sockaddr_in *sa, socklen_t *salenptr, struct sockaddr_in * origindst)
{
	struct msghdr	msg;
	struct iovec	iov[1];
	ssize_t			n;

	struct cmsghdr	*cmptr;
	union {
		struct cmsghdr	cm;
		char				control[CMSG_SPACE(sizeof(struct in_addr)) +
			CMSG_SPACE(sizeof(struct sockaddr_in))];
	} control_un;

	msg.msg_control = control_un.control;
	msg.msg_controllen = sizeof(control_un.control);
	msg.msg_flags = 0;

	msg.msg_name = sa;
	msg.msg_namelen = *salenptr;
	iov[0].iov_base = ptr;
	iov[0].iov_len = nbytes;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;

	if ((n = recvmsg(fd, &msg, *flagsp)) < 0)
		return(n);

	*salenptr = msg.msg_namelen;	/* pass back results */
	if (origindst)
		bzero(origindst, sizeof(struct sockaddr_in));	/* 0.0.0.0, i/f = 0 */
/* end recvfrom_flags1 */

	*flagsp = msg.msg_flags;		/* pass back results */
	if (msg.msg_controllen < sizeof(struct cmsghdr) ||
		(msg.msg_flags & MSG_CTRUNC) || origindst == NULL)
		return(n);

	for (cmptr = CMSG_FIRSTHDR(&msg); cmptr != NULL;
		cmptr = CMSG_NXTHDR(&msg, cmptr)) {

		if (cmptr->cmsg_level == SOL_IP &&
			cmptr->cmsg_type == IP_ORIGDSTADDR) {

			memcpy(origindst, CMSG_DATA(cmptr),
				sizeof(struct sockaddr_in));

			//			char ip[16];
			//			inet_ntop(AF_INET, &origindst->sin_addr, ip, sizeof(ip));
			//			fprintf(stdout, "%s: IP_ORIGDSTADDR=%X:%d/'%s:%d'\n", __FUNCTION__
			//					, origindst->sin_addr.s_addr, origindst->sin_port
			//					, ip, ntohs(origindst->sin_port));
			continue;
		}
	}
	return(n);
}
#endif
/*
 * NOTE: code from hiredis/net.c
 * */
int hp_net_set_alive(hp_sock_t fd, int interval)
{
	int val = 1;

	if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == -1) {
		fprintf(stderr, "%s: setsockopt(SOL_SOCKET, SO_KEEPALIVE): %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	val = interval;

#ifdef _OSX
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPALIVE, &val, sizeof(val)) < 0) {
		fprintf(stderr, "%s: setsockopt(IPPROTO_TCP, TCP_KEEPALIVE): %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}
#else
#if defined(__GLIBC__) && !defined(__FreeBSD_kernel__)
	val = interval;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, sizeof(val)) < 0) {
		fprintf(stderr, "%s: setsockopt(IPPROTO_TCP, TCP_KEEPIDLE): %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	val = interval / 3;
	if (val == 0) val = 1;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, sizeof(val)) < 0) {
		fprintf(stderr, "%s: setsockopt(IPPROTO_TCP, TCP_KEEPINTVL): %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	val = 3;
	if (setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, sizeof(val)) < 0) {
		fprintf(stderr, "%s: setsockopt(IPPROTO_TCP, TCP_KEEPCNT): %s\n", __FUNCTION__, strerror(errno));
		return -1;
	}
#endif //_OSX

	return 0;
}

hp_sock_t hp_net_listen(int port)
{
	hp_sock_t fd;
	struct sockaddr_in	servaddr = { 0 };

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "%s: socket error('%s')\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	/* Make sure connection-intensive things like the redis benckmark
	 * will be able to close/open sockets a zillion of times */
	int yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		fprintf(stderr, "%s: setsockopt SO_REUSEADDR: %s", __FUNCTION__, strerror(errno));
		close(fd);
		return -1;
	}
#if (!defined _MSC_VER) || (defined LIBHP_WITH_WIN32_INTERROP)
	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
#else
	u_long sockopt = 1;
	if (ioctlsocket(fd, FIONBIO, &sockopt) < 0)
#endif /* LIBHP_WITH_WIN32_INTERROP */
	{
		fprintf(stderr, "%s: ioctl(FIONBIO) failed for fd=%d\n", __FUNCTION__, fd);
		close(fd);
		return -1;
	}

	if (bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "%s: bind error('%s'), port=%d\n"
			, __FUNCTION__, strerror(errno), port);
		close(fd);
		return -1;
	}

	if (listen(fd, LISTENQ) < 0) {
		fprintf(stderr, "%s: listen error(%s), port=%d\n"
			, __FUNCTION__, strerror(errno), port);
		close(fd);
		return -1;
	}
	return fd;
	}

#if defined(_MSC_VER)
int hp_net_udp_bind(char const * ip, int port)
{
	int fd;
	struct sockaddr_in	servaddr = { 0 };

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "%s: socket error('%s')\n", __FUNCTION__, strerror(errno));
		return -1;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	//	inet_pton(AF_INET, (ip? ip : "0.0.0.0"), &(servaddr.sin_addr));
		/* Make sure connection-intensive things like the redis benckmark
		 * will be able to close/open sockets a zillion of times */
	int yes = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
		fprintf(stderr, "%s: setsockopt SO_REUSEADDR: %s", __FUNCTION__, strerror(errno));
		close(fd);
		return -1;
	}

	yes = 1;
	if (setsockopt(fd, SOL_IP, IP_TRANSPARENT, &yes, sizeof(yes)) == -1) {
		fprintf(stderr, "%s: setsockopt IP_TRANSPARENT: %s", __FUNCTION__, strerror(errno));
		close(fd);
		return -1;
	}

	yes = 1;
	if (setsockopt(fd, SOL_IP, IP_RECVORIGDSTADDR, &yes, sizeof(yes)) == -1) {
		fprintf(stderr, "%s: setsockopt IP_RECVORIGDSTADDR: %s", __FUNCTION__, strerror(errno));
		close(fd);
		return -1;
	}

	unsigned long sockopt = 1;
	if (ioctl(fd, FIONBIO, &sockopt) < 0) {
		fprintf(stderr, "%s: ioctl(FIONBIO) failed for fd=%d\n", __FUNCTION__, fd);
		close(fd);
		return -1;
	}

	if (bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0) {
		fprintf(stderr, "%s: bind error('%s'), port=%d\n"
			, __FUNCTION__, strerror(errno), port);
		close(fd);
		return -1;
	}

	return fd;
}
#endif
int hp_net_socketpair(int mwfd[2])
{
	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, mwfd) < 0) {
		fprintf(stderr, "%s: socketpair failed, errno=%d, error='%s'\n",
			__FUNCTION__, errno, strerror(errno));
		return -1;
	}
	unsigned long sockopt = 1;
	if (ioctl(mwfd[0], FIONBIO, &sockopt) < 0) {
		fprintf(stderr, "%s: ioctl(FIONBIO) failed for fd=%d\n",
			__FUNCTION__, mwfd[0]);
		return -1;
	}
	sockopt = 1;
	if (ioctl(mwfd[1], FIONBIO, &sockopt) < 0) {
		fprintf(stderr, "%s: ioctl(FIONBIO) failed for fd=%d\n",
			__FUNCTION__, mwfd[1]);
		return -1;
	}
	return 0;
}

char * hp_get_ipport_cstr(int sockfd, char * buf)
{
	struct sockaddr_in cliaddr = { 0 };
	socklen_t len;
	getsockname(sockfd, (struct sockaddr *)&cliaddr, &len);
	if (!buf) {
		static char sbuf[64] = "ip:port";
		buf = sbuf;
	}
	char ip[32];
	inet_ntop(AF_INET, &cliaddr.sin_addr, ip, sizeof(ip));
	snprintf(buf, 64, "%s:%d", ip, ntohs(cliaddr.sin_port));
	return buf;
}

char * get_ipport_cstr2(struct sockaddr_in * addr, char const * sep, char * buf, int len)
{
	if (!(addr && sep && buf && len > 0))
		return 0;

	buf[0] = '\0';

	char ip[64];
	inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
	snprintf(buf, len, "%s%s%d", ip, sep, ntohs(addr->sin_port));

	return buf;
}

char * hp_net_get_ipport2(struct sockaddr_in * addr, char * ip, int iplen, int * port)
{
	if (!(addr && ip && port))
		return ip;

	ip[0] = '\0';

	inet_ntop(AF_INET, &addr->sin_addr, ip, iplen);
	*port = ntohs(addr->sin_port);

	return ip;
}

char * get_ipport(int sockfd, char * ip, int len, int * port)
{
	if (!(ip && port)) return ip;

	struct sockaddr_in cliaddr = { 0 };
	socklen_t slen;
	getsockname(sockfd, (struct sockaddr *)&cliaddr, &slen);
	inet_ntop(AF_INET, &cliaddr.sin_addr, ip, len);

	*port = ntohs(cliaddr.sin_port);

	return ip;
}

/*
 * @return: return the connected fd on success
 * (including errno == EINPROGRESS), -1 on error,
 *  */
hp_sock_t hp_net_connect(char const * ip, int port)
{
#if (defined LIBHP_WITH_WIN32_INTERROP) || (!defined _MSC_VER)
	struct sockaddr_in servaddr = { 0 };
	int fd = -1;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "%s: create socket failed(%s), ip='%s', port=%d.\n", __FUNCTION__, strerror(errno), ip, port);
		return -1;
	}
	if (inet_pton(AF_INET, ip, &servaddr.sin_addr) <= 0) {
		fprintf(stderr, "%s: inet_pton failed(%s), ip='%s', port=%d\n", __FUNCTION__, strerror(errno), ip, port);
		close(fd);
		return -1;
	}
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);

#if (!defined _MSC_VER) || (defined LIBHP_WITH_WIN32_INTERROP)
		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0)
#else
		u_long sockopt = 1;
		if (ioctlsocket(fd, FIONBIO, &sockopt) < 0)
#endif /* LIBHP_WITH_WIN32_INTERROP */
	{
		fprintf(stderr, "%s: ioctl(FIONBIO) failed for fd=%d\n", __FUNCTION__, fd);
		close(fd);
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr)) < 0) {
		if (errno != EINPROGRESS) {
			fprintf(stderr, "%s: connect error(%s), ip='%s', port=%d.\n", __FUNCTION__, strerror(errno), ip, port);
			close(fd);
			return -1;
		}
	}

	return fd;
#else
	struct sockaddr_in server_addr;

	SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	assert(sock != INVALID_SOCKET);
	unsigned long ul = 1;
	if (ioctlsocket(sock, FIONBIO, (unsigned long*)&ul) == SOCKET_ERROR)
		return 0;

	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port);
#ifdef __GNUC__
	server_addr.sin_addr.s_addr = inet_addr(ip);
#else
	inet_pton(AF_INET, ip, &server_addr.sin_addr);
#endif /* WIN32 */

	if (connect((SOCKET)sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR
		&& WSAGetLastError() != WSAEWOULDBLOCK) {
		char err[64];
		hp_log(stderr, "%s: connect failed, error=%d/'%s'\n", __FUNCTION__, WSAGetLastError(), err, sizeof(err));
		return 0;
	}

	return sock;

#endif /* LIBHP_WITH_WIN32_INTERROP */
	}

int hp_net_connect_addr(char const * addr)
{
	char buf[128] = "";
	strncpy(buf, addr, sizeof(buf));
	char * ip = buf, *p = strchr(buf, ':');
	int port = 0;
	if (p) {
		*p = '\0';
		port = atoi(p + 1);
	}
	return hp_net_connect(ip, port);
}

int fd_set_recvbuf(int fd, int * oldsz, int newsz)
{
	if (!(fd > 0)) return -1;
	if (!(newsz > 0)) return 0;

	int err;
	int len, *plen = &len;
	socklen_t nOptLen = sizeof(len);
	if (oldsz) plen = oldsz;

	err = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)plen, &nOptLen);
	if (!(err == 0)) {
		fprintf(stderr, "%s: getsockopt(SO_RCVBUF) failed, fd=%d, new=%d, errno=%d, error='%s'\n"
			, __FUNCTION__, fd, newsz, errno, strerror(errno));
		return -1;
	}

	err = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (char*)&newsz, nOptLen);
	if (!(err == 0)) {
		fprintf(stderr, "%s: setsockopt(SO_RCVBUF) failed, fd=%d, old=%d, new=%d, errno=%d, error='%s'\n"
			, __FUNCTION__, fd, len, newsz, errno, strerror(errno));
		return -1;
	}

	return 0;
}

int fd_set_sendbuf(int fd, int * oldsz, int newsz)
{
	if (!(fd > 0)) return -1;
	if (!(newsz > 0)) return 0;

	int err;
	int len, *plen = &len;
	socklen_t nOptLen = sizeof(len);
	if (oldsz) plen = oldsz;

	err = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)plen, &nOptLen);
	if (!(err == 0)) return -1;

	err = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&newsz, nOptLen);
	if (!(err == 0)) return -1;

	return 0;
}

static void before_readv(struct iovec * vec, int count,
	size_t nread, int * vec_n)
{
	size_t r = 0;
	int i = *vec_n;
	for (; i != count; ++i) {
		r += vec[i].iov_len;
		if (nread < r) {
			size_t left = r - nread;
			vec[i].iov_base = (char *)(vec[i].iov_base) + vec[i].iov_len - left;
			vec[i].iov_len = left;

			*vec_n = i;
			break;
		}
	}
}

ssize_t do_readv(int fd, struct iovec * vec, int count, size_t bytes)
{
	if (!vec) return -1;

	size_t nread = 0;
	int vec_n = 0;
	for (;;) {
		ssize_t r = readv(fd, vec + vec_n, count - vec_n);
		if (r < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;		/* and call read() again */
			else
				return(-1);
		}
		else if (r == 0)
			break;				/* EOF */

		nread += r;
		if (nread == bytes)
			return nread;

		before_readv(vec, count, r, &vec_n);
	}
	return nread;
}

size_t readv_a(int fd, int * err, struct iovec * vec, int count, int * n, size_t bytes)
{
	if (!(vec && n && err)) return 0;

	*n = 0;
	size_t nread = 0;
	for (;;) {
		ssize_t r = readv(fd, vec + *n, count - *n);
		if (r < 0) {
			/* and call read() again later */
			if (errno == EINTR || errno == EAGAIN)
				*err = EAGAIN;
			else
				*err = errno;
			break;
		}
		else if (r == 0) { /* EOF */
			*err = -1;
			break;
		}
		nread += r;
		if (nread == bytes) {
			*n = count;
			*err = 0;
			break;
		}

		before_readv(vec, count, r, n);
	}
	return nread;
}

size_t read_a(hp_sock_t fd, int * err, char * buf, size_t len, size_t bytes)
{
	if (!(fd >= 0 && err && buf && len > 0))
		return 0;

	size_t nread = 0;
	for (;;) {
		ssize_t r = read(fd, buf + nread, len - nread);
		if (r < 0) {
			/* and call read() again later */
			if (errno == EINTR || errno == EAGAIN)
				*err = EAGAIN;
			else
				*err = errno;
			break;
		}
		else if (r == 0) { /* EOF */
			*err = -1;
			break;
		}
		nread += r;
		if (nread == bytes) {
			*err = 0;
			break;
		}
	}
	return nread;
}
#if defined(_MSC_VER)
size_t read_a(hp_sock_t fd, int * err, char * buf, size_t len, size_t bytes)
{
	if(!(buf && err)) { return -1; }
	int nread = 0;

	*err = 0;
	for (;;) {
		int r = (int)recv(fd, buf + nread, len - nread, 0);
		if (SOCKET_ERROR == r) {
			*err = WSAGetLastError();
			if (*err == WSAEWOULDBLOCK) {
				*err = EAGAIN;
			}
			break;
		}
		else if (r == 0) { /* EOF */
			break;
		}
		nread += r;
		if (bytes > 0 && nread == bytes) {
			break;
		}
	}
	return nread;
}
#endif
/*
 * Write "n" bytes to a descriptor.
 * NOTE: from book <unpv13e>, sample url: https://github.com/k84d/unpv13e
 * */
ssize_t	writen(int fd, const char *vptr, size_t n)
{
	size_t		nleft;
	ssize_t		nwritten;
	const char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nwritten = write(fd, ptr, nleft)) <= 0) {
			if (nwritten < 0 && (errno == EINTR || errno == EAGAIN))
				nwritten = 0;		/* and call write() again */
			else
				return(-1);			/* error */
		}

		nleft -= nwritten;
		ptr += nwritten;
	}
	return(n);
}

/*
 * Read "n" bytes from a descriptor
 * NOTE: from book <unpv13e>, sample url: https://github.com/k84d/unpv13e
 * */
ssize_t	readn(int fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = (char *)vptr;
	nleft = n;
	while (nleft > 0) {
		if ((nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR || errno == EAGAIN)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		}
		else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

//#include <stdio.h>
//#include <string.h>
//#include <arpa/inet.h>
#include <netdb.h>
//#include <stdlib.h>

/* NOTE:
 * from http://blog.csdn.net/small_qch/article/details/16805857
*/
int get_ip_from_host(char *ipbuf, const char *host, int maxlen)
{
	struct sockaddr_in sa;
	sa.sin_family = AF_INET;
	/* TODO: inet_pton */
	if (inet_aton(host, &sa.sin_addr) == 0)
	{
		struct hostent *he;
		he = gethostbyname(host);
		if (he == NULL)
			return -1;
		memcpy(&sa.sin_addr, he->h_addr, sizeof(struct in_addr));
	}
	strncpy(ipbuf, inet_ntoa(sa.sin_addr), maxlen);
	return 0;
}

/*
 * @return: 0 on false; else true */
int netutil_same_subnet(int mask, char const * ip1, char const * ip2)
{
	if (!(ip1 && ip2))
		return 0;
	if (mask < 0) {
		char const * netmask = "255.255.255.0";
		struct sockaddr_in addr1 = { 0 }, addr2 = { 0 }, addr = { 0 };
		if (inet_pton(AF_INET, ip1, &addr1.sin_addr) <= 0 ||
			inet_pton(AF_INET, ip2, &addr2.sin_addr) <= 0 ||
			inet_pton(AF_INET, netmask, &addr.sin_addr) <= 0) {
			fprintf(stderr, "%s: inet_pton failed, error='%s', ip1='%s', ip2='%s', mask='%s'.\n"
				, __FUNCTION__, strerror(errno)
				, ip1, ip2, netmask);
			return 0;
		}
		return (addr1.sin_addr.s_addr & addr.sin_addr.s_addr) ==
			(addr2.sin_addr.s_addr & addr.sin_addr.s_addr) ? 1 : 0;
	}
	fprintf(stderr, "%s: TODO: no implementation yet!, set mask to -1 to use default subnet mask: '255.255.255.0'\n", __FUNCTION__);
	return 0;
}

/* same as @see netutil_same_subnet */
int netutil_same_subnet3(int mask, uint32_t ip1, char const * ip2)
{
	if (!(ip1 && ip2))
		return 0;
	if (mask < 0) {
		char const * netmask = "255.255.255.0";
		struct sockaddr_in addr2 = { 0 }, addr = { 0 };
		if (inet_pton(AF_INET, ip2, &addr2.sin_addr) <= 0 ||
			inet_pton(AF_INET, netmask, &addr.sin_addr) <= 0) {
			fprintf(stderr, "%s: inet_pton failed, error='%s', ip1='%u', ip2='%s', mask='%s'.\n"
				, __FUNCTION__, strerror(errno)
				, ip1, ip2, netmask);
			return 0;
		}
		return (ip1 & addr.sin_addr.s_addr) ==
			(addr2.sin_addr.s_addr & addr.sin_addr.s_addr) ? 1 : 0;
	}
	fprintf(stderr, "%s: TODO: no implementation yet!, set mask to -1 to use default subnet mask: '255.255.255.0'\n", __FUNCTION__);
	return 0;
}

/* same as @see netutil_same_subnet */
static int netutil_same_subnet2(int mask, char const * ip1, uint32_t ip2)
{
	if (mask < 0) {
		char const * netmask = "255.255.255.0";
		struct sockaddr_in addr1 = { 0 }, addr = { 0 };
		if (inet_pton(AF_INET, ip1, &addr1.sin_addr) <= 0 ||
			inet_pton(AF_INET, netmask, &addr.sin_addr) <= 0) {
			fprintf(stderr, "%s: inet_pton failed, error='%s', ip1='%s', ip2=%d, mask='%s'.\n"
				, __FUNCTION__, strerror(errno)
				, ip1, ip2, netmask);
			return 0;
		}
		return (addr1.sin_addr.s_addr & addr.sin_addr.s_addr) ==
			(ip2 & addr.sin_addr.s_addr) ? 1 : 0;
	}
	fprintf(stderr, "%s: TODO: no implementation yet!, set mask to -1 to use default subnet mask: '255.255.255.0'\n", __FUNCTION__);
	return 0;

}
/* @param ips: ':' seperated IPs, e.g. '172.28.0.170:172.19.255.95'
 * @return: 0 on false; else true */
int netutil_in_same_subnet(int mask, char const * ips, uint32_t ip)
{
	if (!(ips && ips[0] != '\0')) return 0;

	char buf[512];
	strncpy(buf, ips, sizeof(buf) - 2);
	char * ptr = strchr(buf, '\0');
	*ptr = ':';
	*(ptr + 1) = '\0';

	char * p = buf, *q = strchr(p, ':');
	for (; (q = strchr(p, ':')); p = q + 1) {
		if (p == q)
			continue;
		*q = '\0';
		if (netutil_same_subnet2(mask, p, ip))
			return 1;
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
#include "hp_log.h"   /* hp_log */
#include <string.h>   /* memset */
#include <sys/stat.h> /* stat */
#include <stdlib.h>   /* malloc */
#include <assert.h>

int test_hp_net_main(int argc, char ** argv)
{
	{
		struct sockaddr_in addr = { 0 };
		char ip[64];
		int port = 3;
		char * str = hp_net_get_ipport2(&addr, ip, sizeof(ip), &port);
		assert(str && str == ip && strcmp(ip, str) == 0 && strcmp(ip, "0.0.0.0") == 0);
		assert(port == 0);
	}
	{
		char buf[64];
		struct sockaddr_in addr = { 0 };
		inet_pton(AF_INET, "172.28.0.59", &addr.sin_addr);
		assert(strcmp(get_ipport_cstr2(&addr, ":", buf, sizeof(buf)), "172.28.0.59:0") == 0);
	}
	{
		char buf[64];
		struct sockaddr_in addr = { 0 };
		inet_pton(AF_INET, "172.28.0.59", &addr.sin_addr);
		assert(strcmp(get_ipport_cstr2(&addr, " ", buf, sizeof(buf)), "172.28.0.59 0") == 0);
	}
	{
		char buf[64];
		struct sockaddr_in addr = { 0 };
		inet_pton(AF_INET, "172.28.0.59", &addr.sin_addr);
		assert(strcmp(get_ipport_cstr2(&addr, "::", buf, sizeof(buf)), "172.28.0.59::0") == 0);
	}
	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "172.28.0.59", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95", addr.sin_addr.s_addr);
	assert(r); }

	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "172.19.255.88", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95", addr.sin_addr.s_addr);
	assert(r); }

	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "173.19.255.88", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95", addr.sin_addr.s_addr);
	assert(!r); }

	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "172.19.255.100", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95:192.168.1.140", addr.sin_addr.s_addr);
	assert(r); }

	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "192.168.1.1", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95:192.168.1.140", addr.sin_addr.s_addr);
	assert(r); }

	{struct sockaddr_in addr = { 0 };
	inet_pton(AF_INET, "191.168.1.1", &addr.sin_addr);
	int r = netutil_in_same_subnet(-1, "172.28.0.170:172.19.255.95:192.168.1.140", addr.sin_addr.s_addr);
	assert(!r); }

	int r = netutil_same_subnet(-1, "172.28.0.170", "172.28.0.4");
	assert(r);
	r = netutil_same_subnet(-1, "172.28.0.170", "195.28.0.4");
	assert(!r);
	{
		struct sockaddr_in	servaddr = { 0 };
		hp_log(stdout, "%s: sockaddr_in, ip_len=%zu, port_len=%zu\n", __FUNCTION__
			, sizeof(servaddr.sin_addr.s_addr), sizeof(servaddr.sin_port));
	}
	{
		struct iovec vec[2] = { {0, 0}, {0, 0} };
		FILE * f = fopen("test_writev_0_count", "w");
		if (f) {
			int fd = fileno(f);
			assert(fd > 0);

			int r = writev(fd, vec, 0);
			assert(r == 0);
			fclose(f);
			unlink("test_writev_0_count");
		}
	}
	int a = 0;
	if (a == 0) {
		hp_log(stdout, "%s:a==0\n", __FUNCTION__);
		a = 1;
	}
	else if (a == 1) {
		hp_log(stdout, "%s:a==1\n", __FUNCTION__);
	}
	return 0;

	char ipbuf[128];
	char const * host = "www.baidu.com";
	get_ip_from_host(ipbuf, host, 128);
	printf("%s: host='%s', ip: %s\n", __FUNCTION__, host, ipbuf);


	char buf[1024] = "GET / HTTP/1.1\r\n\r\n";
	int fd = hp_net_connect(ipbuf, 80);

	ssize_t nwrite = write(fd, buf, strlen(buf));
	if (nwrite <= 0) {
		return -1;
	}
	hp_log(stdout, "%s: sent http request '%s' to host '%s'\n"
		, __FUNCTION__, buf, host);

	ssize_t nread = read(fd, buf, sizeof(buf));
	if (nread > 0) {
		buf[nread] = '\0';
		hp_log(stdout, "%s: recv response from '%s', content='%p', len=%zu\n"
			, __FUNCTION__, host, buf, strlen(buf));
	}
	return 0;
}
#endif /* NDEBUG */
#endif /* _MSC_VER */

