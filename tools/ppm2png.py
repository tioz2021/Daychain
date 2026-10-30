#!/usr/bin/env python3
"""Convert a binary PPM (P6) image to PNG. Used for the native app's screenshots."""
import struct
import sys
import zlib


def read_ppm(path):
    data = open(path, "rb").read()
    if not data.startswith(b"P6"):
        raise SystemExit("not a P6 PPM file")
    pos = 2
    values = []
    while len(values) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if data[pos : pos + 1] == b"#":
            while pos < len(data) and data[pos] != 10:
                pos += 1
            continue
        start = pos
        while pos < len(data) and data[pos : pos + 1].isdigit():
            pos += 1
        values.append(int(data[start:pos]))
    pos += 1  # one whitespace byte after the max value
    width, height, _ = values
    return width, height, data[pos : pos + width * height * 3]


def write_png(path, width, height, rgb):
    raw = bytearray()
    stride = width * 3
    for y in range(height):
        raw.append(0)
        raw += rgb[y * stride : (y + 1) * stride]

    def chunk(kind, payload):
        return (
            struct.pack(">I", len(payload))
            + kind
            + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
        )

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: ppm2png.py <in.ppm> <out.png>")
    width, height, rgb = read_ppm(sys.argv[1])
    write_png(sys.argv[2], width, height, rgb)
    print(f"{sys.argv[2]}: {width}x{height}")


if __name__ == "__main__":
    main()
