#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""生成站点用到的位图资源（不依赖网络，可重复执行）。

以 public/folixa-icon.svg 为唯一图源，在 Pillow 里逐路径重绘，
保证 favicon / apple-touch-icon / 分享卡片与页面里的品牌标记完全一致。

产出：
  public/favicon.ico          浏览器标签页图标
  public/apple-touch-icon.png 180×180
  public/folixa-icon-192.png  192×192
  public/folixa-icon-512.png  512×512
  public/folixa-og.png        1200×630 社交分享卡片

用法：python assets/branding/build_site_assets.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
PUBLIC = REPO / "public"

OG_W, OG_H = 1200, 630

# 与 src/styles/global.css 中的调色板保持一致
NAVY_TOP = (12, 17, 32)
NAVY_BOTTOM = (26, 36, 62)
BLUE = (82, 119, 232)
BLUE_SOFT = (155, 177, 255)
INK = (255, 255, 255)
MUTED = (150, 162, 190)

# public/folixa-icon.svg 的渐变端点
MARK_TOP = (111, 143, 240)
MARK_BOTTOM = (65, 101, 212)
FOLD = (174, 188, 244)

FONT_SANS = Path(r"C:\Windows\Fonts\segoeuib.ttf")
FONT_SANS_REG = Path(r"C:\Windows\Fonts\segoeui.ttf")
FONT_CJK = Path(r"C:\Windows\Fonts\msyhbd.ttc")
FONT_CJK_REG = Path(r"C:\Windows\Fonts\msyh.ttc")

# SVG 里的 64×64 坐标系
MARK_BOX = (11, 6, 51, 58)   # 文档主体
MARK_CUT = (39, 6, 51, 18)   # 右上角折角切线
MARK_LINES = ((22, 29, 40), (22, 39, 36), (22, 49, 31))


def font(path: Path, size: int) -> ImageFont.FreeTypeFont:
    if not path.exists():
        raise SystemExit(f"缺少字体文件：{path}")
    return ImageFont.truetype(str(path), size)


def vertical_gradient(width: int, height: int, top: tuple, bottom: tuple) -> Image.Image:
    """逐行插值的竖直渐变。"""
    column = Image.new("RGB", (1, height))
    px = column.load()
    for y in range(height):
        t = y / max(1, height - 1)
        px[0, y] = tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
    return column.resize((width, height), Image.BILINEAR)


def radial_glow(size: tuple, center: tuple, radius: int, peak: int, color: tuple) -> Image.Image:
    """返回带径向光晕的 RGBA 层（用于叠加到背景上）。"""
    layer = Image.new("RGBA", size, color + (0,))
    mask = Image.new("L", size, 0)
    draw = ImageDraw.Draw(mask)
    cx, cy = center
    steps = 90
    for i in range(steps, 0, -1):
        r = radius * i / steps
        alpha = round(peak * (1 - i / steps) ** 2)
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=alpha)
    mask = mask.filter(ImageFilter.GaussianBlur(radius * 0.12))
    layer.putalpha(mask)
    return layer


def render_mark(size: int) -> Image.Image:
    """按 public/folixa-icon.svg 的形状重绘品牌标记（透明底）。"""
    scale = size / 64.0
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))

    def pt(x: float, y: float) -> tuple[float, float]:
        return (x * scale, y * scale)

    # 主体：圆角矩形 + 右上角斜切
    body = Image.new("L", (size, size), 0)
    draw = ImageDraw.Draw(body)
    left, top = pt(MARK_BOX[0], MARK_BOX[1])
    right, bottom = pt(MARK_BOX[2], MARK_BOX[3])
    draw.rounded_rectangle([left, top, right, bottom], radius=4 * scale, fill=255)
    draw.polygon([pt(39, 6), pt(51, 6), pt(51, 18)], fill=0)

    # 主体渐变填充
    gradient = vertical_gradient(size, size, MARK_TOP, MARK_BOTTOM).convert("RGBA")
    gradient.putalpha(body)
    canvas = Image.alpha_composite(canvas, gradient)

    # 折角
    fold = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    fold_draw = ImageDraw.Draw(fold)
    fold_draw.polygon([pt(39, 6), pt(39, 18), pt(51, 18)], fill=FOLD + (255,))
    canvas = Image.alpha_composite(canvas, fold)

    # 白色文本线
    lines = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    line_draw = ImageDraw.Draw(lines)
    for x1, y, x2 in MARK_LINES:
        p1, p2 = pt(x1, y), pt(x2, y)
        line_draw.line([p1, p2], fill=(255, 255, 255, 255), width=max(1, round(5 * scale)))
        radius = 5 * scale / 2
        for p in (p1, p2):
            line_draw.ellipse([p[0] - radius, p[1] - radius, p[0] + radius, p[1] + radius],
                              fill=(255, 255, 255, 255))
    canvas = Image.alpha_composite(canvas, lines)
    return canvas


def build_og_card(mark: Image.Image) -> Image.Image:
    canvas = vertical_gradient(OG_W, OG_H, NAVY_TOP, NAVY_BOTTOM).convert("RGBA")
    canvas = Image.alpha_composite(canvas, radial_glow((OG_W, OG_H), (980, 40), 620, 120, BLUE))

    draw = ImageDraw.Draw(canvas)
    draw.rectangle([0, OG_H - 8, OG_W, OG_H], fill=BLUE + (255,))

    # 标记在 64×64 画布里只占 11..51 / 6..58，缩放时按主体对齐
    icon = mark.resize((240, 240), Image.LANCZOS)
    alpha = icon.split()[-1]
    shadow = Image.new("RGBA", icon.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.point(lambda v: int(v * 0.45)))
    shadow = shadow.filter(ImageFilter.GaussianBlur(20))
    canvas.alpha_composite(shadow, (96, 276))
    canvas.alpha_composite(icon, (96, 262))

    draw = ImageDraw.Draw(canvas)
    draw.text((330, 196), "Folixa", font=font(FONT_SANS, 104), fill=INK)
    draw.text((332, 322), "智能 PDF 工作空间", font=font(FONT_CJK, 58), fill=BLUE_SOFT)
    draw.line([(334, 420), (900, 420)], fill=BLUE + (110,), width=2)
    draw.text((332, 448), "C++20 · Qt 6 · PDFium · Windows",
              font=font(FONT_SANS_REG, 30), fill=MUTED)
    draw.text((332, 496), "稳定阅读 · 可靠批注 · 带离线引用核验的 AI 问答",
              font=font(FONT_CJK_REG, 30), fill=MUTED)
    return canvas.convert("RGB")


def main() -> None:
    PUBLIC.mkdir(parents=True, exist_ok=True)
    mark = render_mark(1024)

    mark.resize((512, 512), Image.LANCZOS).save(PUBLIC / "folixa-icon-512.png")
    mark.resize((192, 192), Image.LANCZOS).save(PUBLIC / "folixa-icon-192.png")
    mark.resize((180, 180), Image.LANCZOS).save(PUBLIC / "apple-touch-icon.png")
    mark.save(PUBLIC / "folixa-icon-1024.png", optimize=True)

    favicon = mark.resize((64, 64), Image.LANCZOS)
    favicon.save(PUBLIC / "favicon.ico",
                 format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48)])

    build_og_card(mark).save(PUBLIC / "folixa-og.png", optimize=True)

    for name in ("favicon.ico", "apple-touch-icon.png", "folixa-icon-192.png",
                 "folixa-icon-512.png", "folixa-icon-1024.png", "folixa-og.png"):
        path = PUBLIC / name
        print(f"  {name:<24} {path.stat().st_size:>8} bytes")


if __name__ == "__main__":
    print("生成站点资源：")
    main()
