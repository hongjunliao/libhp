/*!
 * This file is PART of libhp project
 * @author hongjun.liao <docici@126.com>, @date 2017/11/24
 *
 * net/socket, none-block fds only
 * */

#ifndef LIBHP_NET_H__
#define LIBHP_NET_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#if !defined(_MSC_VER)  && !defined(_WIN32)
#include "Win32_Interop.h"
#include <netinet/in.h>	/* sockaddr_in */
#include <stdlib.h>     /* uint32_t */
#include "hp_sock_t.h"  /* hp_sock_t */
 /////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#define new __new
#endif

#if !defined(_MSC_VER)  && !defined(_WIN32)
ssize_t hp_net_sendto(int fd, char const * ip, int port, char const * buf, size_t len);
ssize_t hp_net_sendmsg(int fd, struct sockaddr_in * servaddr, socklen_t len, struct iovec * iov, size_t iovlen);
ssize_t hp_net_sendmsg1(struct sockaddr_in * origaddr, socklen_t olen
		, struct sockaddr_in * servaddr, socklen_t slen
		, struct iovec * iov, size_t iovlen);
ssize_t
hp_net_recvmsg(int fd, void *ptr, size_t nbytes, int *flagsp,
			   struct sockaddr_in *sa, socklen_t *salenptr, struct sockaddr_in * origdst);
hp_sock_t hp_net_listen(int port);
hp_sock_t hp_net_connect(char const * ip, int port);
int hp_net_connect_addr(char const * addr);
hp_sock_t hp_net_connect_addr2( struct sockaddr_in  servaddr);

int hp_net_set_alive(hp_sock_t fd, int interval);
int hp_net_udp_bind(char const * ip, int port);

int hp_net_socketpair(int mwfd[2]);

char * hp_get_ipport_cstr(int sockfd, char * buf);
char * get_ipport_cstr2(struct sockaddr_in * addr, char const * sep, char * buf, int len);
char * hp_net_get_ipport2(struct sockaddr_in * addr, char * ip, int iplen, int * port);

int netutil_same_subnet(int mask, char const * ip1, char const * ip2);
int netutil_same_subnet3(int mask, uint32_t ip1, char const * ip2);
int netutil_in_same_subnet(int mask, char const * ips, uint32_t ip);

ssize_t
hp_net_recvfrom_flags(int fd, void *ptr, size_t nbytes, int *flagsp,
			   struct sockaddr_in *sa, socklen_t *salenptr, struct sockaddr_in * origdst);
#endif /* _MSC_VER */
size_t read_a(hp_sock_t fd, int * err, char * buf, size_t len, size_t bytes);

#ifndef NDEBUG
int test_hp_net_main(int argc, char ** argv);
#endif /* NDEBUG */

#ifdef __cplusplus
}
#endif

#endif /*_MSC_VER*/

#endif /* LIBHP_NET_H__ */
