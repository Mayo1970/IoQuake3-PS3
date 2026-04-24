#!/usr/bin/env python3
"""
patch_net_ip.py -- Patch net_ip.c for PS3 compatibility.

Problem 1 (FD_ISSET): PSL1GHT socket fds have bit 30 set
(SOCKET_FD_MASK = 0x40000000), making fd values ~1 billion.
Newlib's FD_ISSET indexes fds_bits[fd/64] = fds_bits[16M] -- 128 MB OOB.

Fix: Skip FD_ISSET when fdr==NULL. NET_Sleep passes NULL on PS3.

Problem 2 (sin_len): PSL1GHT uses BSD-style sockaddr_in with a sin_len
field at offset 0. Standard POSIX doesn't have this. ioq3's
NetadrToSockadr never sets sin_len, leaving it 0. The PS3 kernel
(FreeBSD-derived) needs sin_len for routing, especially for broadcast.
sendto() returns ENOENT (2) when sin_len is 0.

Fix: Set sin_len = sizeof(struct sockaddr_in) in NetadrToSockadr and
NET_IPSocket's bind address on PS3.

Idempotent: safe to run multiple times.
"""
import sys
import os

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    modified = False

    # --- Patch 1: NET_GetPacket -- skip FD_ISSET on PS3 ---
    # Change: if(ip_socket != INVALID_SOCKET && FD_ISSET(ip_socket, fdr))
    # To:     if(ip_socket != INVALID_SOCKET && (!fdr || FD_ISSET(ip_socket, fdr)))
    # Same for ip6_socket and multicast6_socket

    marker = '/* __PS3__: skip FD_ISSET */'
    if marker not in content:
        # Patch ip_socket check
        old = 'if(ip_socket != INVALID_SOCKET && FD_ISSET(ip_socket, fdr))'
        new = 'if(ip_socket != INVALID_SOCKET && (!fdr || FD_ISSET(ip_socket, fdr))) ' + marker
        if old in content:
            content = content.replace(old, new, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find ip_socket FD_ISSET pattern")

        # Patch ip6_socket check
        old = 'if(ip6_socket != INVALID_SOCKET && FD_ISSET(ip6_socket, fdr))'
        new = 'if(ip6_socket != INVALID_SOCKET && (!fdr || FD_ISSET(ip6_socket, fdr))) ' + marker
        if old in content:
            content = content.replace(old, new, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find ip6_socket FD_ISSET pattern")

        # Patch multicast6_socket check
        old = 'if(multicast6_socket != INVALID_SOCKET && multicast6_socket != ip6_socket && FD_ISSET(multicast6_socket, fdr))'
        new = 'if(multicast6_socket != INVALID_SOCKET && multicast6_socket != ip6_socket && (!fdr || FD_ISSET(multicast6_socket, fdr))) ' + marker
        if old in content:
            content = content.replace(old, new, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find multicast6_socket FD_ISSET pattern")

    else:
        print(f"  NET_GetPacket FD_ISSET already patched")

    # --- Patch 2: NET_Sleep -- skip select() on PS3, just call NET_Event(NULL) ---
    marker2 = '/* __PS3__: skip select */'
    if marker2 not in content:
        # Find the NET_Sleep function and add PS3 early-out at the start of the body
        target = 'void NET_Sleep(int msec)\n{\n'
        ps3_block = ('void NET_Sleep(int msec)\n{\n'
                     '#ifdef __PS3__\n'
                     '\t' + marker2 + '\n'
                     '\tNET_Event(NULL);\n'
                     '\tif(msec > 0) {\n'
                     '\t\tif(msec > 16) msec = 16;\n'
                     '\t\tusleep(msec * 1000);\n'
                     '\t}\n'
                     '\treturn;\n'
                     '#endif\n')
        if target in content:
            content = content.replace(target, ps3_block, 1)
            modified = True
        else:
            # Try with tabs
            target2 = 'void NET_Sleep(int msec)\n{\n\tstruct timeval timeout;'
            ps3_block2 = ('void NET_Sleep(int msec)\n{\n'
                         '#ifdef __PS3__\n'
                         '\t' + marker2 + '\n'
                         '\tNET_Event(NULL);\n'
                         '\tif(msec > 0) {\n'
                         '\t\tif(msec > 16) msec = 16;\n'
                         '\t\tusleep(msec * 1000);\n'
                         '\t}\n'
                         '\treturn;\n'
                         '#endif\n'
                         '\tstruct timeval timeout;')
            if target2 in content:
                content = content.replace(target2, ps3_block2, 1)
                modified = True
            else:
                print(f"  WARNING: Could not find NET_Sleep function body")
    else:
        print(f"  NET_Sleep already patched")

    # --- Patch 3: NetadrToSockadr -- set sin_len on PS3 (BSD-style sockaddr) ---
    marker3 = '/* __PS3__: set sin_len */'
    if marker3 not in content:
        # Patch NA_BROADCAST case
        old_bc = ('\tif( a->type == NA_BROADCAST ) {\n'
                  '\t\t((struct sockaddr_in *)s)->sin_family = AF_INET;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_port = a->port;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_addr.s_addr = INADDR_BROADCAST;')
        new_bc = ('\tif( a->type == NA_BROADCAST ) {\n'
                  '#ifdef __PS3__\n'
                  '\t\t((struct sockaddr_in *)s)->sin_len = sizeof(struct sockaddr_in); ' + marker3 + '\n'
                  '#endif\n'
                  '\t\t((struct sockaddr_in *)s)->sin_family = AF_INET;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_port = a->port;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_addr.s_addr = INADDR_BROADCAST;')
        if old_bc in content:
            content = content.replace(old_bc, new_bc, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find NetadrToSockadr NA_BROADCAST pattern")

        # Patch NA_IP case
        old_ip = ('\telse if( a->type == NA_IP ) {\n'
                  '\t\t((struct sockaddr_in *)s)->sin_family = AF_INET;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_addr.s_addr = *(int *)&a->ip;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_port = a->port;')
        new_ip = ('\telse if( a->type == NA_IP ) {\n'
                  '#ifdef __PS3__\n'
                  '\t\t((struct sockaddr_in *)s)->sin_len = sizeof(struct sockaddr_in); ' + marker3 + '\n'
                  '#endif\n'
                  '\t\t((struct sockaddr_in *)s)->sin_family = AF_INET;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_addr.s_addr = *(int *)&a->ip;\n'
                  '\t\t((struct sockaddr_in *)s)->sin_port = a->port;')
        if old_ip in content:
            content = content.replace(old_ip, new_ip, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find NetadrToSockadr NA_IP pattern")
    else:
        print(f"  NetadrToSockadr sin_len already patched")

    # --- Patch 4: NET_IPSocket bind -- set sin_len on PS3 ---
    marker4 = '/* __PS3__: set bind sin_len */'
    if marker4 not in content:
        old_bind = '\tif( bind( newsocket, (void *)&address, sizeof(address) ) == SOCKET_ERROR ) {\n\t\tCom_Printf( "WARNING: NET_IPSocket: bind: %s\\n", NET_ErrorString() );'
        new_bind = ('#ifdef __PS3__\n'
                    '\taddress.sin_len = sizeof(address); ' + marker4 + '\n'
                    '#endif\n'
                    '\tif( bind( newsocket, (void *)&address, sizeof(address) ) == SOCKET_ERROR ) {\n'
                    '\t\tCom_Printf( "WARNING: NET_IPSocket: bind: %s\\n", NET_ErrorString() );')
        if old_bind in content:
            content = content.replace(old_bind, new_bind, 1)
            modified = True
        else:
            print(f"  WARNING: Could not find NET_IPSocket bind pattern")
    else:
        print(f"  NET_IPSocket bind sin_len already patched")

    if modified:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"  Patched {os.path.basename(filepath)}")
    else:
        print(f"  No changes needed")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_net_ip.c>")
        sys.exit(1)
    patch_file(sys.argv[1])
