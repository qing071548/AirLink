"""Build the portrait (9:16) version of the AirLink promo deck."""
import os

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

ROOT = r"E:\AZ\deepseek H\gongzuoqu\AirLink"
OUT = os.path.join(ROOT, "AirLink-宣传-竖屏.pptx")
ASSETS = os.path.join(ROOT, "assets")

BG      = RGBColor(0x0B, 0x0E, 0x14)
PANEL   = RGBColor(0x12, 0x16, 0x1F)
LINE    = RGBColor(0x23, 0x2C, 0x3B)
ACC     = RGBColor(0x5E, 0x91, 0xFF)
ACC2    = RGBColor(0x7E, 0x5C, 0xFF)
TEXT    = RGBColor(0xF2, 0xF5, 0xFA)
SUB     = RGBColor(0x9A, 0xA4, 0xB8)
MUTED   = RGBColor(0x5F, 0x6A, 0x80)
GREEN   = RGBColor(0x4A, 0xDE, 0x80)
WARN    = RGBColor(0xFF, 0xB0, 0x6A)

FONT = "Microsoft YaHei"
W, H = 7.5, 13.333
X0, CW = 0.62, 6.26          # left margin / content width

prs = Presentation()
prs.slide_width = Inches(W)
prs.slide_height = Inches(H)
BLANK = prs.slide_layouts[6]


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


def textbox(slide, x, y, w, h, text, size=12, color=TEXT, bold=False,
            align=PP_ALIGN.LEFT, anchor=MSO_ANCHOR.TOP, line=1.3, spacing=None):
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


def shape_text(sh, text, size=12, color=TEXT, bold=False, align=PP_ALIGN.CENTER):
    tf = sh.text_frame
    tf.margin_left = tf.margin_right = Inches(0.06)
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
    sh = rect(slide, x, y, w, h, fill=color, radius=0.5)
    sh.fill.gradient()
    sh.fill.gradient_angle = 60
    stops = sh.fill.gradient_stops
    stops[0].color.rgb = color
    stops[1].color.rgb = BG
    for st, a in zip(stops, (int(alpha * 100000), 0)):
        srgb = st.color._xFill.find(qn("a:srgbClr"))
        srgb.append(srgb.makeelement(qn("a:alpha"), {"val": str(a)}))
    return sh


def new_slide(bg_from=BG, bg_to=RGBColor(0x0E, 0x12, 0x1C)):
    s = prs.slides.add_slide(BLANK)
    gradient_bg(s, bg_from, bg_to)
    return s


def footer(slide, idx):
    textbox(slide, X0, 12.62, 4.0, 0.3, "AirLink · 隔空快传", 8.5, MUTED)
    textbox(slide, W - 1.3, 12.62, 0.68, 0.3, f"{idx:02d}", 8.5, MUTED, align=PP_ALIGN.RIGHT)


def head(slide, kick, ttl, lead_text=None, kick_y=0.72, title_y=1.06, lead_y=1.86):
    textbox(slide, X0, kick_y, 5.6, 0.28, kick, 9.5, ACC, bold=True, spacing=1.6)
    textbox(slide, X0, title_y, CW, 0.62, ttl, 26, TEXT, bold=True, line=1.2)
    if lead_text:
        textbox(slide, X0, lead_y, CW, 0.62, lead_text, 11, SUB, line=1.55)


def card(slide, x, y, w, h, fill=PANEL, line=LINE, radius=0.08):
    return rect(slide, x, y, w, h, fill=fill, line=line, radius=radius)


def slide_transition(slide, kind="fade"):
    from lxml import etree
    xml = ('<p:transition xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
           f'spd="med"><p:{kind}/></p:transition>')
    slide._element.append(etree.fromstring(xml))


# =================================================================== 01 封面
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x12, 0x0F, 0x1E))
glow(s, 2.6, -2.6, 8.4, 8.4, ACC, 0.13)
glow(s, -3.0, 9.4, 8.0, 8.0, ACC2, 0.10)

badge = rect(s, X0, 3.30, 2.05, 0.36, fill=RGBColor(0x18, 0x21, 0x33), line=LINE, radius=0.5)
shape_text(badge, "Windows · Android", 9.5, ACC)

textbox(s, X0, 3.90, CW, 0.95, "隔空快传", 46, TEXT, bold=True, line=1.05)
textbox(s, X0, 5.00, CW, 0.4, "AirLink", 17, ACC, bold=True, spacing=3)
textbox(s, X0, 5.52, CW, 0.4, "两台设备之间，文件直达。", 15.5, TEXT, line=1.35)
textbox(s, X0, 6.10, CW, 0.9,
        "一个 exe、一个 apk，双击即用。\n不需要账号，不需要服务器，不经过任何第三方。",
        10.8, SUB, line=1.7)

pills = ["局域网直连", "广域网可达", "无账号", "无服务器"]
for i, p in enumerate(pills):
    px = X0 + (i % 2) * 2.62
    py = 7.36 + (i // 2) * 0.52
    sh = rect(s, px, py, 2.44, 0.40, fill=RGBColor(0x15, 0x1A, 0x26), line=LINE, radius=0.5)
    shape_text(sh, p, 9.8, SUB)

line = rect(s, X0, 8.74, CW, 0.01, fill=LINE)
textbox(s, X0, 8.94, CW, 0.9,
        "单文件运行 · 无运行时依赖 · 可选 IPv6 直连\nWindows 与 Android 双端同协议",
        9, MUTED, line=1.6)

# =================================================================== 02 痛点
s = new_slide()
head(s, "THE PROBLEM", "传个文件，\n为什么这么难？", "设备越来越多，传一个文件却仍然要绕一大圈。", lead_y=2.02)

pains = [
    ("线", "找不到数据线", "线不在身边，接口还经常对不上。"),
    ("压", "聊天软件会压缩", "图片变糊、视频被压，整个文件夹更传不动。"),
    ("等", "网盘要排队", "登录、上传、下载，流程走完早就不想传了。"),
    ("墙", "跨网络就断", "离开同一个 Wi-Fi，局域网工具全部失效。"),
]
y = 2.86
for glyph, t, d in pains:
    card(s, X0, y, CW, 1.62)
    ring = rect(s, X0 + 0.3, y + 0.34, 0.56, 0.56,
                fill=RGBColor(0x1B, 0x16, 0x18), line=RGBColor(0x4A, 0x2E, 0x2E), radius=0.5)
    shape_text(ring, glyph, 15, WARN)
    textbox(s, X0 + 1.02, y + 0.3, CW - 1.3, 0.32, t, 12.5, TEXT, bold=True)
    textbox(s, X0 + 1.02, y + 0.68, CW - 1.3, 0.6, d, 9.8, MUTED, line=1.5)
    y += 1.78

bar = rect(s, X0, y + 0.16, CW, 1.05, fill=RGBColor(0x14, 0x18, 0x22), line=LINE, radius=0.12)
textbox(s, X0 + 0.32, y + 0.38, CW - 0.64, 0.7,
        "这四件事有一个共同点：\n中间永远隔着别人。\nAirLink 把它去掉。", 11, TEXT, line=1.6)
footer(s, 2)

# =================================================================== 03 定义
s = new_slide()
head(s, "WHAT IT IS", "一个 exe，一个 apk，\n两台设备直接对话", lead_y=2.06)

left = card(s, X0, 2.82, CW, 3.95)
textbox(s, X0 + 0.34, 3.12, CW - 0.68, 0.3, "它不做的事", 11, WARN, bold=True)
for i, (t, d) in enumerate([
    ("不需要账号", "没有登录、没有绑定、没有会员"),
    ("不需要服务器", "两端直连，文件不落到别人机器上"),
    ("不需要运行时", "不装 .NET、不装 Java、不装框架"),
]):
    y = 3.66 + i * 0.92
    rect(s, X0 + 0.34, y + 0.08, 0.08, 0.08, fill=WARN, radius=0.5)
    textbox(s, X0 + 0.58, y, CW - 1.0, 0.3, t, 11.5, TEXT, bold=True)
    textbox(s, X0 + 0.58, y + 0.32, CW - 1.0, 0.3, d, 9.5, MUTED)

right = card(s, X0, 7.02, CW, 3.95)
textbox(s, X0 + 0.34, 7.28, CW - 0.68, 0.3, "它做的事", 11, GREEN, bold=True)
for i, (t, d) in enumerate([
    ("直接发现", "同一网络下自动出现，不用记 IP"),
    ("直接传输", "原始字节对拷，不压缩、不转码"),
    ("直接浏览", "像翻自己的文件夹一样翻对方目录"),
]):
    y = 7.82 + i * 0.92
    rect(s, X0 + 0.34, y + 0.08, 0.08, 0.08, fill=GREEN, radius=0.5)
    textbox(s, X0 + 0.58, y, CW - 1.0, 0.3, t, 11.5, TEXT, bold=True)
    textbox(s, X0 + 0.58, y + 0.32, CW - 1.0, 0.3, d, 9.5, MUTED)

textbox(s, X0, 11.24, CW, 0.8,
        "所有新功能都要落进这六个框里，\n否则不做。", 9.6, MUTED, line=1.6)
footer(s, 3)

# =================================================================== 04 形态
s = new_slide()
head(s, "TWO BUILDERS", "两个程序，\n都是单文件", "小到可以塞进聊天窗口直接发过去。", lead_y=1.98)

for i, (os_name, size, unit, facts, accent) in enumerate([
    ("Windows", "699", "KB", ["单个 exe，静态链接", "无 DLL 依赖，拷走即用", "Win7 及以上 64 位"], ACC),
    ("Android", "333", "KB", ["纯 Java，不用 AndroidX", "minSdk 21（Android 5.0+）", "已签名，直接安装"], ACC2),
]):
    y = 2.72 + i * 3.86
    card(s, X0, y, CW, 3.58)
    textbox(s, X0 + 0.38, y + 0.34, 3.0, 0.3, os_name, 10.5, accent, bold=True, spacing=1.5)
    textbox(s, X0 + 0.38, y + 0.72, 3.2, 0.95, size, 44, TEXT, bold=True)
    textbox(s, X0 + 2.06, y + 1.16, 1.2, 0.36, unit, 13, MUTED)
    for j, f in enumerate(facts):
        textbox(s, X0 + 0.38, y + 1.88 + j * 0.4, CW - 0.76, 0.32, "·  " + f, 9.8, SUB)

textbox(s, X0, 10.7, CW, 0.9,
        "体量来自取舍：界面全部自绘，\n零框架、零图片资源、零冗余依赖。", 9.6, MUTED, line=1.6)
footer(s, 4)

# =================================================================== 05 能力
s = new_slide()
head(s, "CAPABILITIES", "六件事，一件不少", "从发现到保活，构成一条完整链路。", lead_y=1.98)

caps = [
    ("01", "自动发现", "UDP 广播，对方应答即入列"),
    ("02", "扫码连接", "二维码带地址与端口"),
    ("03", "广域网直连", "IPv6 打通 NAT，免映射"),
    ("04", "双向浏览", "两端目录互相可见"),
    ("05", "记录同步", "一开始传就入记录"),
    ("06", "熄屏保活", "锁屏也不掉线"),
]
cw2, ch2, gx, gy = 3.0, 2.24, 0.26, 0.34
for i, (num, t, d) in enumerate(caps):
    col, row = i % 2, i // 2
    x = X0 + col * (cw2 + gx)
    y = 2.72 + row * (ch2 + gy)
    card(s, x, y, cw2, ch2)
    textbox(s, x + 0.28, y + 0.50, 1.0, 0.28, num, 10, ACC, bold=True, spacing=1.2)
    textbox(s, x + 0.28, y + 0.84, cw2 - 0.56, 0.3, t, 12.2, TEXT, bold=True)
    textbox(s, x + 0.28, y + 1.30, cw2 - 0.56, 0.6, d, 9.4, MUTED, line=1.5)
footer(s, 5)

# =================================================================== 06 发现
s = new_slide()
head(s, "STEP 01 · DISCOVERY", "打开就能\n看见对方", "不用问 IP，不用扫端口，也不用先加好友。", lead_y=2.02)

for i, (t, d) in enumerate([
    ("广播探询", "每台设备定时向局域网发一条 UDP 报文"),
    ("即时应答", "收到的设备把名称、地址、端口回给对方"),
    ("列表呈现", "几秒内出现在「附近设备」里，点一下就发"),
]):
    y = 2.86 + i * 0.92
    rect(s, X0, y + 0.09, 0.09, 0.09, fill=ACC, radius=0.5)
    textbox(s, X0 + 0.28, y, CW - 0.28, 0.3, t, 11.8, TEXT, bold=True)
    textbox(s, X0 + 0.28, y + 0.33, CW - 0.28, 0.5, d, 9.5, MUTED, line=1.45)

s.shapes.add_picture(os.path.join(ASSETS, "discovery.gif"), Inches(X0), Inches(6.30), width=Inches(CW))
textbox(s, X0, 9.68, CW, 0.5, "动图：设备发现过程（放映时循环播放）", 8.5, MUTED)
textbox(s, X0, 10.14, CW, 0.9, "同一 Wi-Fi、同一路由器，甚至手机开热点都算。", 9.6, MUTED, line=1.6)
footer(s, 6)

# =================================================================== 07 扫码
s = new_slide()
head(s, "STEP 02 · PAIRING", "扫一下就配好了", "二维码里装着完整连接信息。", lead_y=1.98)

qr = card(s, X0, 2.72, 2.2, 2.2, fill=RGBColor(0xFF, 0xFF, 0xFF), line=None, radius=0.06)
textbox(s, X0 + 0.1, 3.66, 2.0, 0.4, "二维码", 11, RGBColor(0x0B, 0x0E, 0x14), bold=True, align=PP_ALIGN.CENTER)

codes = [("局域网码", "airlink://192.168.1.5:45678?n=设备名", ACC),
         ("广域网码", "airlink://[240e:…]:45678?code=017903", ACC2)]
for i, (t, d, c) in enumerate(codes):
    y = 2.78 + i * 0.94
    textbox(s, X0 + 2.5, y, 2.0, 0.28, t, 10, c, bold=True)
    textbox(s, X0 + 2.5, y + 0.30, CW - 2.5, 0.5, d, 8.8, SUB, line=1.4)

textbox(s, X0, 5.24, CW, 1.6,
        "同一个扫码入口，按内容自动区分：\n"
        "局域网码直接建会话；\n"
        "广域网码先过配对码，再握手。\n"
        "地址变了只需电脑刷新二维码，重扫一次。",
        10, MUTED, line=1.8)

card(s, X0, 7.1, CW, 1.5, fill=RGBColor(0x13, 0x17, 0x21))
textbox(s, X0 + 0.34, 7.34, CW - 0.68, 0.3, "为什么用二维码", 10.5, TEXT, bold=True)
textbox(s, X0 + 0.34, 7.70, CW - 0.68, 0.7,
        "IPv6 地址有 39 个字符，手输几乎不可能。二维码把它变成一次抬手。", 9.4, MUTED, line=1.5)
footer(s, 7)

# =================================================================== 08 广域网
s = new_slide()
head(s, "STEP 03 · WIDE AREA", "不在同一个网络，\n也能直连", "优先 IPv6 直连；没有则退回端口映射。", lead_y=2.06)

for i, (t, d) in enumerate([
    ("IPv6 直连", "地址全球可达，没有 NAT 需要穿透"),
    ("自动兜底", "无 IPv6 时用 UPnP 请路由器开端口"),
    ("双栈同端口", "一个监听同时服务 IPv4 与 IPv6"),
]):
    y = 2.9 + i * 0.92
    rect(s, X0, y + 0.09, 0.09, 0.09, fill=ACC2, radius=0.5)
    textbox(s, X0 + 0.28, y, CW - 0.28, 0.3, t, 11.8, TEXT, bold=True)
    textbox(s, X0 + 0.28, y + 0.33, CW - 0.28, 0.5, d, 9.5, MUTED, line=1.45)

s.shapes.add_picture(os.path.join(ASSETS, "direct.gif"), Inches(X0), Inches(6.30), width=Inches(CW))
textbox(s, X0, 9.68, CW, 0.5, "动图：两端点对点传输（放映时循环播放）", 8.5, MUTED)
footer(s, 8)

# =================================================================== 09 安全
s = new_slide()
head(s, "SECURITY", "开在公网上，\n也不是谁都能进", "把端口暴露到互联网是必要代价，所以必须有闸门。", lead_y=2.06)

steps = [
    ("非内网来源", "来源地址不在 10./192.168./172.16-31. 段内"),
    ("必须出示配对码", "6 位数字，与电脑「WAN 连接」页一致"),
    ("校验通过", "该地址 10 分钟内免检，不用重复输入"),
]
y = 2.98
for i, (t, d) in enumerate(steps):
    card(s, X0, y, CW, 1.42)
    num = rect(s, X0 + 0.3, y + 0.32, 0.5, 0.5, fill=RGBColor(0x18, 0x21, 0x33), line=LINE, radius=0.5)
    shape_text(num, str(i + 1), 11, ACC, bold=True)
    textbox(s, X0 + 0.98, y + 0.3, CW - 1.3, 0.3, t, 11.5, TEXT, bold=True)
    textbox(s, X0 + 0.98, y + 0.66, CW - 1.3, 0.45, d, 9.2, MUTED, line=1.45)
    y += 1.62

card(s, X0, 7.62, CW, 1.52, fill=RGBColor(0x11, 0x1A, 0x16))
textbox(s, X0 + 0.34, 7.86, CW - 0.68, 0.3, "内网不受影响", 10.8, GREEN, bold=True)
textbox(s, X0 + 0.34, 8.24, CW - 0.68, 0.7,
        "同一局域网仍然免密直连——安全加固不该让日常使用变麻烦。", 9.2, MUTED, line=1.45)

textbox(s, X0, 9.62, CW, 0.8,
        "所有校验都在本地完成，\n配对码不上传、不经手任何服务。", 9.4, MUTED, line=1.6)
footer(s, 9)

# =================================================================== 10 浏览
s = new_slide()
head(s, "BROWSE", "两边的文件，\n都像在自己电脑上", "同一套目录树，双向可读。", lead_y=2.06)

cols = [
    ("手机看电脑", ["进入任意目录", "下载文件到手机", "删除对方文件"], ACC),
    ("电脑看手机", ["浏览手机存储", "取回照片与文档", "清理不需要的文件"], ACC2),
]
for i, (t, items, accent) in enumerate(cols):
    y = 2.82 + i * 2.86
    card(s, X0, y, CW, 2.62)
    rect(s, X0, y, 0.06, 2.62, fill=accent)
    textbox(s, X0 + 0.36, y + 0.26, CW - 0.7, 0.32, t, 12.5, TEXT, bold=True)
    for j, it in enumerate(items):
        textbox(s, X0 + 0.36, y + 0.78 + j * 0.42, CW - 0.7, 0.3, "·  " + it, 10, SUB)

card(s, X0, 8.62, CW, 1.32, fill=RGBColor(0x13, 0x17, 0x21))
textbox(s, X0 + 0.34, 8.86, CW - 0.68, 0.85,
        "大文件按块流式传输，不整块读进内存；\n中断后可以重新发起，不会拖垮设备。",
        9.4, MUTED, line=1.6)
footer(s, 10)

# =================================================================== 11 传输
s = new_slide()
head(s, "TRANSFER", "一开始传，\n记录里就有了", "不用等结束才知道发生了什么。", lead_y=2.06)

for i, (t, d) in enumerate([
    ("即时入列", "启动瞬间写入一条「传输中」记录"),
    ("实时进度", "进度条、百分比、速度、剩余量"),
    ("状态归位", "完成后原地更新，不会出现两条"),
]):
    y = 2.9 + i * 0.92
    rect(s, X0, y + 0.09, 0.09, 0.09, fill=ACC, radius=0.5)
    textbox(s, X0 + 0.28, y, CW - 0.28, 0.3, t, 11.8, TEXT, bold=True)
    textbox(s, X0 + 0.28, y + 0.33, CW - 0.28, 0.5, d, 9.5, MUTED, line=1.45)

s.shapes.add_picture(os.path.join(ASSETS, "transfer.gif"), Inches(X0), Inches(6.30), width=Inches(CW))
textbox(s, X0, 9.68, CW, 0.5, "动图：传输进度（放映时循环播放）", 8.5, MUTED)
footer(s, 11)

# =================================================================== 12 保活
s = new_slide()
head(s, "ALWAYS ON", "熄屏之后，链路还在", "手机锁屏被系统掐断，是这类工具最常见的死法。", lead_y=2.0)

items = [
    ("前台服务", "常驻通知，进程优先级提到最高", ACC),
    ("唤醒锁", "定时续约，避免 CPU 休眠停摆", ACC),
    ("Wi-Fi 锁", "逐级降级申请，不让射频睡下", ACC2),
    ("锁屏重武装", "屏幕亮灭事件触发，立即重新拉活", ACC2),
]
y = 2.92
for t, d, accent in items:
    card(s, X0, y, CW, 1.48)
    rect(s, X0, y, 0.06, 1.48, fill=accent)
    textbox(s, X0 + 0.34, y + 0.26, CW - 0.7, 0.3, t, 11.8, TEXT, bold=True)
    textbox(s, X0 + 0.34, y + 0.66, CW - 0.7, 0.45, d, 9.3, MUTED, line=1.45)
    y += 1.66

textbox(s, X0, y + 0.12, CW, 0.8,
        "设置页可以直观看最近一次保活是否成功，\n出问题能自己判断。", 9.4, MUTED, line=1.6)
footer(s, 12)

# =================================================================== 13 指标
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x0F, 0x12, 0x1E))
head(s, "BY THE NUMBERS", "几个能对得上的数字", "全部取自实际构建产物，不含估算。", lead_y=1.98)

stats = [
    ("699", "KB", "电脑端体积"),
    ("333", "KB", "手机端体积"),
    ("0", "个", "第三方运行时依赖"),
    ("6", "字节", "协议头开销"),
    ("2", "个", "网络端口"),
    ("10", "分钟", "配对码有效期"),
]
cw3, ch3, gx3, gy3 = 3.0, 2.30, 0.26, 0.34
for i, (num, unit, label) in enumerate(stats):
    col, row = i % 2, i // 2
    x = X0 + col * (cw3 + gx3)
    y = 2.66 + row * (ch3 + gy3)
    card(s, x, y, cw3, ch3)
    textbox(s, x + 0.32, y + 0.58, 2.4, 0.8, num, 34, TEXT, bold=True)
    textbox(s, x + 0.32 + len(num) * 0.26 + 0.06, y + 0.86, 1.1, 0.3, unit, 10.5, ACC)
    textbox(s, x + 0.32, y + 1.58, cw3 - 0.64, 0.3, label, 9.4, MUTED)
footer(s, 13)

# =================================================================== 14 架构
s = new_slide()
head(s, "ARCHITECTURE", "三层结构，\n两端逐字节一致", "两端共用同一份协议定义。", lead_y=2.06)

layers = [
    ("发现层", "UDP 45679", "广播探询与应答，仅用于局域网可见", ACC),
    ("传输层", "TCP 45678", "ALFT 魔数 + 版本 + 类型，整数一律小端", ACC2),
    ("应用层", "12 类消息", "推文件 · 列目录 · 取文件 · 心跳 · 断开 · 删除\n会话 · 记录同步 ×3 · 配对认证", GREEN),
]
y = 2.82
for name, port, desc, accent in layers:
    h = 2.24 if name == "应用层" else 1.74
    card(s, X0, y, CW, h)
    rect(s, X0, y, 0.06, h, fill=accent)
    textbox(s, X0 + 0.32, y + 0.26, 2.2, 0.32, name, 12, TEXT, bold=True)
    tag = rect(s, X0 + 2.5, y + 0.24, 1.62, 0.32,
               fill=RGBColor(0x18, 0x1E, 0x2B), line=LINE, radius=0.5)
    shape_text(tag, port, 9, accent)
    textbox(s, X0 + 0.32, y + 0.72, CW - 0.64, 0.9, desc, 9.3, MUTED, line=1.55)
    y += h + 0.30

textbox(s, X0, y + 0.12, CW, 0.8,
        "协议只有 6 字节头部，其余全部是负载——\n把开销留给数据本身。", 9.4, MUTED, line=1.6)
footer(s, 14)

# =================================================================== 15 对比
s = new_slide()
head(s, "COMPARISON", "换一种方式看它", "没有一种工具适合所有场景，但传文件可以更直接。", lead_y=1.98)

cols = ["", "AirLink", "聊天", "网盘", "数据线"]
col_x = [X0, X0 + 2.42, X0 + 3.92, X0 + 4.96, X0 + 6.0]
col_w = [2.42, 1.50, 1.04, 1.04, 0.9]
rows = [
    ("需要账号", "否", "是", "是", "否"),
    ("需要互联网", "否", "是", "是", "否"),
    ("经第三方服务器", "否", "是", "是", "否"),
    ("压缩 / 转码", "不压缩", "会压缩", "不压缩", "不压缩"),
    ("跨网络传输", "支持", "支持", "支持", "不支持"),
    ("免安装", "单文件", "需安装", "需安装", "需驱动"),
]
hy = 2.62
for i, c in enumerate(cols):
    if i == 0:
        continue
    fill = RGBColor(0x18, 0x21, 0x33) if i == 1 else RGBColor(0x15, 0x1A, 0x24)
    sh = rect(s, col_x[i], hy, col_w[i], 0.44, fill=fill, line=None, radius=0.12)
    shape_text(sh, c, 8.8, ACC if i == 1 else SUB, bold=(i == 1))

for r, (label, *vals) in enumerate(rows):
    y = hy + 0.56 + r * 0.80
    if r % 2 == 0:
        rect(s, X0, y - 0.06, CW, 0.66, fill=RGBColor(0x0F, 0x13, 0x1B), line=None)
    textbox(s, col_x[0] + 0.12, y + 0.16, col_w[0] - 0.2, 0.3, label, 9.2, SUB)
    for c, v in enumerate(vals):
        good = (c == 0)
        if good:
            ok = v in ("否", "不压缩", "支持", "单文件")
            col = GREEN if ok else TEXT
        else:
            col = MUTED
        textbox(s, col_x[c + 1] + 0.04, y + 0.16, col_w[c + 1] - 0.08, 0.3,
                v, 8.8, col, bold=good)

textbox(s, X0, 8.30, CW, 1.2,
        "对比不为贬低别的工具——聊天软件和网盘\n在各自场景里都很好用。\n"
        "这张表只想说明：如果目标只是把文件从 A 送到 B，\n中间那一层其实可以不存在。",
        9.2, MUTED, line=1.7)
footer(s, 15)

# =================================================================== 16 场景
s = new_slide()
head(s, "USE CASES", "什么时候用得上", "只要两台设备在同一个屏幕前，或同一个人在两地。", lead_y=1.98)

scenes = [
    ("办公室换机", "旧电脑资料整目录搬到新机器，不上传、不等审批。"),
    ("照片回传", "手机拍完先传电脑修图，原始画质不损失。"),
    ("出差在外", "通过 IPv6 直连公司电脑取文件，不需要 VPN。"),
    ("断网环境", "同一路由器内即可互传，外网断了照样能用。"),
]
y = 2.72
for i, (t, d) in enumerate(scenes):
    card(s, X0, y, CW, 1.60)
    num = rect(s, X0 + 0.32, y + 0.34, 0.5, 0.5,
               fill=RGBColor(0x18, 0x21, 0x33), line=LINE, radius=0.5)
    shape_text(num, str(i + 1), 11, ACC, bold=True)
    textbox(s, X0 + 1.02, y + 0.3, CW - 1.3, 0.3, t, 11.8, TEXT, bold=True)
    textbox(s, X0 + 1.02, y + 0.68, CW - 1.3, 0.5, d, 9.2, MUTED, line=1.45)
    y += 1.76
footer(s, 16)

# =================================================================== 17 结语
s = new_slide(RGBColor(0x0A, 0x0D, 0x15), RGBColor(0x12, 0x0F, 0x1E))
glow(s, 1.0, 8.6, 8.0, 8.0, ACC, 0.12)
glow(s, -3.0, -2.6, 7.4, 7.4, ACC2, 0.09)

textbox(s, X0, 2.62, CW, 1.1, "两台设备之间，", 32, TEXT, bold=True, line=1.2)
textbox(s, X0, 3.50, CW, 1.1, "不该有中间商。", 32, ACC, bold=True, line=1.2)
textbox(s, X0, 4.56, CW, 0.4, "小、快、直——就这三件事。", 12.5, SUB)

feats = [("小", "699 KB / 333 KB"), ("快", "点对点直连"), ("直", "不经过任何服务器")]
y = 5.42
for k, v in feats:
    card(s, X0, y, CW, 1.02)
    textbox(s, X0 + 0.34, y + 0.22, 0.8, 0.6, k, 21, ACC, bold=True)
    textbox(s, X0 + 1.12, y + 0.38, CW - 1.4, 0.3, v, 9.8, SUB)
    y += 1.30

rect(s, X0, 9.36, CW, 0.01, fill=LINE)
textbox(s, X0, 9.58, CW, 0.9,
        "AirLink · 隔空快传\nWindows 与 Android 双端就绪", 9.2, MUTED, line=1.7)

NOTES = [
 "开场先给定位：这是一套让两台设备直接对话的工具，电脑端一个 exe、手机端一个 apk，"
 "中间不需要任何服务器。",
 "四个场景分别对应线、压缩、等待、跨网四种阻碍。共同点是：文件必须经过一个第三方。",
 "三个“不需要”是产品的边界：不注册、不上传、不依赖运行时。三个“直接”是体验的主线。",
 "体积是设计结果而不是目标：界面全部自绘，没有 WebView、没有 UI 框架、没有图片资源。",
 "六项能力是一条完整链路：发现决定能不能看见对方，连接决定怎么建立会话。",
 "局域网发现走 UDP 广播，只在同一广播域内有效——这也正是必须有广域网方案的原因。",
 "二维码里放的是完整连接串，手机端按内容自动判断走哪条路径。",
 "优先 IPv6：地址全球可达，不需要 NAT 穿透。没有公网 IPv6 时才退回 UPnP。",
 "把端口开到公网就必须有闸门：非内网来源必须先出示配对码，通过后十分钟免检。",
 "浏览是双向的，大文件按块传输，不会把整个文件读进内存。",
 "记录不是事后日志，而是传输过程的一部分，两端通过记录 id 对齐。",
 "熄屏掉线是这类工具最常见的问题，四层手段叠加，设置页可观测。",
 "这一页的数字都可以核对，不放无法验证的性能宣称。",
 "三层结构职责清晰，两端共用同一份协议定义，任何一端改错另一端会立刻暴露。",
 "对比不为了贬低别的工具，重点是说明中间那一层其实可以不存在。",
 "四个典型场景：换机搬运、照片回传、异地取文件、断网环境。",
 "收尾回到一句话：两台设备之间不该有中间商。小、快、直。",
]

for sl, note in zip(prs.slides, NOTES):
    sl.notes_slide.notes_text_frame.text = note

kinds = ["fade", "push", "wipe", "zoom", "cover", "fade", "push", "fade", "wipe"]
for i, sl in enumerate(prs.slides):
    slide_transition(sl, kinds[i % len(kinds)])

prs.save(OUT)
print("saved", OUT)
