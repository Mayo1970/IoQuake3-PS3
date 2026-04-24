#!/usr/bin/env python3
"""
Patch ioq3's code/qcommon/files.c for PS3:
  1. Extend the GEKKO short-read guard in FS_ReadFileDir to also cover __PS3__
  2. Add diagnostic log before FS_FreeFile(NULL) fatal error

Idempotent: safe to run multiple times.
"""
import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: patch_files_c.py <path/to/files.c>", file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    if not os.path.isfile(path):
        print("ERROR: %s not found" % path, file=sys.stderr)
        sys.exit(1)

    with open(path, "r") as f:
        content = f.read()

    changed = False

    # --- Patch 1: Short-read guard ---
    # The GEKKO block in FS_ReadFileDir looks like:
    #   #ifdef GEKKO
    #       if (_readRet != len) {
    #           extern void wii_diag(...);
    #           wii_diag("FS_ReadFileDir: ...");
    #           ...
    #       }
    #   #endif
    # Change to: #if defined(GEKKO) || defined(__PS3__)
    # and replace wii_diag with Com_Printf inside this block.

    if "__PS3__" not in content:
        # Find the FS_ReadFileDir GEKKO block specifically
        # It's the one containing "FS_ReadFileDir: read"
        marker = '#ifdef GEKKO\n\t\tif (_readRet != len) {'
        if marker in content:
            # Replace the ifdef
            content = content.replace(
                marker,
                '#if defined(GEKKO) || defined(__PS3__)\n\t\tif (_readRet != len) {'
            )

            # Also handle the other GEKKO blocks in files.c that have
            # short-read or error guards we want on PS3:

            # FS_Read unzRead guard
            marker2 = '#ifdef GEKKO\n\t\t\textern void wii_diag(const char *, ...);\n\t\t\twii_diag("FS_Read:'
            if marker2 in content:
                content = content.replace(
                    marker2,
                    '#if defined(GEKKO) || defined(__PS3__)\n\t\t\t// diag forward\n\t\t\tCom_Printf("FS_Read:'
                )

            # unzOpenCurrentFile guard
            marker3 = '#ifdef GEKKO\n\t\t\t\t\t\textern void wii_diag(const char *, ...);\n\t\t\t\t\t\twii_diag("FS: unzOpenCurrentFile'
            if marker3 in content:
                content = content.replace(
                    marker3,
                    '#if defined(GEKKO) || defined(__PS3__)\n\t\t\t\t\t\t// diag forward\n\t\t\t\t\t\tCom_Printf("FS: unzOpenCurrentFile'
                )

            # FS_ReadFileDir block -- replace wii_diag with Com_Printf
            content = content.replace(
                'extern void wii_diag(const char *, ...);\n\t\t\twii_diag("FS_ReadFileDir:',
                '// diag forward\n\t\t\tCom_Printf("FS_ReadFileDir:'
            )

            changed = True
            print("  Applied short-read guard for __PS3__")
        else:
            print("  WARNING: Could not find FS_ReadFileDir GEKKO block")
    else:
        print("  Short-read guard already applied")

    # --- Patch 2: FS_FreeFile diagnostic ---
    diag_marker = "PS3_FREEFILENULL_DIAG"
    if diag_marker not in content:
        old = '\t\tCom_Error( ERR_FATAL, "FS_FreeFile( NULL )" );'
        new = ('\t\tCom_Printf("PS3_FREEFILENULL_DIAG: FS_FreeFile called with NULL, '
               'fs_loadStack=%d\\n", fs_loadStack); /* PS3_FREEFILENULL_DIAG */\n'
               '\t\tCom_Error( ERR_FATAL, "FS_FreeFile( NULL )" );')
        if old in content:
            content = content.replace(old, new)
            changed = True
            print("  Added FS_FreeFile(NULL) diagnostic")
        else:
            print("  WARNING: Could not find FS_FreeFile fatal line")
    else:
        print("  FS_FreeFile diagnostic already present")

    if changed:
        with open(path, "w") as f:
            f.write(content)
        print("  files.c patched successfully")
    else:
        print("  No changes needed")

if __name__ == "__main__":
    main()
