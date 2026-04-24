#!/usr/bin/env python3
"""
Generate a PARAM.SFO file for PS3 homebrew packaging.
No XML dependency -- builds the binary directly.

Usage: python3 make_sfo.py <output.sfo> [--title TITLE] [--appid TITLE_ID]
"""
import struct
import sys

SFO_MAGIC = 0x46535000  # "\0PSF" on disk when packed little-endian
SFO_VERSION = 0x00000101
FMT_STRING = 0x0204
FMT_INT32  = 0x0404

def align(n, a):
    return (n + a - 1) & ~(a - 1)

def make_sfo(params):
    """Build a PARAM.SFO binary from a list of (key, value) tuples.
    String values are str, integer values are int."""
    # Sort by key (SFO spec requires alphabetical order)
    params = sorted(params, key=lambda x: x[0])

    count = len(params)

    # Build key table and value table
    key_data = b""
    val_entries = []
    val_data = b""
    val_offset = 0

    for key, value in params:
        key_bytes = key.encode("ascii") + b"\x00"

        if isinstance(value, int):
            fmt = FMT_INT32
            vbytes = struct.pack("<I", value)
            padded_len = 4
        else:
            fmt = FMT_STRING
            vbytes = value.encode("utf-8") + b"\x00"
            # Padding alignment depends on key
            if key == "TITLE":
                padded_len = align(len(vbytes), 0x80)
            elif key == "LICENSE":
                padded_len = align(len(vbytes), 0x200)
            elif key == "TITLE_ID":
                padded_len = align(len(vbytes), 0x10)
            else:
                padded_len = align(len(vbytes), 4)

        val_entries.append((len(key_data), fmt, len(vbytes), padded_len, val_offset))
        key_data += key_bytes
        val_data += vbytes + b"\x00" * (padded_len - len(vbytes))
        val_offset += padded_len

    # Header: 20 bytes
    # Index table: 16 bytes per entry
    index_size = count * 16
    key_table_offset = 20 + index_size
    val_table_offset = align(key_table_offset + len(key_data), 4)
    key_padding = val_table_offset - (key_table_offset + len(key_data))

    # Build header
    header = struct.pack("<IIIII",
        SFO_MAGIC,
        SFO_VERSION,
        key_table_offset,
        val_table_offset,
        count
    )

    # Build index table
    index_data = b""
    for key_off, fmt, val_len, padded_len, val_off in val_entries:
        index_data += struct.pack("<HHIII",
            key_off,
            fmt,
            val_len,
            padded_len,
            val_off
        )

    return header + index_data + key_data + (b"\x00" * key_padding) + val_data

def main():
    title = "ioQuake3"
    appid = "IOQ3PS300"
    output = None

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] == "--title" and i + 1 < len(args):
            title = args[i + 1]
            i += 2
        elif args[i] == "--appid" and i + 1 < len(args):
            appid = args[i + 1]
            i += 2
        elif not args[i].startswith("-"):
            output = args[i]
            i += 1
        else:
            i += 1

    if output is None:
        print("Usage: python3 make_sfo.py <output.sfo> [--title TITLE] [--appid TITLE_ID]",
              file=sys.stderr)
        sys.exit(1)

    params = [
        ("APP_VER",         "01.00"),
        ("ATTRIBUTE",       0),
        ("BOOTABLE",        1),
        ("CATEGORY",        "HG"),
        ("LICENSE",         "This application was created with the official non-official "
                            "SDK called psl1ght, for more information visit "
                            "http://www.psl1ght.com/ . This is in no way associated with "
                            "Sony Computer Entertainment Inc., please do not contact them "
                            "for help, they will not be able to provide it."),
        ("PARENTAL_LEVEL",  0),
        ("PS3_SYSTEM_VER",  "01.8000"),
        ("RESOLUTION",      63),
        ("SOUND_FORMAT",    279),
        ("TITLE",           title),
        ("TITLE_ID",        appid),
        ("VERSION",         "01.00"),
    ]

    sfo_data = make_sfo(params)

    with open(output, "wb") as f:
        f.write(sfo_data)

    print("Created %s (%d bytes, TITLE=%s, TITLE_ID=%s)" %
          (output, len(sfo_data), title, appid))

if __name__ == "__main__":
    main()
