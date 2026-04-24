#!/bin/bash
#
# apply_patches.sh -- Apply minimal patches to upstream ioq3 source
# for PS3 compilation.
#
# Patches are idempotent: running this script multiple times is safe.
#
# Usage: ./apply_patches.sh [path_to_ioq3]
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IOQ3_DIR="${1:-$SCRIPT_DIR/../ioq3}"

if [ ! -d "$IOQ3_DIR/code/qcommon" ]; then
    echo "ERROR: ioq3 source not found at $IOQ3_DIR"
    echo "Usage: $0 [path_to_ioq3]"
    exit 1
fi

echo "Patching ioq3 at: $IOQ3_DIR"

# ----------------------------------------------------------------
# 1. q_platform.h -- add __PS3__ block
# ----------------------------------------------------------------
echo "[1/11] Patching q_platform.h..."
python3 "$SCRIPT_DIR/patch_q_platform.py" "$IOQ3_DIR/code/qcommon/q_platform.h"

# ----------------------------------------------------------------
# 2. qcommon.h -- add #ifndef guards around constants we override
# ----------------------------------------------------------------
echo "[2/11] Patching qcommon.h..."
QCOMMON_H="$IOQ3_DIR/code/qcommon/qcommon.h"
if ! grep -q 'ifndef PACKET_BACKUP' "$QCOMMON_H" 2>/dev/null; then
    sed -i 's/^#define PACKET_BACKUP\b/#ifndef PACKET_BACKUP\n#define PACKET_BACKUP/' "$QCOMMON_H"
    sed -i '/^#define PACKET_BACKUP/{n;s/$/\n#endif/}' "$QCOMMON_H" 2>/dev/null || true

    sed -i 's/^#define MAX_RELIABLE_COMMANDS\b/#ifndef MAX_RELIABLE_COMMANDS\n#define MAX_RELIABLE_COMMANDS/' "$QCOMMON_H"
    sed -i '/^#define MAX_RELIABLE_COMMANDS/{n;s/$/\n#endif/}' "$QCOMMON_H" 2>/dev/null || true

    sed -i 's/^#define MAX_DOWNLOAD_WINDOW\b/#ifndef MAX_DOWNLOAD_WINDOW\n#define MAX_DOWNLOAD_WINDOW/' "$QCOMMON_H"
    sed -i '/^#define MAX_DOWNLOAD_WINDOW/{n;s/$/\n#endif/}' "$QCOMMON_H" 2>/dev/null || true
    echo "  Patched qcommon.h"
else
    echo "  qcommon.h already patched"
fi

# ----------------------------------------------------------------
# 3. snd_local.h -- add #ifndef guards for audio constants
# ----------------------------------------------------------------
echo "[3/11] Patching snd_local.h..."
SND_LOCAL_H="$IOQ3_DIR/code/client/snd_local.h"
if ! grep -q 'ifndef MAX_RAW_STREAMS' "$SND_LOCAL_H" 2>/dev/null; then
    sed -i 's/^#define MAX_RAW_STREAMS\b/#ifndef MAX_RAW_STREAMS\n#define MAX_RAW_STREAMS/' "$SND_LOCAL_H"
    sed -i '/^#define MAX_RAW_STREAMS/{n;s/$/\n#endif/}' "$SND_LOCAL_H" 2>/dev/null || true

    sed -i 's/^#define MAX_RAW_SAMPLES\b/#ifndef MAX_RAW_SAMPLES\n#define MAX_RAW_SAMPLES/' "$SND_LOCAL_H"
    sed -i '/^#define MAX_RAW_SAMPLES/{n;s/$/\n#endif/}' "$SND_LOCAL_H" 2>/dev/null || true
    echo "  Patched snd_local.h"
else
    echo "  snd_local.h already patched"
fi

# ----------------------------------------------------------------
# 4. snd_codec.c -- change #ifdef USE_CODEC_VORBIS to #if
# ----------------------------------------------------------------
echo "[4/11] Patching snd_codec.c..."
SND_CODEC_C="$IOQ3_DIR/code/client/snd_codec.c"
if grep -q '^#ifdef USE_CODEC_VORBIS' "$SND_CODEC_C" 2>/dev/null; then
    sed -i 's/^#ifdef USE_CODEC_VORBIS/#if USE_CODEC_VORBIS/' "$SND_CODEC_C"
    sed -i 's/^#ifdef USE_CODEC_OPUS/#if USE_CODEC_OPUS/' "$SND_CODEC_C"
    echo "  Patched snd_codec.c"
else
    echo "  snd_codec.c already patched"
fi

# ----------------------------------------------------------------
# 5. tr_main.c -- rename ri to avoid duplicate symbol
# ----------------------------------------------------------------
echo "[5/11] Patching tr_main.c..."
TR_MAIN_C="$IOQ3_DIR/code/renderergl1/tr_main.c"
if grep -q '^refimport_t[[:space:]]*ri;' "$TR_MAIN_C" 2>/dev/null; then
    sed -i 's/^refimport_t[[:space:]]*ri;/refimport_t tr_main_ri_unused;/' "$TR_MAIN_C"
    echo "  Patched tr_main.c"
else
    echo "  tr_main.c already patched (or ri not found)"
fi

# ----------------------------------------------------------------
# 6. tr_init.c -- rename GetRefAPI to avoid duplicate symbol
# ----------------------------------------------------------------
echo "[6/11] Patching tr_init.c..."
TR_INIT_C="$IOQ3_DIR/code/renderergl1/tr_init.c"
if grep -q '^refexport_t \*GetRefAPI' "$TR_INIT_C" 2>/dev/null; then
    sed -i 's/^refexport_t \*GetRefAPI/refexport_t *tr_init_GetRefAPI_unused/' "$TR_INIT_C"
    echo "  Patched tr_init.c"
else
    echo "  tr_init.c already patched (or GetRefAPI not found)"
fi

# ----------------------------------------------------------------
# 7-8. files.c -- PS3 short-read guard + FS_FreeFile diagnostic
# ----------------------------------------------------------------
echo "[7-8/11] Patching files.c..."
python3 "$SCRIPT_DIR/patch_files_c.py" "$IOQ3_DIR/code/qcommon/files.c"

# ----------------------------------------------------------------
# 9. tr_image_jpg.c -- zero-length guard + volatile fbuffer + two-level setjmp
# ----------------------------------------------------------------
echo "[9/11] Patching tr_image_jpg.c..."
python3 "$SCRIPT_DIR/patch_tr_image_jpg.py" "$IOQ3_DIR/code/renderercommon/tr_image_jpg.c"

# ----------------------------------------------------------------
# 10. net_ip.c -- skip FD_ISSET/select() on PS3 (PSL1GHT fd mismatch)
# ----------------------------------------------------------------
echo "[10/11] Patching net_ip.c (PS3 FD_ISSET/select fix)..."
python3 "$SCRIPT_DIR/patch_net_ip.py" "$IOQ3_DIR/code/qcommon/net_ip.c"

# ----------------------------------------------------------------
# 11. cl_cin.c -- re-enable cinematics (RE_StretchRaw now works)
# ----------------------------------------------------------------
echo "[11/11] Patching cl_cin.c (re-enable cinematics)..."
python3 "$SCRIPT_DIR/patch_cl_cin.py" "$IOQ3_DIR/code/client/cl_cin.c"

echo ""
echo "All patches applied. Run 'make' to build."
