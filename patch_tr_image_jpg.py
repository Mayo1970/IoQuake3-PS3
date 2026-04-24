#!/usr/bin/env python3
"""
Patch ioq3's code/renderercommon/tr_image_jpg.c for PS3:

On PS3/PPC64, longjmp corrupts the stack frame of the function containing
setjmp. Even an noinline helper crashes during its epilogue because GCC
restores callee-saved registers from the (corrupted) stack, not from jmp_buf.

Fix: Two-level setjmp scheme.
  - R_LoadJPG sets up an "escape" jmp_buf before calling anything
  - An noinline helper (R_LoadJPG_Decompress) does all libjpeg work
  - If libjpeg errors: libjpeg longjmps to the helper's local setjmp,
    then the helper immediately longjmps AGAIN to R_LoadJPG's escape
    jmp_buf -- bypassing the helper's corrupted epilogue entirely
  - R_LoadJPG's locals were set before its setjmp, so they are valid
    after longjmp per C99 7.13.2.1

Additional PS3 changes:
  - JPEG SOI magic byte pre-check (0xFF 0xD8) to skip non-JPEG files
  - len <= 0 guard (pk3s can contain zero-length entries)

The non-PS3 code path is preserved exactly as upstream.

Idempotent: safe to run multiple times.
"""
import sys
import os

def main():
    if len(sys.argv) < 2:
        print("Usage: patch_tr_image_jpg.py <path/to/tr_image_jpg.c>",
              file=sys.stderr)
        sys.exit(1)

    path = sys.argv[1]
    if not os.path.isfile(path):
        print("ERROR: %s not found" % path, file=sys.stderr)
        sys.exit(1)

    with open(path, "r") as f:
        content = f.read()

    # Idempotency check: if our helper is already present, nothing to do
    if "R_LoadJPG_Decompress" in content:
        print("  tr_image_jpg.c already patched (noinline helper present)")
        return

    changed = False

    # The complete PS3 helper function (inserted before R_LoadJPG)
    PS3_HELPER = '''
#ifdef __PS3__
/*
 * PS3/PPC64: Two-level setjmp scheme to survive libjpeg errors.
 *
 * Problem: longjmp on PPC64 GCC corrupts the stack frame of the function
 * containing setjmp. Even an noinline helper crashes during its epilogue
 * because the compiler restores callee-saved registers from the (corrupted)
 * stack frame, not from the jmp_buf.
 *
 * Solution: R_LoadJPG sets up an "escape" jmp_buf. The helper receives a
 * pointer to it. When libjpeg errors, libjpeg longjmps to the helper's
 * local setjmp. The helper's error handler then longjmps AGAIN to
 * R_LoadJPG's escape jmp_buf -- bypassing the helper's corrupted epilogue
 * entirely. R_LoadJPG's setjmp was established before any libjpeg calls,
 * so its stack frame is pristine.
 */
static jmp_buf *ps3_jpg_escape;  /* pointer to R_LoadJPG's escape jmp_buf */

static void __attribute__((noinline)) R_LoadJPG_Decompress(
    const unsigned char *inbuf, int inlen,
    byte **out_pixels, int *out_w, int *out_h)
{
  struct jpeg_decompress_struct cinfo;
  q_jpeg_error_mgr_t jerr;
  JSAMPARRAY buffer;
  unsigned int row_stride;
  unsigned int pixelcount, memcount;
  unsigned int sindex, dindex;
  byte *out;
  byte *buf;

  Com_Memset(&cinfo, 0, sizeof(cinfo));

  cinfo.err = jpeg_std_error(&jerr.pub);
  cinfo.err->error_exit = R_JPGErrorExit;
  cinfo.err->output_message = R_JPGOutputMessage;

  if (setjmp(jerr.setjmp_buffer))
  {
    /* libjpeg signaled an error. This function's stack is corrupted.
     * Do NOT return -- epilogue would read garbage from stack.
     * Instead, longjmp to R_LoadJPG's escape point. */
    longjmp(*ps3_jpg_escape, 1);
    /* NOTREACHED */
  }

  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, (unsigned char *)inbuf, inlen);
  (void) jpeg_read_header(&cinfo, TRUE);

  cinfo.out_color_space = JCS_RGB;
  (void) jpeg_start_decompress(&cinfo);

  pixelcount = cinfo.output_width * cinfo.output_height;

  if(!cinfo.output_width || !cinfo.output_height
      || ((pixelcount * 4) / cinfo.output_width) / 4 != cinfo.output_height
      || pixelcount > 0x1FFFFFFF || cinfo.output_components != 3
    )
  {
    jpeg_destroy_decompress(&cinfo);
    longjmp(*ps3_jpg_escape, 2);
    /* NOTREACHED */
  }

  memcount = pixelcount * 4;
  row_stride = cinfo.output_width * cinfo.output_components;

  out = ri.Malloc(memcount);

  *out_w = cinfo.output_width;
  *out_h = cinfo.output_height;

  while (cinfo.output_scanline < cinfo.output_height) {
    buf = ((out+(row_stride*cinfo.output_scanline)));
    buffer = &buf;
    (void) jpeg_read_scanlines(&cinfo, buffer, 1);
  }

  buf = out;

  /* Expand from RGB to RGBA */
  sindex = pixelcount * cinfo.output_components;
  dindex = memcount;

  do
  {
    buf[--dindex] = 255;
    buf[--dindex] = buf[--sindex];
    buf[--dindex] = buf[--sindex];
    buf[--dindex] = buf[--sindex];
  } while(sindex);

  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);

  *out_pixels = out;
  /* Normal return -- stack is fine on success path */
}
#endif /* __PS3__ */
'''

    # PS3 code path for inside R_LoadJPG body
    PS3_BODY = '''#ifdef __PS3__
  /* PS3/PPC64: Two-level setjmp to survive libjpeg longjmp.
   * See comment above R_LoadJPG_Decompress for full explanation. */
  int len;
  void *fbuffer = NULL;
  jmp_buf escape;

  len = ri.FS_ReadFile ( ( char * ) filename, &fbuffer);
  if (!fbuffer || len <= 0) {
    if (fbuffer) ri.FS_FreeFile(fbuffer);
    return;
  }

  /* Validate JPEG SOI marker before entering libjpeg.
   * Catches TGA-disguised-as-JPG files (Q3A data quirk). */
  {
    unsigned char *raw = (unsigned char *)fbuffer;
    if (len < 3 || raw[0] != 0xFF || raw[1] != 0xD8) {
      ri.FS_FreeFile(fbuffer);
      return;
    }
  }

  /* Set up escape point -- if libjpeg errors, we land here */
  ps3_jpg_escape = &escape;
  if (setjmp(escape))
  {
    /* libjpeg errored inside the helper. The helper longjmp'd here,
     * bypassing its own corrupted epilogue. Our locals are fine. */
    ri.FS_FreeFile(fbuffer);
    return;
  }

  R_LoadJPG_Decompress((unsigned char *)fbuffer, len,
                        (byte **)pic, width, height);

  ri.FS_FreeFile(fbuffer);

#else /* !__PS3__ */
'''

    # --- Insert the PS3 helper function before R_LoadJPG ---
    marker = "void R_LoadJPG(const char *filename, unsigned char **pic, int *width, int *height)\n{"
    if marker not in content:
        print("  WARNING: Could not find R_LoadJPG function signature")
        sys.exit(1)

    content = content.replace(marker, PS3_HELPER.lstrip('\n') + '\n' + marker)
    changed = True
    print("  Inserted R_LoadJPG_Decompress helper function")

    # --- Insert PS3 code path at the start of R_LoadJPG body ---
    func_start = content.find(marker)
    brace_pos = func_start + len(marker) - 1

    orig_body_marker = "  /* This struct contains the JPEG decompression parameters"
    orig_body_pos = content.find(orig_body_marker, brace_pos)
    if orig_body_pos < 0:
        print("  WARNING: Could not find original R_LoadJPG body")
        sys.exit(1)

    content = (content[:brace_pos + 1] + '\n' +
               PS3_BODY +
               "  /* Original ioq3 code path for non-PS3 platforms */\n" +
               content[orig_body_pos:])
    changed = True
    print("  Inserted PS3 code path in R_LoadJPG")

    # --- Add #endif before closing brace of R_LoadJPG ---
    end_marker = "  /* And we're done! */\n}"
    if end_marker in content:
        content = content.replace(end_marker,
                                  "  /* And we're done! */\n\n#endif /* __PS3__ */\n}")
        changed = True
        print("  Added #endif at end of R_LoadJPG")
    else:
        print("  WARNING: Could not find end of R_LoadJPG")

    if changed:
        with open(path, "w") as f:
            f.write(content)
        print("  tr_image_jpg.c patched successfully")
    else:
        print("  No changes needed")

if __name__ == "__main__":
    main()
