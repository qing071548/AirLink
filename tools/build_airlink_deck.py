"""Build the AirLink promo deck (17 slides, dark tech theme)."""
import copy
import os

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Emu, Inches, Pt

ROOT = r"E:\AZ\deepseek H\gongzuoqu\AirLink"
OUT = os.path.join(ROOT, "AirLink-宣传.pptx")
ASSETS = os.path.join(ROOT, "assets")

BG      = RGBColor(0x0B, 0x0E, 0x14)
PANEL   = RGBColor(0x12, 0x16, 0x1F)
PANEL2  = RGBColor(0x16, 0x1B, 0x26)
LINE    = RGBColor(0x23, 0x2C, 0x3B)
ACC     = RGBColor(0x5E, 0x91, 0xFF)
ACC2    = RGBColor(0x7E, 0x5C, 0xFF)
TEXT    = RGBColor(0xF2, 0xF5, 0xFA)
SUB     = RGBColor(0x9A, 0xA4, 0xB8)
MUTED   = RGBColor(0x5F, 0x6A, 0x80)
GREEN   = RGBColor(0x4A, 0xDE, 0x80)
WARN    = RGBColor(0xFF, 0xB0, 0x6A)

FONT = "Microsoft YaHei"
W, H = 13.333, 7.5

prs = Presentation()
prs.slide_width = Inches(W)
prs.slide_height = Inches(H)
BLANK = prs.slide_layouts[6]


# ------------------------------------------------------------------ primitives
def set_font(run, size=None, bold=None, color=None, name=FONT, spacing=None):
    f = run.font
    f.name = name
    if size is not None:
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


def textbox(slide, x, y, w, h, text, size=14, color=TEXT, bold=False,
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
    shape_type = MSO_SHAPE.ROUNDED_RECTANGLE if radius else MSO_SHAPE.RECTANGLE
    sh = slide.shapes.add_shape(shape_type, Inches(x), Inches(y), Inches(w), Inches(h))
    if radius:
        # radius is a fraction of the shorter side
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
    if sh.has_text_frame:
        sh.text_frame.word_wrap = True
    return sh


def shape_text(sh, text, size=13, color=TEXT, bold=False, align=PP_ALIGN.CENTER):
    tf = sh.text_frame
    tf.margin_left = tf.margin_right = Inches(0.1)
    tf.margin_top = tf.margin_bottom = 0
    tf.vertical_anchor = MSO_ANCHOR.MIDDLE
    p = tf.paragraphs[0]
    p.alignment = align
    r = p.add_run()
    r.text = text
    set_font(r, size, bold, color)


def gradient_bg(slide, top=BG, bottom=RGBColor(0x0E, 0x12, 0x1C)):
    sh = rect(slide, -0.05, -0.05, W + 0.1, H + 0.1, fill=top)
    sh.fill.gradient()
    sh.fill.gradient_angle = 90
    stops = sh.fill.gradient_stops
    stops[0].color.rgb = top
    stops[0].position = 0.0
    stops[1].color.rgb = bottom
    stops[1].position = 1.0
    return sh


def glow(slide, x, y, w, h, color=ACC, alpha=0.10):
    """Soft colour wash: a rounded rect with a transparent gradient."""
    sh = rect(slide, x, y, w, h, fill=color, radius=0.5)
    sh.fill.gradient()
    sh.fill.gradient_angle = 45
    stops = sh.fill.gradient_stops
    stops[0].color.rgb = color
    stops[1].color.rgb = BG
    for st, a in zip(stops, (int(alpha * 100000), 0)):
        srgb = st.color._xFill.find(qn("a:srgbClr"))
        el = srgb.makeelement(qn("a:alpha"), {"val": str(a)})
        srgb.append(el)
    return sh


def footer(slide, idx, label="AirLink · 隔空快传"):
    textbox(slide, 0.85, 6.92, 5.0, 0.3, label, 9, MUTED)
    textbox(slide, W - 1.6, 6.92, 0.8, 0.3, f"{idx:02d}", 9, MUTED, align=PP_ALIGN.RIGHT)


def kicker(slide, text, y=0.62, color=ACC):
    textbox(slide, 0.85, y, 9.0, 0.3, text, 10.5, color, bold=True, spacing=1.6)


def title(slide, text, y=0.95, size=33, color=TEXT):
    textbox(slide, 0.85, y, 11.4, 0.75, text, size, color, bold=True)


def lead(slide, text, y=1.78, w=9.6, color=SUB, size=13):
    textbox(slide, 0.85, y, w, 0.6, text, size, color, line=1.5)


def card(slide, x, y, w, h, fill=PANEL, line=LINE, radius=0.06):
    return rect(slide, x, y, w, h, fill=fill, line=line, radius=radius)


def bullet(slide, x, y, w, text, sub=None, dot=ACC, size=12.5):
    d = rect(slide, x, y + 0.075, 0.09, 0.09, fill=dot, radius=0.5)
    textbox(slide, x + 0.26, y, w, 0.32, text, size, TEXT)
    if sub:
        textbox(slide, x + 0.26, y + 0.29, w, 0.3, sub, 10.3, MUTED, line=1.35)


def new_slide(bg_from=BG, bg_to=RGBColor(0x0E, 0x12, 0x1C)):
    s = prs.slides.add_slide(BLANK)
    gradient_bg(s, bg_from, bg_to)
    return s


def slide_fade(slide, kind="fade"):
    """Add a slide transition so the deck moves between slides."""
    xml = (
        '<p:transition xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
        'xmlns:p14="http://schemas.microsoft.com/office/powerpoint/2010/main" '
        'spd="med">'
        f'<p:{kind}/></p:transition>'
    )
    from lxml import etree
    el = etree.fromstring(xml)
    slide._element.append(el)


# =================================================================== 01 封面
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x12, 0x0F, 0x1E))
glow(s, 7.2, -3.4, 10.4, 10.4, ACC, 0.13)
glow(s, -3.0, 4.2, 8.6, 8.6, ACC2, 0.10)

badge = rect(s, 0.85, 1.28, 2.16, 0.38, fill=RGBColor(0x18, 0x21, 0x33), line=LINE, radius=0.5)
shape_text(badge, "Windows · Android", 10.5, ACC)

textbox(s, 0.85, 1.95, 9.6, 1.5, "隔空快传", 66, TEXT, bold=True)
textbox(s, 0.85, 3.28, 9.6, 0.5, "AirLink", 21, ACC, bold=True, spacing=3)
textbox(s, 0.85, 3.92, 8.6, 0.6, "两台设备之间，文件直达。", 20, TEXT, line=1.4)
textbox(s, 0.85, 4.62, 8.2, 0.9,
        "一个 exe、一个 apk，双击即用。不需要账号，不需要服务器，\n不经过任何第三方。",
        13, SUB, line=1.7)

pills = ["局域网直连", "广域网可达", "无账号", "无服务器"]
px = 0.85
for p in pills:
    w = 0.30 + len(p) * 0.185
    sh = rect(s, px, 5.62, w, 0.40, fill=RGBColor(0x15, 0x1A, 0x26), line=LINE, radius=0.5)
    shape_text(sh, p, 10.5, SUB)
    px += w + 0.16

textbox(s, 0.85, 6.62, 8.0, 0.3, "单文件运行 · 无运行时依赖 · 开源可审", 9.5, MUTED)

# =================================================================== 02 痛点
s = new_slide()
kicker(s, "THE PROBLEM")
title(s, "传个文件，为什么这么难？")
lead(s, "设备越来越多，传一个文件却仍然要绕一大圈。")

pains = [
    ("线", "找不到数据线", "线不在身边，接口还经常对不上。"),
    ("压", "聊天软件会压缩", "图片变糊、视频被压，整个文件夹更是传不动。"),
    ("等", "网盘要排队", "登录、上传、下载，一套流程走完早就不想传了。"),
    ("墙", "跨网络就断", "离开同一个 Wi-Fi，局域网工具全部失效。"),
]
cw, gap = 2.82, 0.27
for i, (glyph, t, d) in enumerate(pains):
    x = 0.85 + i * (cw + gap)
    card(s, x, 2.72, cw, 2.42)
    ring = rect(s, x + 0.28, 3.0, 0.62, 0.62, fill=RGBColor(0x1B, 0x16, 0x18), line=RGBColor(0x4A, 0x2E, 0x2E), radius=0.5)
    shape_text(ring, glyph, 17, WARN)
    textbox(s, x + 0.28, 3.78, cw - 0.56, 0.34, t, 13.5, TEXT, bold=True)
    textbox(s, x + 0.28, 4.18, cw - 0.56, 0.9, d, 10.5, MUTED, line=1.55)

bar = rect(s, 0.85, 5.45, 11.63, 0.85, fill=RGBColor(0x14, 0x18, 0x22), line=LINE, radius=0.08)
textbox(s, 1.15, 5.66, 11.0, 0.45,
        "这四件事有一个共同点：中间永远隔着别人。  AirLink 把它们全部去掉。",
        12.5, TEXT)
footer(s, 2)

# =================================================================== 03 产品定义
s = new_slide()
kicker(s, "WHAT IT IS")
title(s, "一个 exe，一个 apk，两台设备直接对话")
lead(s, "没有中间商：不走云盘，不过聊天软件，不依赖任何后台服务。")

left = card(s, 0.85, 2.55, 5.55, 3.55)
textbox(s, 1.15, 2.83, 4.95, 0.35, "它不做的事", 12, WARN, bold=True)
for i, (t, d) in enumerate([
    ("不需要账号", "没有登录、没有绑定、没有会员"),
    ("不需要服务器", "两端之间直连，你的文件不落到别人机器上"),
    ("不需要运行时", "不装 .NET、不装 Java、不装任何框架"),
]):
    y = 3.32 + i * 0.92
    bullet(s, 1.15, y, 4.9, t, d, dot=WARN)

right = card(s, 6.65, 2.55, 5.83, 3.55)
textbox(s, 6.95, 2.83, 5.2, 0.35, "它做的事", 12, GREEN, bold=True)
for i, (t, d) in enumerate([
    ("直接发现", "同一网络下自动出现，不用记 IP"),
    ("直接传输", "原始字节对拷，不压缩、不转码、不限速"),
    ("直接浏览", "像翻自己的文件夹一样翻对方的目录"),
]):
    y = 3.32 + i * 0.92
    bullet(s, 6.95, y, 5.2, t, d, dot=GREEN)

footer(s, 3)

# =================================================================== 04 形态
s = new_slide()
kicker(s, "TWO BUILDERS")
title(s, "两个程序，都是单文件")
lead(s, "小到可以塞进聊天窗口直接发过去。")

for i, (os_name, size, unit, facts, accent) in enumerate([
    ("Windows", "699", "KB", ["单个 exe，静态链接", "无 DLL 依赖，拷走即用", "Win7 及以上 64 位"], ACC),
    ("Android", "333", "KB", ["纯 Java，不用 AndroidX", "minSdk 21（Android 5.0+）", "已签名，直接安装"], ACC2),
]):
    x = 0.85 + i * 6.0
    card(s, x, 2.62, 5.63, 3.3)
    textbox(s, x + 0.35, 2.9, 3.0, 0.32, os_name, 11.5, accent, bold=True, spacing=1.5)
    textbox(s, x + 0.35, 3.2, 3.4, 0.95, size, 52, TEXT, bold=True)
    textbox(s, x + 0.35 + (1.42 if os_name == "Windows" else 1.42), 3.62, 1.4, 0.4, unit, 15, MUTED)
    for j, f in enumerate(facts):
        textbox(s, x + 0.35, 4.42 + j * 0.36, 4.9, 0.32, "·  " + f, 11, SUB)

bar = rect(s, 0.85, 6.12, 11.63, 0.62, fill=RGBColor(0x13, 0x17, 0x21), line=LINE, radius=0.12)
textbox(s, 1.15, 6.28, 11.0, 0.32,
        "体量来自取舍：界面全部自绘，零框架、零图片资源、零冗余依赖。", 11, MUTED)
footer(s, 4)

# =================================================================== 05 能力全景
s = new_slide()
kicker(s, "CAPABILITIES")
title(s, "六件事，一件不少")
lead(s, "从发现、传输到记录与保活，构成一条完整链路。")

caps = [
    ("01", "局域网自动发现", "UDP 广播，每秒探询，对方应答即入列"),
    ("02", "扫码连接", "二维码带地址与端口，扫一下就用"),
    ("03", "广域网直连", "IPv6 打通 NAT，无需端口映射"),
    ("04", "双向文件浏览", "手机看电脑、电脑看手机，同一套目录树"),
    ("05", "记录双端同步", "传输一开始就入记录，两端状态一致"),
    ("06", "熄屏保活", "前台服务 + 唤醒锁，锁屏也不掉线"),
]
cw2, ch2, gap2 = 3.75, 1.55, 0.20
for i, (num, t, d) in enumerate(caps):
    col, row = i % 3, i // 3
    x = 0.85 + col * (cw2 + gap2)
    y = 2.62 + row * (ch2 + gap2)
    card(s, x, y, cw2, ch2)
    textbox(s, x + 0.28, y + 0.24, 1.0, 0.3, num, 11, ACC, bold=True, spacing=1.2)
    textbox(s, x + 0.28, y + 0.56, cw2 - 0.56, 0.32, t, 13, TEXT, bold=True)
    textbox(s, x + 0.28, y + 0.96, cw2 - 0.56, 0.5, d, 10, MUTED, line=1.5)
footer(s, 5)

# =================================================================== 06 发现
s = new_slide()
kicker(s, "STEP 01 · DISCOVERY")
title(s, "打开就能看见对方")
lead(s, "不用问 IP，不用扫端口，也不用先加好友。", w=5.6)

for i, (t, d) in enumerate([
    ("广播探询", "每台设备定时向局域网发送一条 UDP 报文"),
    ("即时应答", "收到的设备把名称、地址、端口回给对方"),
    ("列表呈现", "几秒内出现在「附近设备」里，点一下就发"),
]):
    bullet(s, 0.85, 2.78 + i * 0.98, 4.9, t, d)

textbox(s, 0.85, 5.86, 5.4, 0.5, "同一 Wi-Fi、同一路由器，甚至手机开热点都算。", 10.5, MUTED, line=1.5)

s.shapes.add_picture(os.path.join(ASSETS, "discovery.gif"),
                     Inches(6.55), Inches(2.55), width=Inches(6.0))
cap = textbox(s, 6.55, 5.85, 6.0, 0.3, "动图：设备发现过程（放映时循环播放）", 9, MUTED)
footer(s, 6)

# =================================================================== 07 扫码
s = new_slide()
kicker(s, "STEP 02 · PAIRING")
title(s, "扫一下就配好了")
lead(s, "二维码里装的是完整连接信息，手机不需要再输入任何东西。", w=6.0)

qr_holder = card(s, 0.85, 2.6, 2.5, 2.5, fill=RGBColor(0xFF, 0xFF, 0xFF), line=None, radius=0.04)
textbox(s, 0.95, 3.62, 2.3, 0.4, "二维码", 13, RGBColor(0x0B, 0x0E, 0x14), bold=True, align=PP_ALIGN.CENTER)

for i, (t, d) in enumerate([
    ("局域网码", "airlink://192.168.1.5:45678?n=设备名"),
    ("广域网码", "airlink://[240e:…]:45678?code=017903"),
]):
    y = 2.72 + i * 0.86
    textbox(s, 3.62, y, 1.3, 0.3, t, 11.5, ACC, bold=True)
    textbox(s, 3.62, y + 0.28, 8.6, 0.32, d, 10.5, SUB)

textbox(s, 3.62, 4.56, 8.4, 1.5,
        "同一个扫码入口，按内容自动区分局域网与广域网：\n"
        "局域网码直接建会话；广域网码先过配对码，再握手。\n"
        "地址变了也不需要重新输入——电脑上刷新二维码，重扫一次即可。",
        11.5, MUTED, line=1.75)

# =================================================================== 08 广域网
s = new_slide()
kicker(s, "STEP 03 · WIDE AREA")
title(s, "不在同一个网络，也能直连")
lead(s, "优先 IPv6 原生直连；没有公网 IPv6 时，自动退回端口映射。", w=5.9)

for i, (t, d) in enumerate([
    ("IPv6 直连", "地址全球可达，没有 NAT 需要穿透"),
    ("自动兜底", "无 IPv6 时用 UPnP 请求路由器开端口"),
    ("双栈同端口", "一个监听同时服务 IPv4 与 IPv6"),
]):
    bullet(s, 0.85, 2.78 + i * 0.98, 4.9, t, d)

s.shapes.add_picture(os.path.join(ASSETS, "direct.gif"),
                     Inches(6.55), Inches(2.55), width=Inches(6.0))
textbox(s, 6.55, 5.85, 6.0, 0.3, "动图：两端点对点传输（放映时循环播放）", 9, MUTED)
footer(s, 8)

# =================================================================== 09 安全
s = new_slide()
kicker(s, "SECURITY")
title(s, "开在公网上，也不是谁都能进")
lead(s, "把端口暴露到互联网是必要代价，所以必须有一道闸。", w=8.4)

steps = [
    ("非内网来源", "来源地址不在 10./192.168./172.16-31. 段内"),
    ("必须出示配对码", "6 位数字，与电脑「WAN 连接」页显示的一致"),
    ("校验通过", "该地址在 10 分钟内免检，期间不用重复输入"),
]
for i, (t, d) in enumerate(steps):
    x = 0.85 + i * 3.92
    card(s, x, 2.66, 3.66, 1.5)
    textbox(s, x + 0.3, 2.9, 3.06, 0.32, t, 12.5, TEXT, bold=True)
    textbox(s, x + 0.3, 3.28, 3.06, 0.62, d, 10.3, MUTED, line=1.5)
    if i < 2:
        textbox(s, x + 3.66, 3.2, 0.26, 0.4, "→", 15, ACC, align=PP_ALIGN.CENTER)

card(s, 0.85, 4.42, 11.63, 1.32)
textbox(s, 1.15, 4.62, 11.0, 0.32, "内网不受影响", 12, GREEN, bold=True)
textbox(s, 1.15, 4.96, 11.0, 0.6,
        "同一局域网内仍然免密直连——安全加固不应该让日常使用变麻烦。",
        11, MUTED, line=1.5)

textbox(s, 0.85, 5.95, 11.6, 0.5,
        "所有校验都在本地完成，配对码不上传、不经手任何服务。", 10.5, MUTED)
footer(s, 9)

# =================================================================== 10 浏览
s = new_slide()
kicker(s, "BROWSE")
title(s, "两边的文件，都像在自己电脑上")
lead(s, "同一套目录树，双向可读。")

cols = [
    ("手机看电脑", ["进入任意目录", "下载文件到手机", "删除对方文件"], ACC),
    ("电脑看手机", ["浏览手机存储", "取回照片与文档", "清理不需要的文件"], ACC2),
]
for i, (t, items, accent) in enumerate(cols):
    x = 0.85 + i * 6.0
    card(s, x, 2.6, 5.63, 2.86)
    stripe = rect(s, x, 2.6, 0.06, 2.86, fill=accent)
    textbox(s, x + 0.36, 2.86, 4.9, 0.34, t, 14, TEXT, bold=True)
    for j, it in enumerate(items):
        textbox(s, x + 0.36, 3.36 + j * 0.44, 4.9, 0.3, "·  " + it, 11.2, SUB)

card(s, 0.85, 5.66, 11.63, 0.86, fill=RGBColor(0x13, 0x17, 0x21))
textbox(s, 1.15, 5.86, 11.0, 0.5,
        "大文件按块流式传输，不整块读进内存；中断后可以重新发起，不会拖垮设备。",
        11, MUTED)
footer(s, 10)

# =================================================================== 11 传输记录
s = new_slide()
kicker(s, "TRANSFER & HISTORY")
title(s, "一开始传，记录里就有了")
lead(s, "不用等结束才知道发生了什么。", w=5.6)

for i, (t, d) in enumerate([
    ("即时入列", "传输启动的瞬间写入一条「传输中」记录"),
    ("实时进度", "进度条、百分比、速度、剩余量同步刷新"),
    ("状态归位", "完成后原地更新为「完成」，不会出现两条"),
]):
    bullet(s, 0.85, 2.78 + i * 0.98, 4.9, t, d)

s.shapes.add_picture(os.path.join(ASSETS, "transfer.gif"),
                     Inches(6.55), Inches(2.55), width=Inches(6.0))
textbox(s, 6.55, 5.85, 6.0, 0.3, "动图：传输进度（放映时循环播放）", 9, MUTED)
footer(s, 11)

# =================================================================== 12 保活
s = new_slide()
kicker(s, "ALWAYS ON")
title(s, "熄屏之后，链路还在")
lead(s, "手机锁屏被系统掐断，是这类工具最常见的死法。")

items = [
    ("前台服务", "常驻通知，进程优先级提到最高", ACC),
    ("唤醒锁", "定时续约，避免 CPU 休眠停摆", ACC),
    ("Wi-Fi 锁", "逐级降级申请，尽量不让射频睡下", ACC2),
    ("锁屏重武装", "屏幕亮灭事件触发，立即重新拉活", ACC2),
]
for i, (t, d, accent) in enumerate(items):
    x = 0.85 + (i % 2) * 5.95
    y = 2.72 + (i // 2) * 1.62
    card(s, x, y, 5.68, 1.36)
    rect(s, x, y, 0.06, 1.36, fill=accent)
    textbox(s, x + 0.34, y + 0.24, 4.9, 0.32, t, 13, TEXT, bold=True)
    textbox(s, x + 0.34, y + 0.64, 4.9, 0.5, d, 10.5, MUTED, line=1.5)

textbox(s, 0.85, 6.16, 11.6, 0.4,
        "设置页可以直观看到最近一次保活是否成功，出问题能自己判断。", 10.5, MUTED)
footer(s, 12)

# =================================================================== 13 指标
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x0F, 0x12, 0x1E))
kicker(s, "BY THE NUMBERS")
title(s, "几个能对得上的数字")
lead(s, "全部取自实际构建产物，不含估算。")

stats = [
    ("699", "KB", "电脑端体积"),
    ("333", "KB", "手机端体积"),
    ("0", "个", "第三方运行时依赖"),
    ("6", "字节", "协议头开销"),
    ("2", "个", "网络端口"),
    ("10", "分钟", "配对码有效期"),
]
cw3, gap3 = 3.78, 0.198
for i, (num, unit, label) in enumerate(stats):
    col, row = i % 3, i // 3
    x = 0.85 + col * (cw3 + gap3)
    y = 2.62 + row * 1.92
    card(s, x, y, cw3, 1.66)
    textbox(s, x + 0.34, y + 0.30, 2.2, 0.82, num, 40, TEXT, bold=True)
    textbox(s, x + 0.34 + len(num) * 0.30 + 0.12, y + 0.62, 1.2, 0.32, unit, 11.5, ACC)
    textbox(s, x + 0.34, y + 1.18, cw3 - 0.68, 0.3, label, 10.8, MUTED)
footer(s, 13)

# =================================================================== 14 架构
s = new_slide()
kicker(s, "ARCHITECTURE")
title(s, "三层结构，两端逐字节一致")
lead(s, "电脑和手机共用同一套协议定义，任何一端改动都会被另一端立刻发现。")

layers = [
    ("发现层", "UDP 45679", "广播探询与应答，仅用于局域网可见", ACC),
    ("传输层", "TCP 45678", "ALFT 魔数 + 版本 + 类型，整数一律小端", ACC2),
    ("应用层", "12 类消息", "推文件 · 列目录 · 取文件 · 心跳 · 断开 · 删除 · 会话 · 记录同步 · 配对认证", GREEN),
]
for i, (name, port, desc, accent) in enumerate(layers):
    y = 2.66 + i * 1.28
    card(s, 0.85, y, 11.63, 1.1)
    rect(s, 0.85, y, 0.06, 1.1, fill=accent)
    textbox(s, 1.16, y + 0.20, 1.6, 0.34, name, 13, TEXT, bold=True)
    tag = rect(s, 2.62, y + 0.22, 1.72, 0.34, fill=RGBColor(0x18, 0x1E, 0x2B), line=LINE, radius=0.5)
    shape_text(tag, port, 9.8, accent)
    textbox(s, 4.52, y + 0.22, 7.7, 0.62, desc, 10.8, MUTED, line=1.5)

textbox(s, 0.85, 6.42, 11.6, 0.4,
        "协议只有 6 字节头部，其余全部是负载——把开销留给数据本身。", 10.5, MUTED)
footer(s, 14)

# =================================================================== 15 对比
s = new_slide()
kicker(s, "COMPARISON")
title(s, "换一种方式看它")
lead(s, "没有一种工具适合所有场景，但传文件这件事可以更直接。")

cols = ["AirLink", "聊天软件", "网盘", "数据线"]
rows = [
    ("需要账号", "否", "是", "是", "否"),
    ("需要互联网", "否", "是", "是", "否"),
    ("经过第三方服务器", "否", "是", "是", "否"),
    ("压缩 / 转码", "不压缩", "会压缩", "不压缩", "不压缩"),
    ("跨网络传输", "支持", "支持", "支持", "不支持"),
    ("免安装", "单文件", "需安装", "需安装", "需驱动"),
]
col_x = [0.85, 3.35, 6.35, 8.85, 11.35]
col_w = [2.4, 2.9, 2.4, 2.4, 1.15]
head_y = 2.62
for i, c in enumerate(cols):
    fill = RGBColor(0x18, 0x21, 0x33) if i == 0 else RGBColor(0x15, 0x1A, 0x24)
    sh = rect(s, col_x[i], head_y, col_w[i], 0.48,
              fill=fill, line=None, radius=0.10)
    shape_text(sh, c, 10.8, ACC if i == 0 else SUB, bold=(i == 0))

for r, (label, *vals) in enumerate(rows):
    y = head_y + 0.56 + r * 0.56
    if r % 2 == 0:
        rect(s, 0.85, y - 0.04, 11.63, 0.54, fill=RGBColor(0x0F, 0x13, 0x1B), line=None)
    textbox(s, col_x[0] + 0.18, y + 0.10, 2.1, 0.32, label, 10.8, SUB)
    for c, v in enumerate(vals):
        good = (c == 0)
        col = GREEN if (good and v in ("否", "不压缩", "支持", "单文件")) else (TEXT if good else MUTED)
        textbox(s, col_x[c + 1], y + 0.10, col_w[c + 1] - 0.1, 0.32, v, 10.8, col,
                bold=good)
footer(s, 15)

# =================================================================== 16 场景
s = new_slide()
kicker(s, "USE CASES")
title(s, "什么时候用得上")
lead(s, "只要两台设备在同一个屏幕前，或者同一个人在两地。")

scenes = [
    ("办公室换机", "旧电脑的资料整目录搬到新机器，不上传、不等审批。"),
    ("照片回传", "手机拍完先传电脑修图，原始画质不损失。"),
    ("出差在外", "通过 IPv6 直连公司的电脑取文件，不需要 VPN。"),
    ("断网环境", "同一路由器内即可互传，外网断了照样能用。"),
]
for i, (t, d) in enumerate(scenes):
    x = 0.85 + (i % 2) * 5.95
    y = 2.72 + (i // 2) * 1.72
    card(s, x, y, 5.68, 1.46)
    num = rect(s, x + 0.30, y + 0.32, 0.52, 0.52, fill=RGBColor(0x18, 0x21, 0x33), line=LINE, radius=0.5)
    shape_text(num, str(i + 1), 12, ACC, bold=True)
    textbox(s, x + 1.0, y + 0.30, 4.4, 0.32, t, 12.8, TEXT, bold=True)
    textbox(s, x + 1.0, y + 0.70, 4.4, 0.6, d, 10.3, MUTED, line=1.5)
footer(s, 16)

# =================================================================== 17 结语
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x12, 0x0F, 0x1E))
glow(s, 6.6, 3.2, 10.0, 10.0, ACC, 0.12)
glow(s, -2.6, -3.2, 8.0, 8.0, ACC2, 0.09)

textbox(s, 0.85, 1.62, 11.2, 1.4, "两台设备之间，", 44, TEXT, bold=True)
textbox(s, 0.85, 2.52, 11.2, 1.4, "不该有中间商。", 44, ACC, bold=True)
textbox(s, 0.85, 3.72, 8.8, 0.6, "小、快、直——就这三件事。", 15, SUB)

feats = [("小", "699 KB / 333 KB"), ("快", "点对点直连"), ("直", "不经过任何服务器")]
for i, (k, v) in enumerate(feats):
    x = 0.85 + i * 3.6
    card(s, x, 4.66, 3.3, 1.18)
    textbox(s, x + 0.32, 4.86, 1.0, 0.5, k, 25, ACC, bold=True)
    textbox(s, x + 1.16, 5.08, 2.0, 0.32, v, 10.5, SUB)

line = rect(s, 0.85, 6.42, 11.63, 0.012, fill=LINE)
textbox(s, 0.85, 6.62, 6.0, 0.3, "AirLink · 隔空快传", 10.5, MUTED)
textbox(s, W - 4.2, 6.62, 3.35, 0.3, "Windows 与 Android 双端就绪", 9.5, MUTED, align=PP_ALIGN.RIGHT)

# ---------------------------------------------------------------- transitions
kinds = ["fade", "push", "wipe", "zoom", "cover", "fade", "push", "fade", "wipe"]
for i, sl in enumerate(prs.slides):
    slide_fade(sl, kinds[i % len(kinds)])


# ---------------------------------------------------------------- speaker notes
NOTES = [
 "开场先给定位：这是一套让两台设备直接对话的工具，电脑端一个 exe、手机端一个 apk，"
 "中间不需要任何服务器。今天的介绍会覆盖它解决什么问题、怎么用、以及背后的设计取舍。",

 "四个场景分别对应线、压缩、等待、跨网四种阻碍。共同点是：文件必须经过一个第三方，"
 "要么是人（U 盘、数据线），要么是服务（网盘、聊天软件）。AirLink 的思路是把这一层去掉。",

 "三个“不需要”是产品的边界：不注册、不上传、不依赖运行时。三个“直接”是体验的主线："
 "直接发现、直接传输、直接浏览。任何新功能都要能落进这六个框里，否则不做。",

 "体积是设计结果而不是目标：界面全部用 GDI+ 和 Canvas 自绘，没有 WebView、没有 UI 框架、"
 "没有图标图片资源，所以才能做到几百 KB。单文件意味着拷走就能用，不需要安装程序。",

 "六项能力是一条完整链路：发现决定能不能看见对方，连接决定怎么建立会话，"
 "传输与浏览决定能做什么，记录与保活决定用起来是否安心。",

 "局域网发现走 UDP 广播，双方互发一条短报文，报文里带设备名、地址和端口。"
 "它只在同一广播域内有效——这也正是必须有广域网方案的原因。",

 "二维码里放的是完整的连接串：局域网码带地址和端口，广域网码额外带配对码。"
 "手机端按内容自动判断走哪条路径，用户不需要理解区别。",

 "优先 IPv6：地址全球可达，不需要 NAT 穿透，也不需要动路由器。"
 "没有公网 IPv6 时才退回 UPnP 自动做端口映射，做不到就提示手动配置。",

 "把端口开到公网就必须有闸门。规则很简单：来源不在内网段，就必须先出示配对码；"
 "通过后该地址十分钟内免检，避免每次操作都输入。内网保持免密，不牺牲日常体验。",

 "浏览是双向的：手机可以进电脑的任何目录，电脑也能翻手机的存储。"
 "大文件按块传输，不会把整个文件读进内存，删除操作也支持。",

 "记录不是一个事后日志，而是传输过程的一部分：一开始传就写一条“传输中”，"
 "过程中刷新进度，结束后原地改成“完成”。两端通过记录 id 对齐，不会出现重复条目。",

 "熄屏掉线是这类工具最常见的问题。四层手段叠加：前台服务提优先级、唤醒锁防 CPU 休眠、"
 "Wi-Fi 锁防射频休眠、锁屏广播触发重新武装。设置页能看到最近一次保活结果，便于自查。",

 "这一页的数字都可以核对：两个产物的实际体积、依赖数量、协议头长度、监听端口数、"
 "配对码有效期。不放无法验证的性能宣称。",

 "三层结构各自职责清晰：发现层只解决“看得见”，传输层只解决“连得上”，应用层定义“能做什么”。"
 "两端共用同一份协议定义，字段顺序和字节序完全一致，任何一端改错另一端会立刻暴露。",

 "对比不为了贬低别的工具——聊天软件和网盘在各自场景里都很好用。这张表的重点是说明："
 "如果目标只是“把文件从 A 送到 B”，中间那一层其实可以不存在。",

 "四个典型场景：换机搬运、照片回传、异地取文件、断网环境。前两个是高频日常，"
 "后两个是别的工具做不到或做起来很麻烦的情况。",

 "收尾回到一句话：两台设备之间不该有中间商。小、快、直，是这套工具的全部主张。",
]

for sl, note in zip(prs.slides, NOTES):
    sl.notes_slide.notes_text_frame.text = note
prs.save(OUT)
print("saved", OUT, "slides:", len(prs.slides.__iter__.__self__._sldIdLst))
