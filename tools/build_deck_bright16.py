"""Bright portrait deck where no text is smaller than 16 pt."""
import os

from pptx import Presentation
from pptx.dml.color import RGBColor
from pptx.enum.shapes import MSO_SHAPE
from pptx.enum.text import MSO_ANCHOR, PP_ALIGN
from pptx.oxml.ns import qn
from pptx.util import Inches, Pt

ROOT = r"E:\AZ\deepseek H\gongzuoqu\AirLink"
OUT = os.path.join(ROOT, "AirLink-宣传-竖屏-亮色.pptx")
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
W, H = 7.5, 13.333
X0, CW = 0.55, 6.40
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
    textbox(slide, X0, 12.56, 4.2, 0.36, "AirLink · 隔空快传", 16, MUTED)
    textbox(slide, W - 1.30, 12.56, 0.75, 0.36, "%02d" % idx, 16, MUTED, align=PP_ALIGN.RIGHT)


def head(slide, kick, ttl, lead_text=None, kick_y=0.80, title_y=1.20, lead_y=2.50):
    textbox(slide, X0, kick_y, 5.8, 0.36, kick, 16, ACC, bold=True, spacing=1.2)
    textbox(slide, X0, title_y, CW, 1.05, ttl, 34, INK, bold=True, line=1.24)
    if lead_text:
        textbox(slide, X0, lead_y, CW, 0.9, lead_text, 17, SUB, line=1.5)


def card(slide, x, y, w, h, fill=CARD, line=LINE, radius=0.07):
    return rect(slide, x, y, w, h, fill=fill, line=line, radius=radius)


def transition(slide, kind="fade"):
    from lxml import etree
    xml = ('<p:transition xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main" '
           'spd="med"><p:%s/></p:transition>' % kind)
    slide._element.append(etree.fromstring(xml))


def gif_page(idx, kick, ttl, lead_text, bullets, gif, caption, extra=None):
    s = new_slide()
    head(s, kick, ttl, lead_text, lead_y=2.50)
    for i, (t, d) in enumerate(bullets):
        y = 3.42 + i * 1.02
        rect(s, X0, y + 0.14, 0.14, 0.14, fill=ACC, radius=0.5)
        textbox(s, X0 + 0.36, y, CW - 0.36, 0.44, t, 20, INK, bold=True)
        if d:
            textbox(s, X0 + 0.36, y + 0.46, CW - 0.36, 0.44, d, 16, SUB)
    s.shapes.add_picture(os.path.join(ASSETS, gif), Inches(X0), Inches(6.62), width=Inches(CW))
    textbox(s, X0, 10.42, CW, 0.4, caption, 16, MUTED)
    if extra:
        textbox(s, X0, 10.92, CW, 1.0, extra, 17, SUB, line=1.6)
    footer(s, idx)
    return s


# =================================================================== 01
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xE9, 0xEF, 0xFA))
badge = rect(s, X0, 2.86, 2.52, 0.50, fill=TINT, line=TINTL, radius=0.5)
shape_text(badge, "Windows · Android", 16, ACC, bold=True)

textbox(s, X0, 3.56, CW, 1.25, "隔空快传", 58, INK, bold=True, line=1.05)
textbox(s, X0, 4.98, CW, 0.46, "AirLink", 22, ACC, bold=True, spacing=3)
textbox(s, X0, 5.64, CW, 0.5, "两台设备之间，文件直达。", 21, INK, line=1.35)
textbox(s, X0, 6.36, CW, 1.05,
        "一个 exe、一个 apk，双击即用。\n不需要账号，不经过服务器。",
        17, SUB, line=1.62)

pills = ["局域网直连", "广域网可达", "无账号", "无服务器"]
for i, p in enumerate(pills):
    px = X0 + (i % 2) * 3.28
    py = 7.86 + (i // 2) * 0.68
    sh = rect(s, px, py, 3.06, 0.56, fill=CARD, line=LINE, radius=0.5)
    shape_text(sh, p, 17, SUB, bold=True)

rect(s, X0, 9.62, CW, 0.014, fill=RGBColor(0xD4, 0xDE, 0xEE))
textbox(s, X0, 9.90, CW, 1.0,
        "单文件运行 · 无运行时依赖\nWindows 与 Android 双端同协议",
        16, MUTED, line=1.7)

# =================================================================== 02
s = new_slide()
head(s, "THE PROBLEM", "传个文件\n为什么这么难？",
     "共同点：中间永远隔着别人。", lead_y=2.50)

pains = [("线", "找不到数据线"), ("压", "聊天软件会压缩"),
         ("等", "网盘要排队"), ("墙", "跨网络就断")]
y = 3.46
for glyph, t in pains:
    card(s, X0, y, CW, 1.62)
    ring = rect(s, X0 + 0.36, y + 0.40, 0.82, 0.82,
                fill=RGBColor(0xFD, 0xEF, 0xE6), line=RGBColor(0xF2, 0xCF, 0xB6), radius=0.5)
    shape_text(ring, glyph, 24, WARN, bold=True)
    textbox(s, X0 + 1.44, y + 0.56, CW - 1.8, 0.5, t, 21, INK, bold=True)
    y += 1.86
footer(s, 2)

# =================================================================== 03
s = new_slide()
head(s, "WHAT IT IS", "一个 exe，一个 apk\n两台设备直接对话", lead_y=2.50)

panels = [
    ("它不做的事", ["不需要账号", "不需要服务器", "不需要运行时"], WARN, 3.24, 3.66),
    ("它做的事", ["直接发现", "直接传输", "直接浏览"], GREEN, 7.16, 3.66),
]
for title_t, items, accent, py, ph in panels:
    card(s, X0, py, CW, ph)
    rect(s, X0, py, 0.09, ph, fill=accent)
    textbox(s, X0 + 0.42, py + 0.30, CW - 0.9, 0.42, title_t, 17, accent, bold=True)
    for j, it in enumerate(items):
        yy = py + 0.92 + j * 0.86
        rect(s, X0 + 0.42, yy + 0.12, 0.13, 0.13, fill=accent, radius=0.5)
        textbox(s, X0 + 0.72, yy, CW - 1.1, 0.44, it, 20, INK, bold=True)

textbox(s, X0, 11.10, CW, 0.9,
        "所有新功能都要落进这六个框里，\n否则不做。", 17, MUTED, line=1.65)
footer(s, 3)

# =================================================================== 04
s = new_slide()
head(s, "TWO BUILDERS", "两个程序\n都是单文件", "小到可以直接发过去。", lead_y=2.50)

for i, (os_name, size, unit, facts, accent) in enumerate([
    ("Windows", "699", "KB", ["单个 exe，静态链接", "无 DLL 依赖，拷走即用"], ACC),
    ("Android", "333", "KB", ["纯 Java，不用 AndroidX", "已签名，可直接安装"], ACC2),
]):
    y = 3.16 + i * 3.98
    card(s, X0, y, CW, 3.66)
    textbox(s, X0 + 0.44, y + 0.34, 3.0, 0.4, os_name, 17, accent, bold=True, spacing=1.5)
    textbox(s, X0 + 0.44, y + 0.86, 3.6, 1.15, size, 64, INK, bold=True)
    textbox(s, X0 + 2.86, y + 1.44, 1.4, 0.45, unit, 20, MUTED)
    for j, f in enumerate(facts):
        textbox(s, X0 + 0.44, y + 2.36 + j * 0.52, CW - 0.88, 0.44, "·  " + f, 17, SUB)

textbox(s, X0, 11.26, CW, 0.9,
        "体量来自取舍：界面全部自绘，\n零框架、零图片资源。", 17, MUTED, line=1.65)
footer(s, 4)

# =================================================================== 05
s = new_slide()
head(s, "CAPABILITIES", "六件事\n一件不少", "从发现到保活，构成完整链路。", lead_y=2.50)

caps = [
    ("01", "自动发现", "UDP 广播入列"),
    ("02", "扫码连接", "扫一下就连"),
    ("03", "广域网直连", "IPv6 免映射"),
    ("04", "双向浏览", "两端目录可见"),
    ("05", "记录同步", "开传即入记录"),
    ("06", "熄屏保活", "锁屏不掉线"),
]
cw2, ch2, gx, gy = 3.07, 2.42, 0.26, 0.30
for i, (num, t, d) in enumerate(caps):
    col, row = i % 2, i // 2
    x = X0 + col * (cw2 + gx)
    y = 3.16 + row * (ch2 + gy)
    card(s, x, y, cw2, ch2)
    textbox(s, x + 0.34, y + 0.44, 1.0, 0.36, num, 16, ACC, bold=True)
    textbox(s, x + 0.34, y + 0.92, cw2 - 0.68, 0.44, t, 20, INK, bold=True)
    textbox(s, x + 0.34, y + 1.52, cw2 - 0.68, 0.6, d, 16, SUB, line=1.4)
footer(s, 5)

gif_page(6, "STEP 01 · DISCOVERY", "打开就能\n看见对方", "不用问 IP，也不用加好友。",
         [("广播探询", "定时发一条 UDP 报文"), ("列表呈现", "几秒内出现，点一下发送")],
         "discovery_big.gif", "动图：设备发现（循环播放）",
         "同一 Wi-Fi、同一路由器，\n手机开热点也算。")



# =================================================================== 07
s = new_slide()
head(s, "STEP 02 · PAIRING", "扫一下就配好了", "二维码里装着完整连接信息。", lead_y=2.50)

card(s, X0, 3.16, 2.60, 2.60, fill=RGBColor(0xF2, 0xF6, 0xFD), line=TINTL, radius=0.08)
textbox(s, X0 + 0.1, 4.28, 2.4, 0.44, "二维码", 17, ACC, bold=True, align=PP_ALIGN.CENTER)

textbox(s, X0, 6.10, CW, 0.44, "两种码，一个入口", 20, INK, bold=True)
textbox(s, X0, 6.72, CW, 1.5,
        "局域网码：airlink://192.168.1.5:45678\n广域网码：后面加 ?code= 配对码",
        16, SUB, line=1.85)
textbox(s, X0, 8.30, CW, 1.1,
        "手机按内容自动区分：\n局域网码直接建会话，广域网码先过配对码。",
        17, SUB, line=1.65)

card(s, X0, 9.76, CW, 1.72, fill=TINT, line=TINTL)
textbox(s, X0 + 0.40, 10.04, CW - 0.8, 0.44, "为什么用二维码", 20, INK, bold=True)
textbox(s, X0 + 0.40, 10.58, CW - 0.8, 0.8, "IPv6 地址 39 个字符，手输几乎不可能。", 17, SUB, line=1.5)
footer(s, 7)

gif_page(8, "STEP 03 · WIDE AREA", "不在同一网络\n也能直连", "优先 IPv6，其次端口映射。",
         [("IPv6 直连", "地址全球可达，无需穿透"), ("自动兜底", "无 IPv6 就请路由器开端口")],
         "direct_big.gif", "动图：点对点直连（循环播放）")

# =================================================================== 09
s = new_slide()
head(s, "SECURITY", "开在公网上\n也不是谁都能进", "把端口开到互联网，就必须有闸门。", lead_y=2.50)

steps = [("非内网来源", "地址不在 10. / 192.168. 段内"),
         ("必须出示配对码", "6 位数字，与电脑上一致"),
         ("校验通过", "该地址 10 分钟内免检")]
y = 3.36
for i, (t, d) in enumerate(steps):
    card(s, X0, y, CW, 1.72)
    num = rect(s, X0 + 0.36, y + 0.44, 0.84, 0.84, fill=TINT, line=TINTL, radius=0.5)
    shape_text(num, str(i + 1), 20, ACC, bold=True)
    textbox(s, X0 + 1.40, y + 0.36, CW - 1.7, 0.44, t, 20, INK, bold=True)
    textbox(s, X0 + 1.40, y + 0.92, CW - 1.7, 0.5, d, 16, SUB)
    y += 1.92

card(s, X0, 9.32, CW, 1.86, fill=RGBColor(0xE8, 0xF7, 0xF0), line=RGBColor(0xB6, 0xE0, 0xCB))
textbox(s, X0 + 0.40, 9.60, CW - 0.8, 0.44, "内网不受影响", 20, GREEN, bold=True)
textbox(s, X0 + 0.40, 10.16, CW - 0.8, 0.8, "同一局域网仍免密直连，\n安全不该让日常变麻烦。", 17, SUB, line=1.55)
footer(s, 9)

# =================================================================== 10
s = new_slide()
head(s, "BROWSE", "两边的文件\n都像在自己电脑上", "同一套目录树，双向可读。", lead_y=2.50)

cols = [("手机看电脑", ["进入任意目录", "下载文件到手机"], ACC),
        ("电脑看手机", ["浏览手机存储", "取回照片与文档"], ACC2)]
for i, (t, items, accent) in enumerate(cols):
    y = 3.30 + i * 2.94
    card(s, X0, y, CW, 2.70)
    rect(s, X0, y, 0.09, 2.70, fill=accent)
    textbox(s, X0 + 0.42, y + 0.28, CW - 0.9, 0.44, t, 20, INK, bold=True)
    for j, it in enumerate(items):
        textbox(s, X0 + 0.42, y + 0.96 + j * 0.58, CW - 0.9, 0.44, "·  " + it, 17, SUB)

card(s, X0, 9.24, CW, 1.66, fill=TINT, line=TINTL)
textbox(s, X0 + 0.40, 9.50, CW - 0.8, 1.1,
        "大文件按块流式传输，不占内存；\n中断后可以重新发起。", 17, SUB, line=1.55)
footer(s, 10)

gif_page(11, "TRANSFER", "一开始传\n记录里就有了", "不用等结束才知道。",
         [("即时入列", "启动瞬间写入记录"), ("实时进度", "进度、速度同步刷新")],
         "transfer_big.gif", "动图：传输进度（循环播放）")

# =================================================================== 12
s = new_slide()
head(s, "ALWAYS ON", "熄屏之后\n链路还在", "锁屏被系统掐断，是最常见的死法。", lead_y=2.50)

items = [("前台服务", ACC), ("唤醒锁", ACC), ("Wi-Fi 锁", ACC2), ("锁屏重武装", ACC2)]
y = 3.34
for t, accent in items:
    card(s, X0, y, CW, 1.62)
    rect(s, X0, y, 0.09, 1.62, fill=accent)
    textbox(s, X0 + 0.44, y + 0.52, CW - 0.9, 0.5, t, 21, INK, bold=True)
    y += 1.84

textbox(s, X0, y + 0.10, CW, 0.9,
        "设置页能直接看到保活是否成功，\n出问题可以自己判断。", 17, MUTED, line=1.65)
footer(s, 12)

# =================================================================== 13
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xEC, 0xF1, 0xF9))
head(s, "BY THE NUMBERS", "几个能对得上的数字", "全部取自实际构建产物。", lead_y=2.50)

stats = [("699", "KB", "电脑端体积"), ("333", "KB", "手机端体积"),
         ("0", "个", "运行时依赖"), ("6", "字节", "协议头开销"),
         ("2", "个", "网络端口"), ("10", "分钟", "配对码有效期")]
cw3, ch3, gx3, gy3 = 3.07, 2.42, 0.26, 0.30
for i, (num, unit, label) in enumerate(stats):
    col, row = i % 2, i // 2
    x = X0 + col * (cw3 + gx3)
    y = 3.16 + row * (ch3 + gy3)
    card(s, x, y, cw3, ch3)
    textbox(s, x + 0.36, y + 0.52, 2.7, 1.0, num, 52, INK, bold=True)
    textbox(s, x + 0.36 + len(num) * 0.40 + 0.08, y + 0.96, 1.3, 0.4, unit, 18, ACC, bold=True)
    textbox(s, x + 0.36, y + 1.68, cw3 - 0.72, 0.42, label, 17, MUTED)
footer(s, 13)

# =================================================================== 14
s = new_slide()
head(s, "ARCHITECTURE", "三层结构\n两端逐字节一致", "两端共用同一份协议定义。", lead_y=2.50)

layers = [("发现层", "UDP 45679", "广播探询与应答", ACC, 1.98),
          ("传输层", "TCP 45678", "ALFT 魔数 + 类型", ACC2, 1.98),
          ("应用层", "12 类消息", "推文件·列目录·心跳\n会话·删除·记录同步", GREEN, 2.42)]
y = 3.18
for name, port, desc, accent, h in layers:
    card(s, X0, y, CW, h)
    rect(s, X0, y, 0.09, h, fill=accent)
    textbox(s, X0 + 0.42, y + 0.30, 2.3, 0.44, name, 20, INK, bold=True)
    tag = rect(s, X0 + 2.86, y + 0.28, 1.94, 0.46, fill=TINT, line=TINTL, radius=0.5)
    shape_text(tag, port, 16, accent, bold=True)
    textbox(s, X0 + 0.42, y + 0.94, CW - 0.9, 1.2, desc, 17, SUB, line=1.5)
    y += h + 0.28

textbox(s, X0, y + 0.12, CW, 0.9,
        "协议只有 6 字节头部，其余全是负载。", 17, MUTED, line=1.6)
footer(s, 14)

# =================================================================== 15
s = new_slide()
head(s, "COMPARISON", "换一种方式看它", "没有一种工具适合所有场景。", lead_y=2.50)

cols = ["维度", "AirLink", "聊天", "网盘"]
col_x = [X0, X0 + 2.02, X0 + 3.56, X0 + 4.96]
col_w = [2.02, 1.54, 1.40, 1.40]
rows = [("需要账号", "否", "是", "是"),
        ("需要联网", "否", "是", "是"),
        ("经服务器", "否", "是", "是"),
        ("压缩画质", "不压缩", "会压缩", "不压缩"),
        ("跨网络", "支持", "支持", "支持"),
        ("免安装", "单文件", "需安装", "需安装")]
hy = 3.22
for i, c in enumerate(cols):
    fill = RGBColor(0xD8, 0xE4, 0xFB) if i == 1 else RGBColor(0xEC, 0xF1, 0xF9)
    sh = rect(s, col_x[i], hy, col_w[i], 0.58, fill=fill, line=None, radius=0.14)
    shape_text(sh, c, 16, ACC if i == 1 else SUB, bold=True)

for r, (label, *vals) in enumerate(rows):
    y = hy + 0.72 + r * 0.90
    if r % 2 == 0:
        rect(s, X0, y - 0.10, CW, 0.84, fill=RGBColor(0xEE, 0xF3, 0xFB), line=None)
    textbox(s, col_x[0] + 0.10, y + 0.16, col_w[0] - 0.14, 0.44, label, 16, SUB)
    for c, v in enumerate(vals):
        good = (c == 0 and v in ("否", "不压缩", "支持", "单文件"))
        col = GREEN if good else (INK if c == 0 else MUTED)
        textbox(s, col_x[c + 1] + 0.06, y + 0.16, col_w[c + 1] - 0.08, 0.44,
                v, 16, col, bold=good)

textbox(s, X0, 9.34, CW, 1.6,
        "数据线不联网、不压缩，但只能插同一台机器。\n"
        "如果目标只是把文件从 A 送到 B，\n中间那一层其实可以不存在。", 17, SUB, line=1.7)
footer(s, 15)

# =================================================================== 16
s = new_slide()
head(s, "USE CASES", "什么时候用得上", "两台设备在同一个屏幕前，或同一个人在两地。", lead_y=2.50)

scenes = ["办公室换机", "照片回传", "出差在外", "断网环境"]
y = 3.30
for i, t in enumerate(scenes):
    card(s, X0, y, CW, 1.86)
    num = rect(s, X0 + 0.36, y + 0.50, 0.88, 0.88, fill=TINT, line=TINTL, radius=0.5)
    shape_text(num, str(i + 1), 20, ACC, bold=True)
    textbox(s, X0 + 1.46, y + 0.62, CW - 1.8, 0.5, t, 21, INK, bold=True)
    y += 2.08
footer(s, 16)

# =================================================================== 17
s = new_slide(RGBColor(0xFA, 0xFB, 0xFE), RGBColor(0xE9, 0xEF, 0xFA))
textbox(s, X0, 2.42, CW, 1.25, "两台设备之间，", 38, INK, bold=True, line=1.25)
textbox(s, X0, 3.62, CW, 1.25, "不该有中间商。", 38, ACC, bold=True, line=1.25)
textbox(s, X0, 4.96, CW, 0.5, "小、快、直——就这三件事。", 17, SUB)

feats = [("小", "699 KB / 333 KB"), ("快", "点对点直连"), ("直", "不经任何服务器")]
y = 5.92
for k, v in feats:
    card(s, X0, y, CW, 1.40)
    textbox(s, X0 + 0.44, y + 0.34, 1.0, 0.8, k, 30, ACC, bold=True)
    textbox(s, X0 + 1.46, y + 0.52, CW - 1.8, 0.44, v, 17, SUB)
    y += 1.58

rect(s, X0, 10.42, CW, 0.014, fill=RGBColor(0xD4, 0xDE, 0xEE))
textbox(s, X0, 10.72, CW, 1.1,
        "AirLink · 隔空快传\nWindows 与 Android 双端就绪", 17, MUTED, line=1.75)

NOTES = [
 "开场定位：让两台设备直接对话，电脑一个 exe、手机一个 apk，中间不需要服务器。",
 "四个场景对应线、压缩、等待、跨网。共同点：中间永远隔着别人。",
 "三个不做的边界，三个直接的主线。",
 "体积是结果：界面自绘、零框架、零图片资源。",
 "六项能力构成一条完整链路。",
 "局域网走 UDP 广播，只在同一广播域有效。",
 "二维码里是完整连接串，手机自动判断走哪条路。",
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
