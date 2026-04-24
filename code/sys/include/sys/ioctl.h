/*
 * ioquake3-PS3: sys/ioctl.h stub
 * PSL1GHT does not provide ioctl(). Map FIONBIO to setsockopt SO_NBIO.
 */
#ifndef PS3_SYS_IOCTL_H
#define PS3_SYS_IOCTL_H

#include <net/socket.h>

#ifndef FIONBIO
#define FIONBIO 0x5421  /* value doesn't matter; we intercept in ioctl() */
#endif

/* ioq3 uses: ioctl(fd, FIONBIO, &_true)
 * Map to PSL1GHT's setsockopt(fd, SOL_SOCKET, SO_NBIO, ...) */
static inline int ioctl(int fd, unsigned long request, void *arg)
{
    if (request == FIONBIO) {
        int val = arg ? *(int *)arg : 0;
        return setsockopt(fd, SOL_SOCKET, SO_NBIO, &val, sizeof(val));
    }
    return -1;
}

#endif /* PS3_SYS_IOCTL_H */
