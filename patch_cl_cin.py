#!/usr/bin/env python3
"""
patch_cl_cin.py -- Patch cl_cin.c for PS3 cinematic support.

Previously, system cinematics (intro logo) were blocked because
RE_StretchRaw wasn't functional. Now that the GL translation layer
handles qglTexImage2D, qglTexSubImage2D, and immediate mode
(qglBegin/qglEnd), cinematics should work.

This patch REMOVES the old PS3 cinematic blocks if present.
Idempotent: safe to run multiple times.
"""
import sys
import os

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()

    modified = False

    # --- Remove block 1: early return for CIN_system ---
    old_block1 = ('#ifdef __PS3__\n'
                  '\t/* PS3: block game-state-altering cinematics (the intro logo).\n'
                  '\t * RE_StretchRaw is not yet functional. Non-system cinematics\n'
                  '\t * (menu background videos) are allowed since they use a\n'
                  '\t * different rendering path. */\n'
                  '\tif (systemBits & CIN_system) {\n'
                  '\t\tCom_DPrintf("CIN_PlayCinematic: skipping system cinematic %s on PS3\\n", arg);\n'
                  '\t\treturn -1;\n'
                  '\t}\n'
                  '#endif\n')

    if old_block1 in content:
        content = content.replace(old_block1, '', 1)
        modified = True
        print("  Removed PS3 cinematic early-return block")
    else:
        print("  PS3 cinematic early-return block not found (already removed)")

    # --- Remove block 2: clc.state = CA_CINEMATIC guard ---
    old_block2 = ('#ifndef __PS3__\n'
                  '\t\t\tclc.state = CA_CINEMATIC;\n'
                  '#else\n'
                  '\t\t\t/* PS3: skip cinematic state -- RE_StretchRaw (qglTexImage2D +\n'
                  '\t\t\t * qglBegin/qglEnd immediate mode) is not yet functional in\n'
                  '\t\t\t * the GL-to-RSX translation layer. Stay in CA_DISCONNECTED\n'
                  '\t\t\t * so the normal menu rendering path is used instead. */\n'
                  '\t\t\tcinTable[currentHandle].status = FMV_EOF;\n'
                  '#endif')
    new_block2 = '\t\t\tclc.state = CA_CINEMATIC;'

    if old_block2 in content:
        content = content.replace(old_block2, new_block2, 1)
        modified = True
        print("  Removed PS3 CA_CINEMATIC guard")
    else:
        print("  PS3 CA_CINEMATIC guard not found (already removed)")

    if modified:
        with open(filepath, 'w') as f:
            f.write(content)
        print(f"  Patched {os.path.basename(filepath)}")
    else:
        print(f"  No changes needed")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_cl_cin.c>")
        sys.exit(1)
    patch_file(sys.argv[1])
