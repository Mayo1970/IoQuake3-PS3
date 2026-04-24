/*
 * Stub ifaddrs.h for ioquake3-PS3.
 * PS3 has no getifaddrs. Provide stubs so net_ip.c compiles.
 */
#ifndef _IFADDRS_H_STUB
#define _IFADDRS_H_STUB

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_broadaddr;
    void            *ifa_data;
};

static inline int getifaddrs(struct ifaddrs **ifap) {
    (void)ifap;
    return -1; /* not supported */
}

static inline void freeifaddrs(struct ifaddrs *ifa) {
    (void)ifa;
}

#endif /* _IFADDRS_H_STUB */
