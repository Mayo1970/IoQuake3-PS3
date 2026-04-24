#!/bin/bash
#
# compile_shaders.sh -- Compile Cg shaders to binary .vpo/.fpo files,
# then generate C header with embedded byte arrays.
#
# Two-step process:
#   1. cgc.exe (NVIDIA Cg compiler, 32-bit native) compiles .vcg/.fcg -> assembly
#   2. cgcomp -a (PSL1GHT tool, assembly mode) converts assembly -> .vpo/.fpo
#
# This avoids the 64-bit cgcomp needing a 64-bit cg.dll (which NVIDIA
# no longer distributes).
#
# Requirements:
#   - cgcomp from PSL1GHT (ps3toolchain step 8)
#   - cgc.exe from NVIDIA Cg Toolkit 3.1 (32-bit)
#
# Output: ../ps3gl_shader_data.h (included by ps3gl_shaders.c)

set -e

# ---------- Find cgcomp ----------
CGCOMP="${PS3DEV}/bin/cgcomp"
if [ ! -x "$CGCOMP" ]; then
    CGCOMP="${PS3DEV}/ppu/bin/cgcomp"
fi
if [ ! -x "$CGCOMP" ]; then
    CGCOMP=$(which cgcomp 2>/dev/null || true)
fi

# ---------- Find cgc.exe ----------
# Check common locations for NVIDIA Cg Toolkit
CGC=""
for candidate in \
    "/c/Program Files (x86)/NVIDIA Corporation/Cg/bin/cgc.exe" \
    "/c/Program Files/NVIDIA Corporation/Cg/bin/cgc.exe" \
    "/e/Users/Matteo/Desktop/quake3/DEVkits/Cg64/bin/cgc.exe" \
    "$(which cgc.exe 2>/dev/null || true)" \
    "$(which cgc 2>/dev/null || true)"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        CGC="$candidate"
        break
    fi
done

if [ -z "$CGCOMP" ] || [ ! -x "$CGCOMP" ]; then
    echo "ERROR: cgcomp not found. Build ps3toolchain first."
    echo "Generating stub shader data header..."
    cat > ../ps3gl_shader_data.h <<'STUBEOF'
/*
 * ps3gl_shader_data.h -- STUB: no compiled shaders available.
 * Regenerate with: cd shaders && ./compile_shaders.sh
 */
#ifndef PS3GL_SHADER_DATA_H
#define PS3GL_SHADER_DATA_H

#define PS3GL_SHADERS_AVAILABLE 0

#endif /* PS3GL_SHADER_DATA_H */
STUBEOF
    echo "Wrote stub ps3gl_shader_data.h"
    exit 0
fi

if [ -z "$CGC" ]; then
    echo "ERROR: cgc.exe not found."
    echo "Install NVIDIA Cg Toolkit 3.1 or set CGC= environment variable."
    exit 1
fi

echo "Using cgcomp: $CGCOMP"
echo "Using cgc:    $CGC"

# ---------- Compile shaders: cgc -> assembly -> cgcomp -a -> binary ----------

compile_vp() {
    local src="$1"
    local out="$2"
    local asm="${out}.asm"

    echo "  cgc -profile vp40: $src -> $asm"
    "$CGC" -profile vp40 -o "$asm" "$src"

    echo "  cgcomp -a -v: $asm -> $out"
    "$CGCOMP" -a -v "$asm" "$out"

    rm -f "$asm"
}

compile_fp() {
    local src="$1"
    local out="$2"
    local asm="${out}.asm"

    echo "  cgc -profile fp40: $src -> $asm"
    "$CGC" -profile fp40 -o "$asm" "$src"

    echo "  cgcomp -a -f: $asm -> $out"
    "$CGCOMP" -a -f "$asm" "$out"

    rm -f "$asm"
}

echo ""
echo "Compiling vertex program..."
compile_vp q3_vp.vcg q3_vp.vpo

echo ""
echo "Compiling fragment programs..."
for fp in q3_fp_coloronly q3_fp_modulate q3_fp_replace q3_fp_decal q3_fp_add q3_fp_blend; do
    compile_fp ${fp}.fcg ${fp}.fpo
done

# ---------- Generate C header with embedded binary data ----------
HEADER="../ps3gl_shader_data.h"
echo ""
echo "Generating $HEADER..."

cat > "$HEADER" <<'EOF'
/*
 * ps3gl_shader_data.h -- Auto-generated embedded shader binaries.
 * Do not edit manually. Regenerate with: cd shaders && ./compile_shaders.sh
 */
#ifndef PS3GL_SHADER_DATA_H
#define PS3GL_SHADER_DATA_H

#define PS3GL_SHADERS_AVAILABLE 1

EOF

bin_to_c_array() {
    local file="$1"
    local name="$2"
    local size=$(wc -c < "$file")

    echo "static const unsigned char ${name}[] __attribute__((aligned(16))) = {" >> "$HEADER"
    python3 -c "
import sys
data = open(sys.argv[1], 'rb').read()
for i, b in enumerate(data):
    if i > 0:
        sys.stdout.write(',')
    if i % 12 == 0:
        sys.stdout.write('\n  ')
    else:
        sys.stdout.write(' ')
    sys.stdout.write('0x%02x' % b)
sys.stdout.write('\n')
" "$file" >> "$HEADER"
    echo "};" >> "$HEADER"
    echo "static const unsigned int ${name}_size = ${size};" >> "$HEADER"
    echo "" >> "$HEADER"
}

bin_to_c_array q3_vp.vpo            shader_vp_data
bin_to_c_array q3_fp_coloronly.fpo   shader_fp_coloronly_data
bin_to_c_array q3_fp_modulate.fpo    shader_fp_modulate_data
bin_to_c_array q3_fp_replace.fpo     shader_fp_replace_data
bin_to_c_array q3_fp_decal.fpo       shader_fp_decal_data
bin_to_c_array q3_fp_add.fpo         shader_fp_add_data
bin_to_c_array q3_fp_blend.fpo       shader_fp_blend_data

echo "#endif /* PS3GL_SHADER_DATA_H */" >> "$HEADER"

echo ""
echo "Done. Generated $HEADER"
echo "Shader binaries:"
ls -la *.vpo *.fpo 2>/dev/null
