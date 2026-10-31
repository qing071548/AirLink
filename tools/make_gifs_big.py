"""Light-theme looping GIFs for the bright phone deck."""
import math
import os
from PIL import Image, ImageDraw, ImageFont

OUT = r"E:\AZ\deepseek H\gongzuoqu\AirLink\assets"
os.makedirs(OUT, exist_ok=True)

S = 2
BG = (245, 247, 251)
PANEL = (255, 255, 255)
LINE = (226, 232, 242)
ACC = (37, 99, 235)
ACC2 = (124, 58, 237)
TEXT = (15, 23, 42)
SUB = (85, 97, 122)
GREEN = (5, 150, 105)


def font(size, bold=False):
    names = (r"C:\Windows\Fonts\msyhbd.ttc", r"C:\Windows\Fonts\msyh.ttc") if bold \
        else (r"C:\Windows\Fonts\msyh.ttc", r"C:\Windows\Fonts\msyhbd.ttc")
    for p in names:
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


def card(d, box, r=18, fill=PANEL, outline=LINE):
    d.rounded_rectangle([box[0] * S, box[1] * S, box[2] * S, box[3] * S],
                        radius=r * S, fill=fill, outline=outline, width=max(1, S * 2))


def text_at(d, xy, s, size, color=TEXT, anchor="la", bold=False):
    d.text((xy[0] * S, xy[1] * S), s, font=font(size, bold), fill=color, anchor=anchor)


def save(frames, name, dur):
    w, h = frames[0].size
    small = [f.resize((w // S, h // S), Image.LANCZOS) for f in frames]
    small[0].save(os.path.join(OUT, name), save_all=True, append_images=small[1:],
                  duration=dur, loop=0, optimize=True)
    print("wrote", name, len(frames), "frames")


def radar():
    W, H = 760, 440
    frames, N = [], 60
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (10, 10, W - 10, H - 10))
        cx, cy = W // 2, 150
        for k in range(3):
            phase = (f / N + k / 3.0) % 1.0
            r = 26 + phase * 120
            col = mix(ACC, (255, 255, 255), min(1.0, phase * 1.06))
            d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S],
                      outline=col, width=max(1, S * 2))
        d.ellipse([(cx - 26) * S, (cy - 26) * S, (cx + 26) * S, (cy + 26) * S],
                  fill=(232, 240, 255), outline=ACC, width=S * 2)
        text_at(d, (cx, cy - 1), "电脑", 19, ACC, "mm", bold=True)
        if f > 18:
            t = min(1.0, (f - 18) / 10.0)
            bx, by = cx + 148, cy - 4
            a = 0.55 + 0.45 * math.sin(f / 6.0)
            d.ellipse([(bx - 10) * S, (by - 10) * S, (bx + 10) * S, (by + 10) * S],
                      fill=mix((255, 255, 255), GREEN, a * t))
            if t > 0.6:
                text_at(d, (bx + 24, by - 18), "手机 Z10", 18, TEXT, bold=True)
                text_at(d, (bx + 24, by + 8), "已发现 · 同一 Wi-Fi", 15, SUB)
        text_at(d, (30, 340), "自动发现", 21, TEXT, bold=True)
        text_at(d, (30, 372), "同一网络下互相可见，无需输入地址", 16, SUB)
        frames.append(img)
    save(frames, "discovery_big.gif", 45)


def transfer():
    W, H = 760, 440
    frames, N = [], 110
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (10, 10, W - 10, H - 10))
        text_at(d, (36, 36), "项目资料.zip", 22, TEXT, bold=True)
        text_at(d, (36, 72), "240 MB · 电脑 → 手机", 16, SUB)

        p = min(1.0, f / 78.0)
        p = 1 - pow(1 - p, 2.2)
        bx, by, bw = 36, 130, W - 72
        d.rounded_rectangle([bx * S, by * S, (bx + bw) * S, (by + 12) * S],
                            radius=6 * S, fill=(230, 236, 246))
        if p > 0.001:
            d.rounded_rectangle([bx * S, by * S, (bx + max(14, bw * p)) * S, (by + 12) * S],
                                radius=6 * S, fill=ACC)
        pct = int(p * 100)
        text_at(d, (36, 158), f"{pct}%", 32, TEXT, bold=True)
        if p < 1:
            speed = 42 + int(6 * math.sin(f / 4.0))
            text_at(d, (150, 172), f"{speed} MB/s", 17, SUB)
            text_at(d, (W - 36, 172), f"{240 - int(240 * p)} MB 剩余", 17, SUB, "ra")
            text_at(d, (36, 228), "正在传输" + "." * (1 + (f // 10) % 3), 17, ACC)
        else:
            d.rounded_rectangle([36 * S, 214 * S, 168 * S, 254 * S], radius=19 * S,
                                fill=(224, 246, 236), outline=(140, 210, 180), width=S * 2)
            text_at(d, (102, 234), "传输完成", 17, GREEN, "mm", bold=True)
            text_at(d, (186, 234), "已写入手机 AirLink 文件夹", 16, SUB, "lm")

        d.line([36 * S, 330 * S, (W - 36) * S, 330 * S], fill=(232, 238, 248), width=S * 2)
        tx = 36 + (W - 72) * ((f * 2.6) % 1.0)
        d.ellipse([tx * S, 322 * S, (tx + 16) * S, 322 * S + 16 * S], fill=ACC2)
        frames.append(img)
    save(frames, "transfer_big.gif", 40)


def direct():
    W, H = 760, 440
    frames, N = [], 64
    for f in range(N):
        img, d = canvas(W, H)
        card(d, (10, 10, W - 10, H - 10))
        lx, rx, cy = 140, W - 140, 150
        for (x, label, col) in ((lx, "手机", ACC), (rx, "电脑", ACC2)):
            d.rounded_rectangle([(x - 34) * S, (cy - 46) * S, (x + 34) * S, (cy - 4) * S],
                                radius=8 * S, fill=(240, 244, 252), outline=LINE, width=S * 2)
            d.rounded_rectangle([(x - 22) * S, (cy) * S, (x + 22) * S, (cy + 8) * S],
                                radius=3 * S, fill=col)
            text_at(d, (x, cy + 30), label, 17, SUB, "mm", bold=True)

        seg = 10
        for i in range(0, int(rx - lx) - 80, seg * 2):
            d.line([(lx + 44 + i) * S, cy * S, (lx + 44 + i + seg) * S, cy * S],
                   fill=(206, 216, 232), width=S * 2)
        span = (rx - lx) - 96
        for k in range(5):
            t = ((f / N) * 1.4 + k / 5.0) % 1.0
            px = lx + 48 + span * t
            d.ellipse([px * S, (cy - 5) * S, (px + 10) * S, (cy + 5) * S],
                      fill=mix(ACC, ACC2, t))
        for k in range(3):
            t = ((f / N) * 1.1 + k / 3.0 + 0.4) % 1.0
            px = rx - 48 - span * t
            d.ellipse([px * S, (cy + 16) * S, (px + 8) * S, (cy + 24) * S],
                      fill=mix(ACC2, ACC, t))
        text_at(d, (30, 340), "点对点直连", 21, TEXT, bold=True)
        text_at(d, (30, 372), "数据不经过服务器，两端直接传输", 16, SUB)
        frames.append(img)
    save(frames, "direct_big.gif", 45)


radar()
transfer()
direct()
