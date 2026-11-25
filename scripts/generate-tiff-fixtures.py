#!/usr/bin/env python3
"""生成 TIFF 测试夹具（由 libtiff / Pillow 写出）。

为什么要有这些夹具：
    自造字节的用例只能证明「解码器认得自己写的格式」——
    写字节的人和读字节的人是我自己，两边一起错也照样全绿。
    这批夹具由**独立实现**（libtiff）写出，解码器能不能读懂
    真实世界的文件，靠它们说话。

夹具是**签进仓库**的（tests/fixtures/tiff/），构建与测试都不需要 Pillow。
    需要重新生成时：python scripts/generate-tiff-fixtures.py

覆盖的读法：
    rgb-lzw      RGB 8 位 + LZW（码宽会跨过 511 / 1023 两道坎）
    rgb-deflate  RGB 8 位 + Deflate
    gray-packbits  灰度 8 位 + PackBits
    palette      调色板（4 位索引）
    bigendian    大端（MM）字节序
    multipage    一个文件两页
"""

from __future__ import annotations

import argparse
import os
import struct
import sys

from PIL import Image

# 夹具里那张图的像素（RGB）。刻意用「有规律但不可预测」的图案：
# 纯色图压缩得太好，压不出足够多的 LZW 码，跨不过码宽的坎。
def pattern_rgb(width: int, height: int) -> bytes:
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out.append((x * 7 + y * 13) % 256)
            out.append((x * 11 + y * 3) % 256)
            out.append((x * 5 + y * 17) % 256)
    return bytes(out)


def pattern_gray(width: int, height: int) -> bytes:
    out = bytearray()
    for y in range(height):
        for x in range(width):
            out.append((x * 31 + y * 23) % 256)
    return bytes(out)


def write_rgb(path: str, width: int, height: int, compression: str) -> None:
    image = Image.frombytes("RGB", (width, height), pattern_rgb(width, height))
    image.save(path, format="TIFF", compression=compression)


def write_gray(path: str, width: int, height: int, compression: str) -> None:
    image = Image.frombytes("L", (width, height), pattern_gray(width, height))
    image.save(path, format="TIFF", compression=compression)


def write_palette(path: str) -> None:
    # 4 位索引：调色板是 16 位分量（红 16 档、绿 16 档、蓝 16 档）。
    indices = bytes(((x + y) % 16) for y in range(4) for x in range(16))
    image = Image.frombytes("P", (16, 4), indices)
    palette = bytearray()
    for channel in range(3):
        for i in range(16):
            value = 0 if channel != 0 else 0xFF
            if channel == 2:
                value = 0xFF
            if channel == 1:
                value = 0x80
            palette += struct.pack(">H", value)
    image.putpalette(bytes(palette))
    image.save(path, format="TIFF", compression="tiff_adobe_deflate")


def write_bigendian(path: str) -> None:
    """自己拼一个大端（MM）TIFF：Pillow 只会写小端。

    结构照 TIFF 6.0 写：头 → IFD → 像素数据。
    大端文件的关键在于**所有**多字节字段都按大端排，
    包括内联在 IFD 里的值（值要左对齐在 4 字节的 value 字段里）。
    """
    width, height = 4, 3
    pixels = pattern_rgb(width, height)
    # 头：字节序标记（MM）+ 魔数 42 + 第一个 IFD 的偏移，共 8 字节。
    header = b"MM" + struct.pack(">HI", 42, 8)
    tags = [
        (256, 4, 1, width),    # ImageWidth
        (257, 4, 1, height),   # ImageLength
        (258, 3, 3, None),     # BitsPerSample → 外置
        (259, 3, 1, 1),        # Compression = none
        (262, 3, 1, 2),        # Photometric = RGB
        (273, 4, 1, None),     # StripOffsets → 稍后回填
        (277, 3, 1, 3),        # SamplesPerPixel
        (278, 4, 1, height),   # RowsPerStrip
        (279, 4, 1, len(pixels)),  # StripByteCounts
        (284, 3, 1, 1),        # PlanarConfig = chunky
    ]
    entry_count = len(tags)
    ifd_size = 2 + entry_count * 12 + 4
    strip_offset = 8 + ifd_size
    bits_offset = strip_offset + len(pixels)
    body = bytearray()
    body += struct.pack(">H", entry_count)
    for tag, typ, count, value in tags:
        if tag == 273:
            value = strip_offset
        if tag == 258:
            body_value = struct.pack(">I", bits_offset)
        else:
            body_value = struct.pack(">I", value) if typ == 4 else struct.pack(">H", value) + b"\0\0"
        body += struct.pack(">HHI", tag, typ, count) + body_value
    body += struct.pack(">I", 0)
    out = bytearray(header)
    out += body
    out += pixels
    out += struct.pack(">HHH", 8, 8, 8)
    with open(path, "wb") as handle:
        handle.write(bytes(out))


def write_multipage(path: str) -> None:
    first = Image.frombytes("RGB", (3, 2), pattern_rgb(3, 2))
    second = Image.frombytes("RGB", (5, 2), pattern_rgb(5, 2))
    first.save(path, format="TIFF", compression="tiff_adobe_deflate",
               save_all=True, append_images=[second])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("outdir", help="夹具输出目录")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.outdir, exist_ok=True)
    # LZW 那张要够大：码宽 9→10→11 两道坎都要跨过，
    # 才有资格验「码宽增长规则」这条最容易写错的逻辑。
    write_rgb(os.path.join(args.outdir, "rgb-lzw.tif"), 64, 48, "tiff_lzw")
    write_rgb(os.path.join(args.outdir, "rgb-deflate.tif"), 8, 4, "tiff_adobe_deflate")
    write_gray(os.path.join(args.outdir, "gray-packbits.tif"), 8, 4, "packbits")
    write_palette(os.path.join(args.outdir, "palette.tif"))
    write_bigendian(os.path.join(args.outdir, "bigendian.tif"))
    write_multipage(os.path.join(args.outdir, "multipage.tif"))

    if not args.quiet:
        for name in sorted(os.listdir(args.outdir)):
            size = os.path.getsize(os.path.join(args.outdir, name))
            print(f"{name}: {size} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
