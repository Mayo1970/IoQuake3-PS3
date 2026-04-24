#!/usr/bin/env python3
"""
patch_q_platform.py -- Inject a __PS3__ block into ioQ3's q_platform.h.

Inserts the PS3 platform block right before the Emscripten section
(or before the Q3VM section if Emscripten is not present).

Idempotent: does nothing if the __PS3__ block already exists.
"""

import sys
import os

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    # Already patched?
    if '__PS3__' in content:
        print(f"[patch] {filepath} already contains __PS3__ block, skipping")
        return False

    ps3_block = '''
//================================================================== PS3 ===

#if defined(__PS3__) || defined(__lv2ppu__) || defined(__PPU__)

#define OS_STRING "ps3"
#define ID_INLINE inline
#define PATH_SEP '/'

#define ARCH_STRING "cell"

#define Q3_BIG_ENDIAN

#define DLL_EXT ".sprx"

#define IOAPI_NO_64BIT

#endif

'''

    # Find insertion point: before Emscripten section or Q3VM section
    markers = [
        '//============================================================ EMSCRIPTEN ===',
        '//================================================================== Q3VM ===',
        '#ifdef Q3_VM',
    ]

    insert_pos = -1
    for marker in markers:
        pos = content.find(marker)
        if pos != -1:
            insert_pos = pos
            break

    if insert_pos == -1:
        # Fallback: insert before the "Catch missing defines" section
        marker = '// Catch missing defines'
        insert_pos = content.find(marker)

    if insert_pos == -1:
        print(f"[patch] ERROR: Could not find insertion point in {filepath}")
        return False

    content = content[:insert_pos] + ps3_block + content[insert_pos:]

    with open(filepath, 'w') as f:
        f.write(content)

    print(f"[patch] Injected __PS3__ block into {filepath}")
    return True


if __name__ == '__main__':
    if len(sys.argv) > 1:
        filepath = sys.argv[1]
    else:
        # Default: assume ioq3 is sibling directory
        script_dir = os.path.dirname(os.path.abspath(__file__))
        filepath = os.path.join(script_dir, '..', 'ioq3', 'code', 'qcommon', 'q_platform.h')

    if not os.path.exists(filepath):
        print(f"[patch] ERROR: {filepath} not found")
        sys.exit(1)

    patch_file(filepath)
