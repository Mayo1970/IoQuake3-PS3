/*
 * ioquake3-PS3: net/if.h stub
 * PSL1GHT does not provide net/if.h. Provide minimal stubs for
 * the few things ioq3's net_ip.c uses.
 */
#ifndef PS3_NET_IF_H
#define PS3_NET_IF_H

/* Interface flags used by net_ip.c */
#define IFF_UP          0x1
#define IFF_LOOPBACK    0x8
#define IFF_MULTICAST   0x1000

/* PS3 doesn't have if_nametoindex; return 0 (any interface) */
static inline unsigned int if_nametoindex(const char *ifname)
{
    (void)ifname;
    return 0;
}

#endif /* PS3_NET_IF_H */
