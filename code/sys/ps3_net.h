/* ps3_net.h -- PSL1GHT socket API shim (netInitialize, IPv6 stubs). */

#ifndef PS3_NET_H
#define PS3_NET_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <net/net.h>
#include <net/socket.h>
#include <net/netctl.h>
#include <netinet/in.h>
#include <sys/select.h>

/* IPv6 stubs (AF_INET6 defined but non-functional; socket() fails at runtime). */

/* sockaddr_storage must match PSL1GHT's BSD-style layout (sa_len @0, sa_family u8 @1),
 * or code reading ss_family gets sa_len instead of the real address family. */
#ifndef _SS_SIZE
#define _SS_MAXSIZE 128
#define _SS_ALIGNSIZE (sizeof(int64_t))
#define _SS_PAD1SIZE (_SS_ALIGNSIZE - sizeof(u8) - sizeof(sa_family_t))
#define _SS_PAD2SIZE (_SS_MAXSIZE - sizeof(u8) - sizeof(sa_family_t) - _SS_PAD1SIZE - _SS_ALIGNSIZE)
struct sockaddr_storage {
    u8           ss_len;
    sa_family_t  ss_family;
    char         __ss_pad1[_SS_PAD1SIZE];
    int64_t      __ss_align;
    char         __ss_pad2[_SS_PAD2SIZE];
};
#define _SS_SIZE 1
#endif

/* in6_addr */
#ifndef PS3_IN6_ADDR_DEFINED
#define PS3_IN6_ADDR_DEFINED
struct in6_addr {
    unsigned char s6_addr[16];
};
static const struct in6_addr in6addr_any = {{0}};
#endif

/* sockaddr_in6 */
#ifndef PS3_SOCKADDR_IN6_DEFINED
#define PS3_SOCKADDR_IN6_DEFINED
struct sockaddr_in6 {
    sa_family_t     sin6_family;
    in_port_t       sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};
#endif

/* ipv6_mreq */
#ifndef PS3_IPV6_MREQ_DEFINED
#define PS3_IPV6_MREQ_DEFINED
struct ipv6_mreq {
    struct in6_addr ipv6mr_multiaddr;
    unsigned int    ipv6mr_interface;
};
#endif

/* IPv6 socket options (values don't matter; IPv6 sockets will fail) */
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6        41
#endif
#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF   17
#endif
#ifndef IPV6_JOIN_GROUP
#define IPV6_JOIN_GROUP     20
#endif
#ifndef IPV6_LEAVE_GROUP
#define IPV6_LEAVE_GROUP    21
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY         26
#endif

/* IPv6 address test macros (always false -- no real IPv6 on PS3) */
#ifndef IN6_IS_ADDR_MULTICAST
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0] == 0xff)
#endif
#ifndef IN6_IS_ADDR_UNSPECIFIED
#define IN6_IS_ADDR_UNSPECIFIED(a) \
    (((uint32_t *)(a))[0] == 0 && ((uint32_t *)(a))[1] == 0 && \
     ((uint32_t *)(a))[2] == 0 && ((uint32_t *)(a))[3] == 0)
#endif

/* gai_strerror stub */
static inline const char *gai_strerror(int errcode)
{
    (void)errcode;
    return "address resolution error";
}

/* gethostname stub */
static inline int gethostname(char *name, size_t len)
{
    if (name && len > 0) {
        strncpy(name, "ps3", len - 1);
        name[len - 1] = '\0';
    }
    return 0;
}

/* End IPv6 stubs */

/* Include PSL1GHT's netdb.h for struct addrinfo, AI_PASSIVE, etc. */
#include <netdb.h>

/* PSL1GHT's netdb.h gives struct addrinfo and AI_* defines but no
 * getaddrinfo/freeaddrinfo/getnameinfo — minimal implementations below fill the gap. */

/* Initialize PS3 networking; must run before any socket call. Returns 0 on success, negative on failure. */
static inline int PS3_Net_Init(void)
{
    int ret;

    ret = netInitialize();
    if (ret < 0) return ret;

    ret = netCtlInit();
    if (ret < 0) return ret;

    return 0;
}

static inline void PS3_Net_Shutdown(void)
{
    netCtlTerm();
    netDeinitialize();
}

/* getaddrinfo/freeaddrinfo/getnameinfo stubs.
 * PSL1GHT doesn't provide these; ioq3's net_ip.c needs them. */
#include <arpa/inet.h>

static inline int getaddrinfo(const char *node, const char *service,
                              const struct addrinfo *hints,
                              struct addrinfo **res)
{
    (void)hints;
    struct addrinfo *ai;
    struct sockaddr_in *sa;

    ai = (struct addrinfo *)calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
    if (!ai) return -1;

    sa = (struct sockaddr_in *)(ai + 1);

    sa->sin_family = AF_INET;
    if (service) {
        sa->sin_port = htons((unsigned short)atoi(service));
    }
    if (node) {
        /* Try numeric first */
        sa->sin_addr.s_addr = inet_addr(node);
        if (sa->sin_addr.s_addr == INADDR_NONE) {
            /* Try DNS via gethostbyname */
            struct hostent *he = gethostbyname(node);
            if (he && he->h_addrtype == AF_INET && he->h_length == 4) {
                memcpy(&sa->sin_addr, he->h_addr, 4);
            } else {
                free(ai);
                return -1;
            }
        }
    } else {
        sa->sin_addr.s_addr = INADDR_ANY;
    }

    ai->ai_family   = AF_INET;
    ai->ai_socktype  = SOCK_DGRAM;
    ai->ai_protocol  = 0;
    ai->ai_addrlen   = sizeof(struct sockaddr_in);
    ai->ai_addr      = (struct sockaddr *)sa;
    ai->ai_next      = NULL;

    *res = ai;
    return 0;
}

static inline void freeaddrinfo(struct addrinfo *res)
{
    free(res);
}

static inline int getnameinfo(const struct sockaddr *sa, unsigned int salen,
                              char *host, size_t hostlen,
                              char *serv, size_t servlen,
                              int flags)
{
    (void)salen; (void)flags;
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in *sin = (struct sockaddr_in *)sa;
        if (host && hostlen > 0) {
            char *ip = inet_ntoa(sin->sin_addr);
            strncpy(host, ip, hostlen - 1);
            host[hostlen - 1] = '\0';
        }
        if (serv && servlen > 0) {
            snprintf(serv, servlen, "%d", ntohs(sin->sin_port));
        }
    }
    return 0;
}

/* Stub for ifaddrs -- PS3 doesn't have getifaddrs */
#include "ifaddrs.h"

#endif /* PS3_NET_H */
