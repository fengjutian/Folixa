#!/usr/bin/env python3
"""Folixa 应用图标生成器 —— 单一几何来源，同时输出 SVG / PNG / ICO / 预览图。

设计网格：1024 × 1024。所有坐标都用设计单位书写，导出时按目标尺寸等比缩放，
因此 16px 到 1024px 的形状完全一致，不会出现"大图标好看、小图标糊成一团"。

用法：
    python assets/branding/build_icon.py

产物（同目录）：
    folixa-icon.svg            1024×1024 主矢量（应用内 QIcon / 文档用）
    folixa-icon-<n>.png        16–1024 各尺寸位图
    folixa.ico                 Windows 多尺寸图标（16/24/32/48/64/128/256）
    folixa-icon-preview.png    浅色 / 深色背景下的尺寸对照图

依赖：Pillow、numpy（仓库脚本统一使用随附 Python 运行时）。
"""

from __future__ import annotations

import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = Path(__file__).resolve().parent
S = 1024  # 设计网格边长

# ------------------------------------------------------------------ 设计 Token
# 颜色全部取自 docs/UI_DESIGN_SYSTEM.md，不另造品牌色。
TILE_STOPS = [(0.00, "#C13CC7"), (0.48, "#8C35BB"), (1.00, "#4D46C7")]
PAGE_STOPS = [(0.00, "#FFFFFF"), (1.00, "#E7EDFA")]                     # paper
FOLD_STOPS = [(0.00, "#C9B7FB"), (1.00, "#7B5CE0")]                     # ai.300 → ai.600
LINE_COLOR = "#C9D5EE"
SHADOW_COLOR = "#28133D"

TILE_RADIUS = 196
PAGE = dict(l=292, t=142, r=760, b=866, rad=44)  # 纸张矩形
FOLD = 165                                       # 折角边长
LINE_WIDTH = 34
LINES = [(470, 530, 680), (548, 530, 706), (626, 530, 650)]  # (y, x1, x2)
BADGE = dict(l=112, t=278, r=484, b=748, rad=48)
BADGE_STOPS = [(0.00, "#72258F"), (0.55, "#642584"), (1.00, "#4D287A")]

OUT_SIZES = [1024, 512, 256, 128, 64, 48, 32, 24, 16]
ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]


# ------------------------------------------------------------------ SVG 输出
def svg_document() -> str:
    p = PAGE
    cut_x, cut_y = p["r"] - FOLD, p["t"] + FOLD
    page_path = (
        f"M{p['l'] + p['rad']} {p['t']} H{cut_x} L{p['r']} {cut_y} "
        f"V{p['b'] - p['rad']} A{p['rad']} {p['rad']} 0 0 1 {p['r'] - p['rad']} {p['b']} "
        f"H{p['l'] + p['rad']} A{p['rad']} {p['rad']} 0 0 1 {p['l']} {p['b'] - p['rad']} "
        f"V{p['t'] + p['rad']} A{p['rad']} {p['rad']} 0 0 1 {p['l'] + p['rad']} {p['t']} Z"
    )
    lines = "\n".join(
        f'    <path d="M{x1} {y} H{x2}"/>' for y, x1, x2 in LINES
    )
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="0 0 1024 1024" fill="none" role="img" aria-label="Folixa">
  <title>Folixa — Prism Fold</title>
  <defs>
    <linearGradient id="tile" x1="120" y1="40" x2="900" y2="980" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="{TILE_STOPS[0][1]}"/>
      <stop offset="0.52" stop-color="{TILE_STOPS[1][1]}"/>
      <stop offset="1" stop-color="{TILE_STOPS[2][1]}"/>
    </linearGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="560" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#FFFFFF" stop-opacity="0.20"/>
      <stop offset="1" stop-color="#FFFFFF" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="page" x1="300" y1="200" x2="700" y2="840" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="{PAGE_STOPS[0][1]}"/>
      <stop offset="1" stop-color="{PAGE_STOPS[1][1]}"/>
    </linearGradient>
    <linearGradient id="fold" x1="{cut_x}" y1="{p['t']}" x2="{p['r']}" y2="{cut_y}" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="{FOLD_STOPS[0][1]}"/>
      <stop offset="1" stop-color="{FOLD_STOPS[1][1]}"/>
    </linearGradient>
    <filter id="pageShadow" x="150" y="80" width="740" height="900" filterUnits="userSpaceOnUse" color-interpolation-filters="sRGB">
      <feDropShadow dx="0" dy="18" stdDeviation="24" flood-color="{SHADOW_COLOR}" flood-opacity="0.24"/>
    </filter>
    <clipPath id="tileClip"><rect width="1024" height="1024" rx="{TILE_RADIUS}"/></clipPath>
  </defs>

  <g clip-path="url(#tileClip)">
    <rect width="1024" height="1024" fill="url(#tile)"/>
    <rect width="1024" height="1024" fill="url(#sheen)"/>
  </g>

  <g filter="url(#pageShadow)">
    <path d="{page_path}" fill="url(#page)"/>
  </g>

  <path d="M{cut_x} {p['t']} L{p['r']} {cut_y} H{cut_x} Z" fill="url(#fold)"/>

  <g stroke="{LINE_COLOR}" stroke-width="{LINE_WIDTH}" stroke-linecap="round">
{lines}
  </g>

  <defs>
    <linearGradient id="badge" x1="112" y1="278" x2="484" y2="748" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="{BADGE_STOPS[0][1]}"/>
      <stop offset="0.55" stop-color="{BADGE_STOPS[1][1]}"/>
      <stop offset="1" stop-color="{BADGE_STOPS[2][1]}"/>
    </linearGradient>
    <filter id="badgeShadow" x="58" y="208" width="520" height="650" filterUnits="userSpaceOnUse">
      <feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#24102F" flood-opacity="0.28"/>
    </filter>
  </defs>
  <rect x="112" y="278" width="372" height="470" rx="48" fill="url(#badge)" filter="url(#badgeShadow)"/>
  <path d="M210 385 H386 V445 H278 V510 H374 V570 H278 V668 H210 Z" fill="#FFFFFF"/>
  <rect x="120" y="286" width="356" height="454" rx="40" stroke="#FFFFFF" stroke-opacity="0.13" stroke-width="7"/>

  <rect x="4" y="4" width="1016" height="1016" rx="{TILE_RADIUS - 4}" stroke="#FFFFFF" stroke-opacity="0.16" stroke-width="8"/>
</svg>
"""


# ------------------------------------------------------------------ 光栅化
def _rgb(hex_color: str) -> np.ndarray:
    h = hex_color.lstrip("#")
    return np.array([int(h[i : i + 2], 16) for i in (0, 2, 4)], dtype=np.float64)


def _gradient(w: int, h: int, stops, p1, p2) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w]
    dx, dy = p2[0] - p1[0], p2[1] - p1[1]
    denom = dx * dx + dy * dy
    t = np.clip(((xx - p1[0]) * dx + (yy - p1[1]) * dy) / denom, 0.0, 1.0)
    out = np.zeros((h, w, 3), dtype=np.float64)
    pos = [s[0] for s in stops]
    cols = [_rgb(s[1]) for s in stops]
    out[:] = cols[0]
    for i in range(len(stops) - 1):
        lo, hi = pos[i], pos[i + 1]
        m = (t >= lo) & ((t <= hi) if i == len(stops) - 2 else (t < hi))
        if not m.any():
            continue
        local = np.clip((t[m] - lo) / max(hi - lo, 1e-9), 0, 1)[:, None]
        out[m] = cols[i] * (1 - local) + cols[i + 1] * local
    return out


def _layer(w: int, h: int, color, alpha: np.ndarray) -> Image.Image:
    rgb = np.broadcast_to(np.asarray(color, dtype=np.float64), (h, w, 3))
    rgba = np.dstack([rgb, np.clip(alpha, 0, 1) * 255.0]).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def _tile_alpha(size: int, f: float) -> np.ndarray:
    m = Image.new("L", (size, size), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, size, size], radius=TILE_RADIUS * f, fill=255)
    return np.asarray(m, dtype=np.float64) / 255.0


def _page_alpha(size: int, f: float) -> np.ndarray:
    p = PAGE
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle(
        [p["l"] * f, p["t"] * f, p["r"] * f, p["b"] * f], radius=p["rad"] * f, fill=255
    )
    d.polygon(
        [
            ((p["r"] - FOLD) * f, p["t"] * f),
            (p["r"] * f, (p["t"] + FOLD) * f),
            (p["r"] * f, p["t"] * f),
        ],
        fill=0,
    )
    return np.asarray(m, dtype=np.float64) / 255.0


def _lines_for(out_px: int):
    """小尺寸光学校正：像素不够时删行，而不是把三行糊成灰块。"""
    if out_px >= 32:
        return LINES
    if out_px >= 24:
        return [(500, 310, 640), (600, 310, 560)]
    return [(550, 310, 640)]


def render(out_px: int, ss: int = 4) -> Image.Image:
    size = out_px * ss
    f = size / S
    p = PAGE
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    # 圆角底板 + 顶部高光
    tile_a = _tile_alpha(size, f)
    canvas = Image.alpha_composite(
        canvas,
        _layer(size, size, _gradient(size, size, TILE_STOPS, (120 * f, 40 * f), (900 * f, 980 * f)), tile_a),
    )
    sheen = np.clip(1.0 - np.mgrid[0:size, 0:size][0] / (560 * f), 0, 1) * 0.20
    canvas = Image.alpha_composite(
        canvas, _layer(size, size, (255, 255, 255), sheen * tile_a)
    )

    # 纸张投影
    page_a = _page_alpha(size, f)
    blurred = Image.fromarray((page_a * 255).astype(np.uint8), "L").filter(
        ImageFilter.GaussianBlur(24 * f)
    )
    sh = np.asarray(blurred, dtype=np.float64) / 255.0 * 0.24
    dy = int(round(18 * f))
    shifted = np.zeros_like(sh)
    shifted[dy:, :] = sh[: size - dy, :]
    canvas = Image.alpha_composite(canvas, _layer(size, size, _rgb(SHADOW_COLOR), shifted))

    # 纸张本体
    page_rgb = _gradient(size, size, PAGE_STOPS, (300 * f, 200 * f), (700 * f, 840 * f))
    canvas = Image.alpha_composite(canvas, _layer(size, size, page_rgb, page_a))

    # 纸面文字线
    lines = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(lines)
    for y, x1, x2 in _lines_for(out_px):
        w = LINE_WIDTH * f
        d.line([(x1 * f, y * f), (x2 * f, y * f)], fill=LINE_COLOR, width=max(1, int(round(w))))
        r = w / 2
        for x in (x1 * f, x2 * f):
            d.ellipse([x - r, y * f - r, x + r, y * f + r], fill=LINE_COLOR)
    lines_a = np.asarray(lines.getchannel("A"), dtype=np.float64) / 255.0 * page_a
    lines.putalpha(Image.fromarray((lines_a * 255).astype(np.uint8), "L"))
    canvas = Image.alpha_composite(canvas, lines)

    # 折角
    fold = Image.new("L", (size, size), 0)
    ImageDraw.Draw(fold).polygon(
        [
            ((p["r"] - FOLD) * f, p["t"] * f),
            (p["r"] * f, (p["t"] + FOLD) * f),
            ((p["r"] - FOLD) * f, (p["t"] + FOLD) * f),
        ],
        fill=255,
    )
    fold_rgb = _gradient(
        size, size, FOLD_STOPS,
        ((p["r"] - FOLD) * f, p["t"] * f), (p["r"] * f, (p["t"] + FOLD) * f),
    )
    canvas = Image.alpha_composite(
        canvas, _layer(size, size, fold_rgb, np.asarray(fold, dtype=np.float64) / 255.0)
    )

    # 前景字母块借鉴 Office 的分层轮廓，但使用 Folixa 的品牌色与字母 F。
    badge = Image.new("L", (size, size), 0)
    ImageDraw.Draw(badge).rounded_rectangle(
        [BADGE["l"] * f, BADGE["t"] * f, BADGE["r"] * f, BADGE["b"] * f],
        radius=BADGE["rad"] * f,
        fill=255,
    )
    badge_alpha = np.asarray(badge, dtype=np.float64) / 255.0
    blurred_badge = badge.filter(ImageFilter.GaussianBlur(18 * f))
    badge_shadow = np.asarray(blurred_badge, dtype=np.float64) / 255.0 * 0.28
    badge_dy = max(1, int(round(14 * f)))
    shifted_badge_shadow = np.zeros_like(badge_shadow)
    shifted_badge_shadow[badge_dy:, :] = badge_shadow[: size - badge_dy, :]
    canvas = Image.alpha_composite(
        canvas, _layer(size, size, _rgb("#15102F"), shifted_badge_shadow)
    )
    badge_rgb = _gradient(
        size, size, BADGE_STOPS,
        (BADGE["l"] * f, BADGE["t"] * f), (BADGE["r"] * f, BADGE["b"] * f),
    )
    canvas = Image.alpha_composite(canvas, _layer(size, size, badge_rgb, badge_alpha))

    mark = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    ImageDraw.Draw(mark).polygon(
        [(210*f,385*f),(386*f,385*f),(386*f,445*f),(278*f,445*f),
         (278*f,510*f),(374*f,510*f),(374*f,570*f),(278*f,570*f),
         (278*f,668*f),(210*f,668*f)],
        fill=(255, 255, 255, 255),
    )
    canvas = Image.alpha_composite(canvas, mark)

    # 底板描边高光
    edge = Image.new("L", (size, size), 0)
    ImageDraw.Draw(edge).rounded_rectangle(
        [4 * f, 4 * f, size - 4 * f, size - 4 * f],
        radius=(TILE_RADIUS - 4) * f, outline=255, width=max(1, int(round(8 * f))),
    )
    canvas = Image.alpha_composite(
        canvas, _layer(size, size, (255, 255, 255), np.asarray(edge, dtype=np.float64) / 255.0 * 0.16)
    )

    return canvas.resize((out_px, out_px), Image.LANCZOS)


# ------------------------------------------------------------------ ICO / 预览
def write_ico(path: Path, images: list[Image.Image]) -> None:
    """手写 ICO：每帧都是 PNG，避免 Pillow 只从单张大图缩放出所有尺寸。"""
    frames = []
    for img in images:
        import io

        buf = io.BytesIO()
        img.save(buf, format="PNG")
        frames.append((img.width, buf.getvalue()))

    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    entries, blobs = b"", b""
    for side, data in frames:
        entries += struct.pack(
            "<BBBBHHII", side % 256, side % 256, 0, 0, 1, 32, len(data), offset
        )
        blobs += data
        offset += len(data)
    path.write_bytes(header + entries + blobs)


def preview_sheet(images: dict[int, Image.Image]) -> Image.Image:
    sheet = Image.new("RGBA", (1200, 720), "#FFFFFF")
    for row, bg in enumerate(("#F5F7FA", "#111827")):
        panel = Image.new("RGBA", (1200, 360), bg)
        x = 48
        for s in (16, 24, 32, 48, 64, 128):
            panel.alpha_composite(images[s], (x, 40 + (256 - s) // 2))
            x += max(s, 40) + 32
        panel.alpha_composite(images[256], (860, 52))
        sheet.alpha_composite(panel, (0, row * 360))
    return sheet


def main() -> None:
    (HERE / "folixa-icon.svg").write_text(svg_document(), encoding="utf-8")

    images: dict[int, Image.Image] = {}
    for px in OUT_SIZES:
        img = render(px, ss=max(2, min(8, 2048 // px)))
        img.save(HERE / f"folixa-icon-{px}.png")
        images[px] = img

    write_ico(HERE / "folixa.ico", [images[s] for s in ICO_SIZES])
    preview_sheet(images).save(HERE / "folixa-icon-preview.png")
    print("generated:", ", ".join(sorted(p.name for p in HERE.glob("folixa*"))))


if __name__ == "__main__":
    main()
