"""Bright 16:9 deck, no text below 16 pt."""
import os

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

ROOT = r"E:\AZ\deepseek H\gongzuoqu\AirLink"
OUT = os.path.join(ROOT, "AirLink-宣传-横屏-亮色.pptx")
ASSETS = os.path.join(ROOT, "assets")

BG    = RGBColor(0xF5, 0xF7, 0xFB)
BG2   = RGBColor(0xEC, 0xF1, 0xF9)
CARD  = RGBColor(0xFF, 0xFF, 0xFF)
LINE  = RGBColor(0xDC, 0xE4, 0xF0)
INK   = RGBColor(0x0F, 0x17, 0x2A)
SUB   = RGBColor(0x44, 0x50, 0x66)
MUTED = RGBColor(0x6B, 0x76, 0x8C)
ACC   = RGBColor(0x1D, 0x4E, 0xD8)
ACC2  = RGBColor(0x6D, 0x28, 0xD9)
GREEN = RGBColor(0x04, 0x6B, 0x4B)
WARN  = RGBColor(0xB4, 0x45, 0x06)
TINT  = RGBColor(0xE8, 0xF0, 0xFF)
TINTL = RGBColor(0xC7, 0xD8, 0xF8)

FONT = "Microsoft YaHei"
W, H = 13.333, 7.5
X0, CW = 0.75, 11.83
MIN_PT = 16

prs = Presentation()
prs.slide_width = Inches(W)
prs.slide_height = Inches(H)
BLANK = prs.slide_layouts[6]
_sizes = []


def set_font(run, size=None, bold=None, color=None, name=FONT, spacing=None):
    f = run.font
    f.name = name
    if size is not None:
        assert size >= MIN_PT, "font %s below the 16pt floor" % size
        _sizes.append(size)
        f.size = Pt(size)
    if bold is not None:
        f.bold = bold
    if color is not None:
        f.color.rgb = color
    rPr = run._r.get_or_add_rPr()
    for tag in ("a:ea", "a:cs"):
        el = rPr.find(qn(tag))
        if el is None:
            el = rPr.makeelement(qn(tag), {})
            rPr.append(el)
        el.set("typeface", name)
    if spacing is not None:
        rPr.set("spc", str(int(spacing * 100)))


def textbox(slide, x, y, w, h, text, size=17, color=INK, bold=False,
            align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, line=1.25, spacing=None):
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.margin_left = tf.margin_right = tf.margin_top = tf.margin_bottom = 0
    tf.vertical_anchor = anchor
    p = tf.paragraphs[0]
    p.alignment = align
    p.line_spacing = line
    r = p.add_run()
    r.text = text
    set_font(r, size, bold, color, spacing=spacing)
    return tb


def rect(slide, x, y, w, h, fill=None, line=None, radius=None, lw=1.0):
    kind = MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE
    sh = slide.shapes.add_shape(kind, Inches(x), Inches(y), Inches(w), Inches(h))
    if radius:
        sh.adjustments[0] = radius
    if fill is None:
        sh.fill.background()
    else:
        sh.fill.solid()
        sh.fill.fore_color.rgb = fill
    if line is None:
        sh.line.fill.background()
    else:
        sh.line.color.rgb = line
        sh.line.width = Pt(lw)
    sh.shadow.inherit = False
    return sh


def shape_text(sh, text, size=16, color=INK, bold=False, align=PP_ALIGN.CENTER):
    tf = sh.text_frame
    tf.margin_left = tf.margin_right = Inches(0.04)
    tf.margin_top = tf.margin_bottom = 0
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = align
    r = p.add_run()
    r.text = text
    set_font(r, size, bold, color)


def gradient_bg(slide, top=BG, bottom=BG2):
    sh = rect(slide, -0.05, -0.05, W + 0.1, H + 0.1, fill=top)
    sh.fill.gradient()
    sh.fill.gradient_angle = 90
    st = sh.fill.gradient_stops
    st[0].color.rgb = top
    st[0].position = 0.0
    st[1].color.rgb = bottom
    st[1].position = 1.0
    return sh


def new_slide(top=BG, bottom=BG2):
    s = prs.slides.add_slide(BLANK)
    gradient_bg(s, top, bottom)
    return s


def footer(slide, idx):
    textbox(slide, X0, 7.02, 5.0, 0.34, "AirLink · 隔空快传", 16, MUTED)
    textbox(slide, W - 1.45, 7.02, 0.7, 0.34, "%02d" % idx, 16, MUTED, align=PP_ALIGN.RIGHT)


def head(slide, kick, ttl, lead_text=None, kick_y=0.50, title_y=0.86, lead_y=1.72):
    textbox(slide, X0, kick_y, 7.0, 0.34, kick, 16, ACC, bold=True, spacing=1.2)
    textbox(slide, X0, title_y, CW, 0.72, ttl, 34, INK, bold=True, line=1.2)
    if lead_text:
        textbox(slide, X0, lead_y, CW, 0.42, lead_text, 17, SUB, line=1.4)


def card(slide, x, y, w, h, fill=CARD, line=LINE, radius=0.06):
    return rect(slide, x, y, w, h, fill=fill, line=line, radius=radius)


def transition(slide, kind="fade"):
    from lxml import etree
    xml = ('<p:transition xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
           'spd="med"><p:%s/></p:transition>' % kind)
    slide._element.append(etree.fromstring(xml))


# =================================================================== 01 封面
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xE9, 0xEF, 0xFA))
badge = rect(s, X0, 1.10, 2.52, 0.50, fill=TINT, line=TINTL, radius=0.5)
shape_text(badge, "Windows · Android", 16, ACC, bold=True)

textbox(s, X0, 1.78, 8.4, 1.20, "隔空快传", 58, INK, bold=True, line=1.05)
textbox(s, X0, 3.12, 8.4, 0.46, "AirLink", 22, ACC, bold=True, spacing=3)
textbox(s, X0, 3.74, 8.4, 0.46, "两台设备之间，文件直达。", 21, INK)
textbox(s, X0, 4.36, 6.4, 0.9,
        "一个 exe、一个 apk，双击即用。\n不需要账号，也不经过任何服务器。",
        17, SUB, line=1.55)

# right-hand balance block: the two product sizes, at a glance
card(s, 8.52, 1.80, 4.06, 3.72, fill=CARD, line=LINE)
rect(s, 8.52, 1.80, 0.09, 3.72, fill=ACC)
textbox(s, 8.86, 2.02, 3.4, 0.40, "极致轻量", 17, ACC, bold=True)
for k, (num, unit, label) in enumerate([("699", "KB", "Windows 单文件"),
                                        ("333", "KB", "Android 安装包")]):
    yy = 2.56 + k * 1.46
    textbox(s, 8.86, yy, 2.4, 0.80, num, 44, INK, bold=True)
    textbox(s, 8.86 + 1.62, yy + 0.34, 1.0, 0.40, unit, 18, ACC, bold=True)
    textbox(s, 8.86, yy + 0.92, 3.4, 0.40, label, 16, MUTED)

pills = ["局域网直连", "广域网可达", "无账号", "无服务器"]
for i, p in enumerate(pills):
    px = X0 + i * 2.98
    sh = rect(s, px, 5.62, 2.78, 0.56, fill=CARD, line=LINE, radius=0.5)
    shape_text(sh, p, 17, SUB, bold=True)

rect(s, X0, 6.52, CW, 0.014, fill=RGBColor(0xD4, 0xDE, 0xEE))
textbox(s, X0, 6.72, 7.1, 0.4, "单文件运行 · 无运行时依赖 · 双端同协议", 16, MUTED)

# =================================================================== 02 痛点
s = new_slide()
head(s, "THE PROBLEM", "传个文件，为什么这么难？",
     "共同点：中间永远隔着别人。", lead_y=1.66)

pains = [("线", "找不到数据线"), ("压", "聊天软件会压缩"),
         ("等", "网盘要排队"), ("墙", "跨网络就断")]
cw = 2.86
for i, (glyph, t) in enumerate(pains):
    x = X0 + i * (cw + 0.14)
    card(s, x, 2.30, cw, 2.40)
    ring = rect(s, x + 0.30, 2.62, 0.80, 0.80,
                fill=RGBColor(0xFD, 0xEF, 0xE6), line=RGBColor(0xF2, 0xCF, 0xB6), radius=0.5)
    shape_text(ring, glyph, 24, WARN, bold=True)
    textbox(s, x + 0.30, 3.62, cw - 0.6, 0.86, t, 19, INK, bold=True, line=1.35)

bar = rect(s, X0, 5.02, CW, 1.20, fill=TINT, line=TINTL, radius=0.1)
textbox(s, X0 + 0.50, 5.36, CW - 1.0, 0.5,
        "这四件事的共同点：文件必须经过一个第三方。AirLink 把它去掉。", 19, INK, bold=True)
footer(s, 2)

# =================================================================== 03 定义
s = new_slide()
head(s, "WHAT IT IS", "一个 exe，一个 apk，两台设备直接对话",
     "没有中间商：不走网盘，不过聊天软件。", lead_y=1.66)

panels = [("它不做的事", ["不需要账号", "不需要服务器", "不需要运行时"], WARN, X0),
          ("它做的事", ["直接发现", "直接传输", "直接浏览"], GREEN, X0 + 6.02)]
for title_t, items, accent, px in panels:
    card(s, px, 2.24, 5.81, 4.02)
    rect(s, px, 2.24, 0.09, 4.02, fill=accent)
    textbox(s, px + 0.42, 2.50, 5.0, 0.42, title_t, 17, accent, bold=True)
    for j, it in enumerate(items):
        yy = 3.14 + j * 1.02
        rect(s, px + 0.44, yy + 0.14, 0.15, 0.15, fill=accent, radius=0.5)
        textbox(s, px + 0.76, yy, 4.7, 0.48, it, 21, INK, bold=True)
footer(s, 3)

# =================================================================== 04 形态
s = new_slide()
head(s, "TWO BUILDERS", "两个程序，都是单文件", "小到可以直接发过去。", lead_y=1.66)

for i, (os_name, size, unit, facts, accent) in enumerate([
    ("Windows", "699", "KB", ["单个 exe，静态链接", "无 DLL 依赖，拷走即用", "Win7 及以上 64 位"], ACC),
    ("Android", "333", "KB", ["纯 Java，不用 AndroidX", "minSdk 21（Android 5.0+）", "已签名，可直接安装"], ACC2),
]):
    px = X0 + i * 6.02
    card(s, px, 2.24, 5.81, 4.02)
    textbox(s, px + 0.46, 2.50, 3.0, 0.40, os_name, 17, accent, bold=True, spacing=1.5)
    textbox(s, px + 0.46, 3.00, 3.2, 1.10, size, 60, INK, bold=True)
    textbox(s, px + 2.62, 3.58, 1.2, 0.44, unit, 20, MUTED)
    for j, f in enumerate(facts):
        textbox(s, px + 0.46, 4.36 + j * 0.54, 5.0, 0.44, "·  " + f, 17, SUB)
footer(s, 4)

# =================================================================== 05 能力
s = new_slide()
head(s, "CAPABILITIES", "六件事，一件不少", "从发现到保活，构成一条完整链路。", lead_y=1.66)

caps = [("01", "自动发现", "UDP 广播，自动入列"), ("02", "扫码连接", "扫一下就连上"),
        ("03", "广域网直连", "IPv6 免端口映射"), ("04", "双向浏览", "两端目录互相可见"),
        ("05", "记录同步", "一开始传就入记录"), ("06", "熄屏保活", "锁屏也不掉线")]
cw2, ch2 = 3.85, 1.92
for i, (num, t, d) in enumerate(caps):
    col, row = i % 3, i // 3
    x = X0 + col * (cw2 + 0.14)
    y = 2.22 + row * (ch2 + 0.18)
    card(s, x, y, cw2, ch2)
    textbox(s, x + 0.34, y + 0.26, 1.0, 0.34, num, 16, ACC, bold=True)
    textbox(s, x + 0.34, y + 0.66, cw2 - 0.68, 0.44, t, 20, INK, bold=True)
    textbox(s, x + 0.34, y + 1.18, cw2 - 0.68, 0.5, d, 16, SUB)
footer(s, 5)


def gif_page(idx, kick, ttl, lead_text, bullets, gif, caption):
    s = new_slide()
    head(s, kick, ttl, lead_text, lead_y=1.66)
    for i, (t, d) in enumerate(bullets):
        y = 2.40 + i * 1.12
        rect(s, X0, y + 0.14, 0.15, 0.15, fill=ACC, radius=0.5)
        textbox(s, X0 + 0.38, y, 5.2, 0.46, t, 20, INK, bold=True)
        textbox(s, X0 + 0.38, y + 0.48, 5.2, 0.46, d, 16, SUB)
    s.shapes.add_picture(os.path.join(ASSETS, gif), Inches(6.60), Inches(2.24), width=Inches(6.0))
    textbox(s, 6.60, 5.98, 6.0, 0.36, caption, 16, MUTED)
    footer(s, idx)
    return s


# =================================================================== 06
gif_page(6, "STEP 01 · DISCOVERY", "打开就能看见对方", "不用问 IP，不用扫端口，也不用先加好友。",
         [("广播探询", "定时向局域网发一条 UDP 报文"), ("即时应答", "对方把名称、地址、端口回给你")],
         "discovery_big.gif", "动图：设备发现过程（放映时循环播放）")

# =================================================================== 07
s = new_slide()
head(s, "STEP 02 · PAIRING", "扫一下就配好了", "二维码里装着完整连接信息。", lead_y=1.66)

card(s, X0, 2.24, 2.90, 2.90, fill=RGBColor(0xF2, 0xF6, 0xFD), line=TINTL, radius=0.08)
textbox(s, X0 + 0.1, 3.50, 2.7, 0.44, "二维码", 17, ACC, bold=True, align=PP_ALIGN.CENTER)

textbox(s, X0 + 3.20, 2.30, 8.4, 0.44, "两种码，一个入口", 20, INK, bold=True)
textbox(s, X0 + 3.20, 2.86, 8.4, 1.0,
        "局域网码：airlink://192.168.1.5:45678\n广域网码：后面加 ?code= 配对码",
        16, SUB, line=1.8)
textbox(s, X0 + 3.20, 4.10, 8.4, 0.9,
        "手机按内容自动区分，不需要你理解区别。", 17, SUB, line=1.5)

card(s, X0 + 3.20, 5.06, 8.63, 1.24, fill=TINT, line=TINTL)
textbox(s, X0 + 3.55, 5.26, 8.0, 0.4, "为什么用二维码", 17, INK, bold=True)
textbox(s, X0 + 3.55, 5.68, 8.0, 0.4, "IPv6 地址有 39 个字符，手输几乎不可能。", 17, SUB)
footer(s, 7)

# =================================================================== 08
gif_page(8, "STEP 03 · WIDE AREA", "不在同一个网络，也能直连",
         "优先 IPv6 直连；没有则退回端口映射。",
         [("IPv6 直连", "地址全球可达，没有 NAT 需要穿透"), ("自动兜底", "无 IPv6 时用 UPnP 请路由器开端口")],
         "direct_big.gif", "动图：两端点对点传输（放映时循环播放）")

# =================================================================== 09
s = new_slide()
head(s, "SECURITY", "开在公网上，也不是谁都能进", "把端口暴露到互联网是必要代价，所以必须有闸门。", lead_y=1.66)

steps = [("非内网来源", "地址不在 10. / 192.168. 段内"),
         ("必须出示配对码", "6 位数字，与电脑上一致"),
         ("校验通过", "该地址 10 分钟内免检")]
cw3 = 3.85
for i, (t, d) in enumerate(steps):
    x = X0 + i * (cw3 + 0.14)
    card(s, x, 2.26, cw3, 2.30)
    num = rect(s, x + 0.32, 2.58, 0.78, 0.78, fill=TINT, line=TINTL, radius=0.5)
    shape_text(num, str(i + 1), 19, ACC, bold=True)
    textbox(s, x + 0.32, 3.52, cw3 - 0.64, 0.44, t, 19, INK, bold=True)
    textbox(s, x + 0.32, 4.02, cw3 - 0.64, 0.5, d, 16, SUB, line=1.4)

card(s, X0, 4.90, CW, 1.40, fill=RGBColor(0xE8, 0xF7, 0xF0), line=RGBColor(0xB6, 0xE0, 0xCB))
textbox(s, X0 + 0.50, 5.16, 6.0, 0.44, "内网不受影响", 19, GREEN, bold=True)
textbox(s, X0 + 0.50, 5.64, CW - 1.0, 0.44,
        "同一局域网仍然免密直连——安全加固不该让日常使用变麻烦。", 17, SUB)
footer(s, 9)

# =================================================================== 10
s = new_slide()
head(s, "BROWSE", "两边的文件，都像在自己电脑上", "同一套目录树，双向可读。", lead_y=1.66)

cols = [("手机看电脑", ["进入任意目录", "下载文件到手机", "删除对方文件"], ACC),
        ("电脑看手机", ["浏览手机存储", "取回照片与文档", "清理不需要的文件"], ACC2)]
for i, (t, items, accent) in enumerate(cols):
    x = X0 + i * 6.02
    card(s, x, 2.26, 5.81, 3.30)
    rect(s, x, 2.26, 0.09, 3.30, fill=accent)
    textbox(s, x + 0.44, 2.56, 5.0, 0.44, t, 20, INK, bold=True)
    for j, it in enumerate(items):
        textbox(s, x + 0.44, 3.24 + j * 0.62, 5.0, 0.44, "·  " + it, 17, SUB)

card(s, X0, 5.82, CW, 1.10, fill=TINT, line=TINTL)
textbox(s, X0 + 0.50, 6.06, CW - 1.0, 0.5,
        "大文件按块流式传输，不占内存；中断后可以重新发起。", 17, SUB)
footer(s, 10)

# =================================================================== 11
gif_page(11, "TRANSFER", "一开始传，记录里就有了", "不用等结束才知道发生了什么。",
         [("即时入列", "启动瞬间写入一条「传输中」记录"), ("实时进度", "进度、速度、剩余量同步刷新")],
         "transfer_big.gif", "动图：传输进度（放映时循环播放）")

# =================================================================== 12
s = new_slide()
head(s, "ALWAYS ON", "熄屏之后，链路还在", "锁屏被系统掐断，是这类工具最常见的死法。", lead_y=1.66)

items = [("前台服务", ACC), ("唤醒锁", ACC), ("Wi-Fi 锁", ACC2), ("锁屏重武装", ACC2)]
cw4 = 2.86
for i, (t, accent) in enumerate(items):
    x = X0 + i * (cw4 + 0.14)
    card(s, x, 2.30, cw4, 2.36)
    rect(s, x, 2.30, 0.09, 2.36, fill=accent)
    textbox(s, x + 0.34, 3.06, cw4 - 0.6, 0.86, t, 20, INK, bold=True, line=1.3)

textbox(s, X0, 5.02, CW, 0.9,
        "设置页能直接看到最近一次保活是否成功，出问题可以自己判断。", 17, SUB, line=1.5)
footer(s, 12)

# =================================================================== 13
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xEC, 0xF1, 0xF9))
head(s, "BY THE NUMBERS", "几个能对得上的数字", "全部取自实际构建产物，不含估算。", lead_y=1.66)

stats = [("699", "KB", "电脑端体积"), ("333", "KB", "手机端体积"),
         ("0", "个", "第三方运行时依赖"), ("6", "字节", "协议头开销"),
         ("2", "个", "网络端口"), ("10", "分钟", "配对码有效期")]
cw5, ch5 = 3.85, 1.98
for i, (num, unit, label) in enumerate(stats):
    col, row = i % 3, i // 3
    x = X0 + col * (cw5 + 0.14)
    y = 2.22 + row * (ch5 + 0.18)
    card(s, x, y, cw5, ch5)
    textbox(s, x + 0.34, y + 0.36, 3.0, 0.9, num, 48, INK, bold=True)
    textbox(s, x + 0.34 + len(num) * 0.38 + 0.08, y + 0.72, 1.2, 0.4, unit, 18, ACC, bold=True)
    textbox(s, x + 0.34, y + 1.36, cw5 - 0.68, 0.4, label, 17, MUTED)
footer(s, 13)

# =================================================================== 14
s = new_slide()
head(s, "ARCHITECTURE", "三层结构，两端逐字节一致", "电脑和手机共用同一套协议定义。", lead_y=1.66)

layers = [("发现层", "UDP 45679", "广播探询与应答，仅用于局域网可见", ACC),
          ("传输层", "TCP 45678", "ALFT 魔数 + 版本 + 类型，整数一律小端", ACC2),
          ("应用层", "12 类消息", "推文件 · 列目录 · 取文件 · 心跳 · 会话 · 记录同步 · 配对认证", GREEN)]
y = 2.22
for name, port, desc, accent in layers:
    card(s, X0, y, CW, 1.28)
    rect(s, X0, y, 0.09, 1.28, fill=accent)
    textbox(s, X0 + 0.44, y + 0.44, 2.2, 0.44, name, 20, INK, bold=True)
    tag = rect(s, X0 + 2.90, y + 0.42, 2.05, 0.46, fill=TINT, line=TINTL, radius=0.5)
    shape_text(tag, port, 16, accent, bold=True)
    textbox(s, X0 + 5.20, y + 0.44, 6.2, 0.45, desc, 17, SUB)
    y += 1.50
footer(s, 14)

# =================================================================== 15
s = new_slide()
head(s, "COMPARISON", "换一种方式看它", "没有一种工具适合所有场景。", lead_y=1.66)

cols = ["维度", "AirLink", "聊天软件", "网盘", "数据线"]
col_x = [X0, X0 + 2.55, X0 + 4.60, X0 + 7.00, X0 + 9.40]
col_w = [2.55, 2.05, 2.40, 2.40, 2.40]
rows = [("需要账号", "否", "是", "是", "否"),
        ("需要互联网", "否", "是", "是", "否"),
        ("经过第三方服务器", "否", "是", "是", "否"),
        ("压缩 / 转码", "不压缩", "会压缩", "不压缩", "不压缩"),
        ("跨网络传输", "支持", "支持", "支持", "不支持"),
        ("免安装", "单文件", "需安装", "需安装", "需驱动")]
hy = 2.10
for i, c in enumerate(cols):
    fill = RGBColor(0xD8, 0xE4, 0xFB) if i == 1 else RGBColor(0xEC, 0xF1, 0xF9)
    sh = rect(s, col_x[i], hy, col_w[i], 0.50, fill=fill, line=None, radius=0.14)
    shape_text(sh, c, 16, ACC if i == 1 else SUB, bold=True)

for r, (label, *vals) in enumerate(rows):
    y = hy + 0.60 + r * 0.58
    if r % 2 == 0:
        rect(s, X0, y - 0.07, CW, 0.54, fill=RGBColor(0xEE, 0xF3, 0xFB), line=None)
    textbox(s, col_x[0] + 0.12, y + 0.06, col_w[0] - 0.16, 0.42, label, 16, SUB)
    for c, v in enumerate(vals):
        good = (c == 0 and v in ("否", "不压缩", "支持", "单文件"))
        col = GREEN if good else (INK if c == 0 else MUTED)
        textbox(s, col_x[c + 1] + 0.08, y + 0.06, col_w[c + 1] - 0.10, 0.42,
                v, 16, col, bold=good)

textbox(s, X0, 6.18, CW, 0.5,
        "如果目标只是把文件从 A 送到 B，中间那一层其实可以不存在。", 17, SUB)
footer(s, 15)

# =================================================================== 16
s = new_slide()
head(s, "USE CASES", "什么时候用得上", "两台设备在同一个屏幕前，或同一个人在两地。", lead_y=1.66)

scenes = ["办公室换机", "照片回传", "出差在外", "断网环境"]
cw6 = 2.86
for i, t in enumerate(scenes):
    x = X0 + i * (cw6 + 0.14)
    card(s, x, 2.30, cw6, 2.36)
    num = rect(s, x + 0.32, 2.62, 0.80, 0.80, fill=TINT, line=TINTL, radius=0.5)
    shape_text(num, str(i + 1), 19, ACC, bold=True)
    textbox(s, x + 0.32, 3.60, cw6 - 0.64, 0.86, t, 20, INK, bold=True, line=1.3)
footer(s, 16)

# =================================================================== 17
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xE9, 0xEF, 0xFA))
textbox(s, X0, 1.62, 11.5, 0.95, "两台设备之间，", 42, INK, bold=True, line=1.2)
textbox(s, X0, 2.52, 11.5, 0.95, "不该有中间商。", 42, ACC, bold=True, line=1.2)
textbox(s, X0, 3.52, 9.0, 0.48, "小、快、直——就这三件事。", 19, SUB)

feats = [("小", "699 KB / 333 KB"), ("快", "点对点直连"), ("直", "不经任何服务器")]
cw7 = 3.85
for i, (k, v) in enumerate(feats):
    x = X0 + i * (cw7 + 0.14)
    card(s, x, 4.32, cw7, 1.44)
    textbox(s, x + 0.34, 4.56, 0.9, 0.9, k, 30, ACC, bold=True)
    textbox(s, x + 1.24, 4.78, cw7 - 1.5, 0.44, v, 17, SUB)

rect(s, X0, 6.24, CW, 0.014, fill=RGBColor(0xD4, 0xDE, 0xEE))
textbox(s, X0, 6.46, 7.0, 0.44, "AirLink · 隔空快传　Windows 与 Android 双端就绪", 16, MUTED)

NOTES = [
 "开场定位：让两台设备直接对话，电脑一个 exe、手机一个 apk，中间不需要服务器。",
 "四个场景对应线、压缩、等待、跨网。共同点：文件必须经过第三方。",
 "三个不做的边界，三个直接的主线。",
 "体积是结果：界面自绘、零框架、零图片资源。",
 "六项能力构成一条完整链路。",
 "局域网走 UDP 广播，只在同一广播域有效，所以还需要广域网方案。",
 "二维码里是完整连接串，手机自动判断走哪条路径。",
 "优先 IPv6，无则退回 UPnP 端口映射。",
 "非内网来源必须先出示配对码，通过后十分钟免检。",
 "双向浏览，大文件按块流式传输。",
 "记录是传输过程的一部分，不是事后日志。",
 "四层保活手段叠加，设置页可观测。",
 "数字都可以核对，不放无法验证的宣称。",
 "三层结构，两端共用一份协议定义。",
 "重点是：中间那一层其实可以不存在。",
 "四个典型场景。",
 "收尾：两台设备之间不该有中间商。",
]
for sl, note in zip(prs.slides, NOTES):
    sl.notes_slide.notes_text_frame.text = note

kinds = ["fade", "push", "wipe", "zoom", "cover", "fade", "push", "fade", "wipe"]
for i, sl in enumerate(prs.slides):
    transition(sl, kinds[i % len(kinds)])

prs.save(OUT)
print("saved", OUT)
print("runs:", len(_sizes), "min pt:", min(_sizes), "max pt:", max(_sizes))
