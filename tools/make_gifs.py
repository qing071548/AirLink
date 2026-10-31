"""Generate the looping GIFs used in the AirLink promo deck."""
import math
import os
from PIL import Image, ImageDraw, ImageFont

OUT = r"E:\AZ\deepseek H\gongzuoqu\AirLink\assets"
os.makedirs(OUT, exist_ok=True)

S = 2  # supersample factor
BG = (11, 14, 20)
PANEL = (18, 22, 31)
LINE = (35, 44, 59)
ACC = (94, 145, 255)
ACC2 = (126, 92, 255)
TEXT = (242, 245, 250)
SUB = (154, 164, 184)
GREEN = (74, 222, 128)


def font(size):
    for p in (r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\msyhbd.ttc",
              r"C:\Windows\Fonts\simhei.ttf"):
        if os.path.exists(p):
            try:
                return ImageFont.truetype(p, size * S)
            except Exception:
                continue
    return ImageFont.load_default()


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def canvas(w, h):
    img = Image.new("RGB", (w * S, h * S), BG)
    return img, ImageDraw.Draw(img)


def card(d, box, r=14, fill=PANEL, outline=LINE):
    d.rounded_rectangle([box[0] * S, box[1] * S, box[2] * S, box[3] * S],
                        radius=r * S, fill=fill, outline=outline, width=max(1, S))


def text_at(d, xy, s, size, color=TEXT, anchor="la"):
    d.text((xy[0] * S, xy[1] * S), s, font=font(size), fill=color, anchor=anchor)


def save(frames, name, dur):
    w, h = frames[0].size
    small = [f.resize((w // S, h // S), Image.LANCZOS) for f in frames]
    small[0].save(os.path.join(OUT, name), save_all=True, append_images=small[1:],
                  duration=dur, loop=0, optimize=True)
    print("wrote", name, len(frames), "frames")


# ---------------------------------------------------------------- 1. discovery
def radar():
    W, H = 640, 320
    frames = []
    N = 60
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (16, 16, W - 16, H - 16), r=16)

        cx, cy = W // 2, 132
        # expanding rings
        for k in range(3):
            phase = (f / N + k / 3.0) % 1.0
            r = 26 + phase * 118
            col = mix(ACC, PANEL, min(1.0, phase * 1.05))
            d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
                      outline=col, width=max(1, S))
        # device hub
        d.ellipse([(cx - 24) * S, (cy - 24) * S, (cx + 24) * S, (cy + 24) * S],
                  fill=(28, 38, 60), outline=ACC, width=S)
        text_at(d, (cx, cy - 1), "PC", 13, TEXT, "mm")

        # a phone blip that appears and settles
        if f > 18:
            t = min(1.0, (f - 18) / 10.0)
            bx = int(cx + 150)
            by = cy - 4
            a = 0.55 + 0.45 * math.sin(f / 6.0)
            glow = mix(PANEL, GREEN, a * t)
            d.ellipse([(bx - 9) * S, (by - 9) * S, (bx + 9) * S, (by + 9) * S], fill=glow)
            if t > 0.6:
                text_at(d, (bx + 18, by - 10), "手机 Z10", 12, TEXT)
                text_at(d, (bx + 18, by + 7), "已发现 · 同一 Wi-Fi", 10, SUB)

        text_at(d, (32, 246), "自动发现", 15, TEXT)
        text_at(d, (32, 270), "同一网络下互相可见，无需输入任何地址", 10.5, SUB)
        frames.append(img)
    save(frames, "discovery.gif", 45)


# ---------------------------------------------------------------- 2. transfer
def transfer():
    W, H = 640, 320
    frames = []
    N = 110
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (16, 16, W - 16, H - 16), r=16)
        text_at(d, (36, 36), "项目资料.zip", 15, TEXT)
        text_at(d, (36, 60), "240 MB · 电脑 → 手机", 10.5, SUB)

        p = min(1.0, f / 78.0)
        p = 1 - pow(1 - p, 2.2)          # ease out
        bar_x, bar_y, bar_w = 36, 108, W - 72
        d.rounded_rectangle([bar_x * S, bar_y * S, (bar_x + bar_w) * S, (bar_y + 10) * S],
                            radius=5 * S, fill=(30, 37, 50))
        if p > 0.001:
            d.rounded_rectangle([bar_x * S, bar_y * S,
                                 (bar_x + max(12, bar_w * p)) * S, (bar_y + 10) * S],
                                radius=5 * S, fill=ACC)

        pct = int(p * 100)
        text_at(d, (36, 132), f"{pct}%", 22, TEXT)
        speed = 0 if p >= 1 else max(0, 42 + int(6 * math.sin(f / 4.0)))
        if p < 1:
            text_at(d, (110, 142), f"{speed} MB/s", 11.5, SUB)
            text_at(d, (W - 36, 142), f"{240 - int(240 * p)} MB 剩余", 11, SUB, "ra")

        if p >= 1:
            d.rounded_rectangle([36 * S, 172 * S, 118 * S, 198 * S], radius=13 * S,
                                fill=(22, 48, 36), outline=(38, 92, 62), width=S)
            text_at(d, (77, 185), "传输完成", 11, GREEN, "mm")
            text_at(d, (130, 185), "已写入手机 AirLink 文件夹", 10.5, SUB, "lm")
        else:
            dots = "." * (1 + (f // 10) % 3)
            text_at(d, (36, 185), "正在传输" + dots, 11, ACC)

        # a travelling packet along a thin track
        tx = 36 + (bar_w) * ((f * 2.6) % 1.0)
        d.ellipse([tx * S, 232 * S, (tx + 7) * S, (232 + 7) * S], fill=ACC2)
        d.line([36 * S, 236 * S, (W - 36) * S, 236 * S], fill=(32, 39, 52), width=max(1, S))
        frames.append(img)
    save(frames, "transfer.gif", 40)


# ---------------------------------------------------------------- 3. direct
def direct():
    W, H = 640, 320
    frames = []
    N = 64
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (16, 16, W - 16, H - 16), r=16)

        lx, rx, cy = 120, W - 120, 118
        # two devices
        for (x, label) in ((lx, "手机"), (rx, "电脑")):
            d.rounded_rectangle([(x - 30) * S, (cy - 44) * S, (x + 30) * S, (cy + 44) * S],
                                radius=10 * S, fill=(26, 32, 45), outline=LINE, width=S)
            text_at(d, (x, cy + 66), label, 11, SUB, "mm")
        text_at(d, (lx, cy - 2), "▢", 20, ACC, "mm")
        text_at(d, (rx, cy - 2), "▣", 20, ACC2, "mm")

        # dashed link
        seg = 10
        for i in range(0, int(rx - lx), seg * 2):
            d.line([(lx + 42 + i) * S, cy * S, (lx + 42 + i + seg) * S, cy * S],
                   fill=(44, 54, 72), width=max(1, S * 2))

        # packets both directions
        span = (rx - lx) - 96
        for k in range(5):
            t = ((f / N) * 1.4 + k / 5.0) % 1.0
            px = lx + 48 + span * t
            col = mix(ACC, ACC2, t)
            d.ellipse([px * S, (cy - 4) * S, (px + 8) * S, (cy + 4) * S], fill=col)
        for k in range(3):
            t = ((f / N) * 1.1 + k / 3.0 + 0.4) % 1.0
            px = rx - 48 - span * t
            d.ellipse([px * S, (cy + 14) * S, (px + 6) * S, (cy + 20) * S],
                      fill=mix(ACC2, ACC, t))

        text_at(d, (32, 232), "点对点直连", 15, TEXT)
        text_at(d, (32, 256), "数据不经过任何服务器，两端之间直接传输", 10.5, SUB)

        # a soft pulse to make the link feel alive
        a = 0.35 + 0.25 * math.sin(f / 5.0)
        d.line([(lx + 42) * S, cy * S, (rx - 42) * S, cy * S],
               fill=mix(PANEL, ACC, a * 0.35), width=max(1, S))
        frames.append(img)
    save(frames, "direct.gif", 45)


radar()
transfer()
direct()
