#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""录制产物的独立校验：用 Python 重新解析 AVI，确认它是能播的视频文件。

为什么要有这道校验：C++ 侧写 AVI 的代码和 C++ 侧读 AVI 的测试用的是同一套
「对格式的理解」——写错了，两边会一起错，文件就带着错出厂了。
这里的解析器是**另写的一份**（Python 标准库 + PIL 的 JPEG 解码器），
它不知道 C++ 那边怎么写的，只按 RIFF/AVI 规范走字节：

  * RIFF 长度 == 文件长度 - 8；
  * avih 里的总帧数 == movi 里 '00dc' 帧块数 == idx1 索引条目数；
  * avih 与 strf 里的宽高一致；
  * 每条索引都真的指向一个 '00dc' 帧块，长度与索引一致；
  * 每一帧都是完整的 JPEG（SOI/EOI），并且**能被 PIL 解码**，
    解出来的宽高与文件头声明的一致。

最后一条是关键：它证明这些帧不是「看起来像 JPEG 的字节」，而是真的图片。

用法：
    python scripts/verify-recordings.py --dir <样例目录>
    python scripts/verify-recordings.py --dir <样例目录> --self-test

--self-test 会故意把样例改坏（抽掉索引、破坏一帧），确认本校验器当场报错。
"""
from __future__ import annotations

import argparse
import os
import struct
import sys

try:
    from PIL import Image
    import io

    HAVE_PIL = True
except Exception:  # pragma: no cover - 环境问题
    HAVE_PIL = False


class Problem(Exception):
    pass


def fourcc(data: bytes, position: int) -> str:
    return data[position:position + 4].decode("latin-1")


def u32(data: bytes, position: int) -> int:
    return struct.unpack_from("<I", data, position)[0]


def u16(data: bytes, position: int) -> int:
    return struct.unpack_from("<H", data, position)[0]


def parse_avi(data: bytes):
    """走一遍 RIFF/AVI 结构，返回 (avih 字段, 帧块列表, 索引条目)。"""
    if len(data) < 12:
        raise Problem("文件太短，不是 AVI")
    if fourcc(data, 0) != "RIFF":
        raise Problem("不是 RIFF 文件")
    riff_size = u32(data, 4)
    if riff_size != len(data) - 8:
        raise Problem("RIFF 长度 %d 与实际文件长度 %d 不符" % (riff_size, len(data) - 8))
    if fourcc(data, 8) != "AVI ":
        raise Problem("RIFF 里不是 AVI ")

    avih = {}
    strf = {}
    frames = []          # (chunk 位置, 长度)
    index_entries = []   # (偏移, 长度)
    movi_pos = -1

    position = 12
    while position + 8 <= len(data):
        identifier = fourcc(data, position)
        size = u32(data, position + 4)
        if identifier == "LIST":
            list_type = fourcc(data, position + 8)
            list_end = position + 8 + size
            if list_type == "movi":
                movi_pos = position + 8
                cursor = position + 12
                while cursor + 8 <= list_end:
                    chunk_id = fourcc(data, cursor)
                    chunk_size = u32(data, cursor + 4)
                    if chunk_id == "00dc":
                        frames.append((cursor, chunk_size))
                    cursor += 8 + chunk_size + (chunk_size % 2)
            elif list_type == "hdrl":
                cursor = position + 12
                while cursor + 8 <= list_end:
                    chunk_id = fourcc(data, cursor)
                    chunk_size = u32(data, cursor + 4)
                    if chunk_id == "avih" and chunk_size >= 56:
                        avih = {
                            "microsecondsPerFrame": u32(data, cursor + 8),
                            "maxBytesPerSec": u32(data, cursor + 12),
                            "flags": u32(data, cursor + 20),
                            "totalFrames": u32(data, cursor + 24),
                            "streams": u32(data, cursor + 32),
                            "suggestedBufferSize": u32(data, cursor + 36),
                            "width": u32(data, cursor + 40),
                            "height": u32(data, cursor + 44),
                        }
                    elif chunk_id == "strf" and chunk_size >= 40:
                        strf = {
                            "width": struct.unpack_from("<i", data, cursor + 12)[0],
                            "height": struct.unpack_from("<i", data, cursor + 16)[0],
                            "bitCount": u16(data, cursor + 26),
                            "compression": fourcc(data, cursor + 28),
                        }
                    cursor += 8 + chunk_size + (chunk_size % 2)
            position += 8 + size + (size % 2)
        elif identifier == "idx1":
            count = size // 16
            for i in range(count):
                entry = position + 8 + i * 16
                index_entries.append((u32(data, entry + 8), u32(data, entry + 12)))
            position += 8 + size + (size % 2)
        else:
            position += 8 + size + (size % 2)

    if not avih:
        raise Problem("没有 avih 视频头")
    if movi_pos < 0:
        raise Problem("没有 movi 帧数据")
    return avih, strf, frames, index_entries, movi_pos


def verify_avi(path: str) -> list:
    problems = []
    with open(path, "rb") as handle:
        data = handle.read()

    try:
        avih, strf, frames, index_entries, movi_pos = parse_avi(data)
    except Problem as exc:
        return ["%s：%s" % (os.path.basename(path), exc)]

    name = os.path.basename(path)

    if avih["streams"] != 1:
        problems.append("%s：流数是 %d，应为 1" % (name, avih["streams"]))
    if avih["totalFrames"] != len(frames):
        problems.append("%s：avih 说 %d 帧，movi 里实际 %d 帧"
                        % (name, avih["totalFrames"], len(frames)))
    if len(index_entries) != len(frames):
        problems.append("%s：索引 %d 条，帧 %d 个" % (name, len(index_entries), len(frames)))
    if strf:
        if strf["width"] != avih["width"] or strf["height"] != avih["height"]:
            problems.append("%s：avih 宽高 %dx%d 与 strf 的 %dx%d 不一致"
                            % (name, avih["width"], avih["height"],
                               strf["width"], strf["height"]))
        if strf["compression"] != "MJPG":
            problems.append("%s：压缩方式是 %s，应为 MJPG" % (name, strf["compression"]))

    for offset, length in index_entries:
        absolute = movi_pos + offset
        if absolute + 8 > len(data):
            problems.append("%s：索引偏移 %d 指向文件之外" % (name, offset))
            continue
        if fourcc(data, absolute) != "00dc":
            problems.append("%s：索引偏移 %d 指向的不是帧块（是 %s）"
                            % (name, offset, fourcc(data, absolute)))
            continue
        if u32(data, absolute + 4) != length:
            problems.append("%s：索引长度 %d 与帧块长度 %d 不一致"
                            % (name, length, u32(data, absolute + 4)))

    for index, (chunk_pos, chunk_size) in enumerate(frames):
        payload = data[chunk_pos + 8:chunk_pos + 8 + chunk_size]
        if len(payload) < 4 or payload[0] != 0xFF or payload[1] != 0xD8:
            problems.append("%s：第 %d 帧不是 JPEG（缺 SOI）" % (name, index + 1))
            continue
        if payload[-2] != 0xFF or payload[-1] != 0xD9:
            problems.append("%s：第 %d 帧不是完整 JPEG（缺 EOI）" % (name, index + 1))
            continue
        if HAVE_PIL:
            try:
                image = Image.open(io.BytesIO(payload))
                image.load()
            except Exception as exc:
                problems.append("%s：第 %d 帧解不开（%s）" % (name, index + 1, exc))
                continue
            if (image.width, image.height) != (avih["width"], avih["height"]):
                problems.append("%s：第 %d 帧是 %dx%d，与文件头声明的 %dx%d 不一致"
                                % (name, index + 1, image.width, image.height,
                                   avih["width"], avih["height"]))
    return problems


def self_test(sample: str) -> list:
    """把样例改坏，确认本校验器真的会拦人。"""
    failures = []
    with open(sample, "rb") as handle:
        original = handle.read()

    # 破坏 1：抽掉帧索引（把 idx1 改成空索引）。
    marker = original.find(b"idx1")
    if marker < 0:
        failures.append("自检失败：样例里找不到 idx1")
    else:
        broken = bytearray(original)
        struct.pack_into("<I", broken, marker + 4, 0)
        path = sample + ".broken-index.avi"
        with open(path, "wb") as handle:
            handle.write(bytes(broken))
        problems = verify_avi(path)
        os.remove(path)
        if not problems:
            failures.append("自检失败：抽掉索引后本校验器仍然放行")
        else:
            print("   改坏索引 → 如报错：%s" % problems[0])

    # 破坏 2：把第一帧的 JPEG 头打坏。
    try:
        _avih, _strf, frames, _index, _movi = parse_avi(original)
    except Problem as exc:
        failures.append("自检失败：样例解析不了（%s）" % exc)
        return failures
    if not frames:
        failures.append("自检失败：样例里没有帧")
        return failures
    broken = bytearray(original)
    broken[frames[0][0] + 8] = 0x00
    path = sample + ".broken-frame.avi"
    with open(path, "wb") as handle:
        handle.write(bytes(broken))
    problems = verify_avi(path)
    os.remove(path)
    if not problems:
        failures.append("自检失败：破坏一帧后本校验器仍然放行")
    else:
        print("   改坏一帧 → 如实报错：%s" % problems[0])
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="录制产物的独立校验")
    parser.add_argument("--dir", required=True, help="样例目录")
    parser.add_argument("--self-test", action="store_true", help="改坏样例，确认校验器会拦人")
    args = parser.parse_args()

    if not os.path.isdir(args.dir):
        print("目录不存在：%s" % args.dir)
        return 1

    samples = sorted(name for name in os.listdir(args.dir) if name.lower().endswith(".avi"))
    if not samples:
        print("目录里没有 .avi 样例：%s" % args.dir)
        return 1

    print("校验 %d 份录制样例（JPEG 解码器：%s）" % (len(samples), "PIL" if HAVE_PIL else "无（只查字节）"))

    problems = []
    for name in samples:
        path = os.path.join(args.dir, name)
        found = verify_avi(path)
        if found:
            problems.extend(found)
        else:
            print("   %s 通过" % name)

    failures = []
    if args.self_test:
        failures = self_test(os.path.join(args.dir, samples[0]))

    if problems or failures:
        print("")
        print("!! 录制产物校验未通过：")
        for item in problems:
            print("   - " + item)
        for item in failures:
            print("   - " + item)
        return 1
    print("录制产物是结构完整、能被解码器打开的视频文件")
    return 0


if __name__ == "__main__":
    sys.exit(main())
