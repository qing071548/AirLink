// AirLink - custom drawn UI (GDI+)
#include "app.h"
#include "qr.h"
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// ------------------------------------------------------------------ theme
namespace C {
    const Color bg        (255, 12, 14, 18);
    const Color side      (255, 16, 19, 25);
    const Color card      (255, 22, 26, 33);
    const Color card2     (255, 18, 22, 29);
    const Color line      (255, 34, 40, 52);
    const Color lineSoft  (255, 28, 33, 43);
    const Color text      (255, 233, 237, 245);
    const Color text2     (255, 154, 164, 182);
    const Color text3     (255, 106, 117, 136);
    const Color accent    (255, 90, 141, 255);
    const Color accent2   (255, 124, 92, 255);
    const Color accentDim (40,  90, 141, 255);
    const Color green     (255, 63, 214, 140);
    const Color orange    (255, 240, 169, 59);
    const Color red       (255, 240, 96, 93);
    const Color hover     (255, 32, 38, 48);
    const Color pressed   (255, 40, 47, 60);
}

// ------------------------------------------------------------------ state
static int   g_page = 0;                 // 0 transfer, 1 history, 2 settings
static int   g_hoverId = -1, g_hoverArg = -1;
static int   g_ctxPeer = -1;             // right-click menu: peer index, -1 = closed
static float g_ctxX = 0, g_ctxY = 0;
static std::wstring g_devSel;            // device-management detail selection
static int   g_pressId = -1, g_pressArg = -1;
static int   g_w = 1000, g_h = 700;
static float g_s = 1.0f;
static bool  g_animating = false;
static bool  g_searchPulse = false;   // only the idle "searching" animation is running
static bool  g_searchPartial = false; // repainting just the animated rect
static bool  sHasScene = false;       // the back buffer holds a complete frame
static RectF g_searchRect;            // dirty region of the searching animation
static RectF g_searchC2;
static float g_searchTop = 0, g_searchBottom = 0;
static ULONGLONG g_lastFrame = 0;
static std::map<long long, float> g_anim;
static std::string g_selPeer;
static float g_scroll[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
static std::vector<std::wstring> g_hist;

// ---- remote file browser (page 3)
static std::mutex        g_brMu;
static std::string       g_brPeerId, g_brIp;
static int               g_brPort = 0;
static std::wstring      g_brPeerName;
static std::string       g_brPath = "/";
static std::vector<RemoteEntry> g_brEntries;
static std::wstring      g_brErr;
static int               g_brState = 0;    // 0 idle, 1 loading, 2 error, 3 ready
static int               g_brSel = -1;
static bool              g_brDlActive = false;
static std::wstring      g_brDlName;
static uint64_t          g_brDlDone = 0, g_brDlTotal = 0;
static int               g_brBack = 0;     // page to return to when leaving

// inline editing
static int          g_edit = 0;           // 0 none 1 name 2 port
static std::wstring g_editText;
static int          g_caret = 0;

enum {
    H_NONE = 0, H_MIN = 1, H_MAX, H_CLOSE,
    H_NAV_SEND = 10, H_NAV_HIST, H_NAV_SET, H_NAV_FILES,
    H_COPY = 20, H_OPENRX, H_REFRESH,
    H_NAV_WAN = 70,
    H_PEER = 100,
    H_PEER_SEND = 130,
    H_PEER_DISCONNECT = 140,
    H_PEER_CONNECT = 160,
    H_CTX_DELETE = 150,
    H_DROP = 40,
    H_TX_ACTION = 200,
    H_HIST_CLEAR = 50, H_HIST_OPEN, H_HIST_DEL,
    H_SET_NAME = 60, H_SET_PORT, H_SET_BROWSE, H_SET_OPEN_DIR,
    H_SET_AUTO, H_SET_ABOUTOpen, H_SET_ALLOW_BROWSE, H_SET_KEEP_AWAKE, H_SET_DEVICES,
    H_ABOUT_BACK = 400,
    H_DEV_BACK = 410, H_DEV_DELETE = 411, H_DEV_DELETE_ALL = 412,
    H_WAN_TOGGLE = 420, H_WAN_COPY = 421, H_WAN_ADDR = 422, H_WAN_CODE = 423, H_WAN_CONNECT = 424,
    H_FS_ENTRY = 300,      // arg = index into g_brEntries
    H_FS_UP = 320, H_FS_REFRESH, H_FS_DOWNLOAD, H_FS_HOME,
    H_OPEN_FILES = 340,    // the left-side card that opens the browser
};

struct Hit { float x, y, w, h; int id, arg; };
static std::vector<Hit> g_hits;

// ------------------------------------------------------------------ gdi+ init
static ULONG_PTR g_token = 0;
static std::map<long long, Font*> g_fonts;
static bool g_gdip = false;

static void ensure_gdiplus() {
    if (g_gdip) return;
    GdiplusStartupInput in;
    GdiplusStartup(&g_token, &in, nullptr);
    g_gdip = true;
}

static Font* F(float px, int style = 0) {
    long long key = (long long)(px * 10) * 4 + style;
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second;
    Font* f = new Font(L"Microsoft YaHei UI", px, style, UnitPixel);
    if (f->GetLastStatus() != Ok) {
        delete f;
        f = new Font(L"Segoe UI", px, style, UnitPixel);
    }
    g_fonts[key] = f;
    return f;
}
static std::map<long long, Font*> g_monoFonts;
static Font* FM(float px, int style = 0) {
    long long key = (long long)(px * 10) * 4 + style;
    auto it = g_monoFonts.find(key);
    if (it != g_monoFonts.end()) return it->second;
    Font* f = new Font(L"Consolas", px, style, UnitPixel);
    g_monoFonts[key] = f;
    return f;
}
static void reset_fonts() {
    for (auto& kv : g_fonts) delete kv.second;
    g_fonts.clear();
    for (auto& kv : g_monoFonts) delete kv.second;
    g_monoFonts.clear();
}

// ------------------------------------------------------------------ primitives
static float S(float v) { return v * g_s; }

static GraphicsPath* rp(const RectF& r, float rad) {
    GraphicsPath* p = new GraphicsPath();
    float d = rad * 2;
    if (r.Width <= 0 || r.Height <= 0) return p;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    p->AddArc(r.X, r.Y, d, d, 180, 90);
    p->AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    p->AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    p->AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
}

static void fill_round(Graphics& g, const RectF& r, float rad, const Color& c) {
    GraphicsPath* p = rp(r, rad);
    SolidBrush b(c);
    g.FillPath(&b, p);
    delete p;
}
static void stroke_round(Graphics& g, const RectF& r, float rad, const Color& c, float w = 1.0f, DashStyle ds = DashStyleSolid) {
    GraphicsPath* p = rp(r, rad);
    Pen pen(c, w);
    pen.SetDashStyle(ds);
    g.DrawPath(&pen, p);
    delete p;
}

static void hline(Graphics& g, float x, float y, float w, const Color& c) {
    Pen pen(c, 1.0f);
    g.DrawLine(&pen, x, y + 0.5f, x + w, y + 0.5f);
}

static void text(Graphics& g, const std::wstring& s, RectF r, Font* f, const Color& c,
                 int align = 0, bool clip = true) {
    StringFormat sf;
    sf.SetAlignment(align == 0 ? StringAlignmentNear : align == 1 ? StringAlignmentCenter : StringAlignmentFar);
    sf.SetLineAlignment(StringAlignmentCenter);
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    sf.SetHotkeyPrefix(HotkeyPrefixNone);
    if (clip) sf.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush b(c);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &sf, &b);
}

static float text_w(Graphics& g, const std::wstring& s, Font* f) {
    StringFormat sf;
    sf.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsMeasureTrailingSpaces);
    RectF out;
    g.MeasureString(s.c_str(), (INT)s.size(), f, PointF(0, 0), &sf, &out);
    return out.Width;
}

static Color mix(const Color& a, const Color& b, float t) {
    if (t < 0) t = 0; if (t > 1) t = 1;
    return Color((BYTE)(a.GetA() + (b.GetA() - a.GetA()) * t),
                 (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t),
                 (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t),
                 (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
}
static Color with_alpha(const Color& c, int a) { return Color((BYTE)a, c.GetR(), c.GetG(), c.GetB()); }

static Color hue_color(float hue, float sat, float val) {
    float h = hue * 6.0f;
    int i = (int)floorf(h); float f = h - i;
    float p = val * (1 - sat), q = val * (1 - f * sat), t = val * (1 - (1 - f) * sat);
    float r = 0, gg = 0, b = 0;
    switch (i % 6) {
    case 0: r = val; gg = t; b = p; break;
    case 1: r = q; gg = val; b = p; break;
    case 2: r = p; gg = val; b = t; break;
    case 3: r = p; gg = q; b = val; break;
    case 4: r = t; gg = p; b = val; break;
    default: r = val; gg = p; b = q; break;
    }
    return Color(255, (BYTE)(r * 255), (BYTE)(gg * 255), (BYTE)(b * 255));
}

// ------------------------------------------------------------------ icons
enum Icon {
    I_SEND, I_CLOCK, I_SLIDERS, I_FILE, I_FOLDER, I_WIFI, I_USB, I_COPY,
    I_REFRESH, I_CHECK, I_X, I_CHEV, I_DOWN, I_UP, I_TRASH, I_MIN,
    I_MAX, I_CLOSE, I_INBOX, I_LINK, I_SHIELD
};

static void icon(Graphics& g, Icon k, const RectF& box, const Color& c, float strokeW = 1.7f) {
    GraphicsState st = g.Save();
    g.TranslateTransform(box.X, box.Y);
    float sc = box.Width / 24.0f;
    g.ScaleTransform(sc, sc);
    Pen pen(c, strokeW);
    pen.SetLineJoin(LineJoinRound);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    SolidBrush br(c);
    switch (k) {
    case I_SEND: {
        GraphicsPath p;
        p.AddPolygon(new PointF[]{ PointF(3.2f,11.0f), PointF(21.0f,3.0f), PointF(14.4f,21.0f), PointF(11.4f,13.0f) }, 4);
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, 11.4f, 13.0f, 21.0f, 3.0f);
        break;
    }
    case I_CLOCK: {
        g.DrawEllipse(&pen, 3.5f, 3.5f, 17.0f, 17.0f);
        g.DrawLine(&pen, 12.0f, 7.2f, 12.0f, 12.2f);
        g.DrawLine(&pen, 12.0f, 12.2f, 15.6f, 14.2f);
        break;
    }
    case I_SLIDERS: {
        g.DrawLine(&pen, 3.5f, 7.0f, 20.5f, 7.0f);
        g.DrawLine(&pen, 3.5f, 12.0f, 20.5f, 12.0f);
        g.DrawLine(&pen, 3.5f, 17.0f, 20.5f, 17.0f);
        g.FillEllipse(&br, 8.2f, 4.7f, 4.6f, 4.6f);
        g.FillEllipse(&br, 13.6f, 9.7f, 4.6f, 4.6f);
        g.FillEllipse(&br, 6.2f, 14.7f, 4.6f, 4.6f);
        break;
    }
    case I_FILE: {
        GraphicsPath p;
        p.AddLine(6.5f, 2.8f, 13.6f, 2.8f);
        p.AddLine(13.6f, 2.8f, 18.5f, 7.7f);
        p.AddLine(18.5f, 7.7f, 18.5f, 21.0f);
        p.AddLine(18.5f, 21.0f, 6.5f, 21.0f);
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, 13.6f, 2.9f, 13.6f, 7.6f);
        g.DrawLine(&pen, 13.6f, 7.6f, 18.3f, 7.6f);
        break;
    }
    case I_FOLDER: {
        GraphicsPath p;
        p.AddLine(3.0f, 6.5f, 9.4f, 6.5f);
        p.AddLine(9.4f, 6.5f, 11.4f, 9.0f);
        p.AddLine(11.4f, 9.0f, 21.0f, 9.0f);
        p.AddLine(21.0f, 9.0f, 21.0f, 19.0f);
        p.AddLine(21.0f, 19.0f, 3.0f, 19.0f);
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        break;
    }
    case I_WIFI: {
        g.DrawArc(&pen, 2.0f, 6.0f, 20.0f, 20.0f, 215.0f, 110.0f);
        g.DrawArc(&pen, 6.0f, 10.0f, 12.0f, 12.0f, 215.0f, 110.0f);
        g.FillEllipse(&br, 10.6f, 17.2f, 2.8f, 2.8f);
        break;
    }
    case I_USB: {
        g.DrawLine(&pen, 12.0f, 6.0f, 12.0f, 20.0f);
        GraphicsPath head;
        head.AddLine(9.4f, 8.0f, 12.0f, 4.6f);
        head.AddLine(12.0f, 4.6f, 14.6f, 8.0f);
        g.DrawPath(&pen, &head);
        g.DrawLine(&pen, 12.0f, 15.0f, 7.6f, 11.6f);
        g.DrawLine(&pen, 7.6f, 11.6f, 7.6f, 9.2f);
        g.FillRectangle(&br, 6.0f, 6.4f, 3.2f, 3.2f);
        g.DrawLine(&pen, 12.0f, 12.0f, 16.4f, 8.6f);
        g.FillEllipse(&br, 14.9f, 5.0f, 3.4f, 3.4f);
        break;
    }
    case I_COPY: {
        GraphicsPath p;
        p.AddLine(8.5f, 8.5f, 8.5f, 3.5f);
        p.AddLine(8.5f, 3.5f, 20.5f, 3.5f);
        p.AddLine(20.5f, 3.5f, 20.5f, 15.5f);
        p.AddLine(20.5f, 15.5f, 15.5f, 15.5f);
        g.DrawPath(&pen, &p);
        GraphicsPath q;
        q.AddLine(3.5f, 8.5f, 15.5f, 8.5f);
        q.AddLine(15.5f, 8.5f, 15.5f, 20.5f);
        q.AddLine(15.5f, 20.5f, 3.5f, 20.5f);
        q.CloseFigure();
        g.DrawPath(&pen, &q);
        break;
    }
    case I_REFRESH: {
        g.DrawArc(&pen, 4.0f, 4.0f, 16.0f, 16.0f, 40.0f, 280.0f);
        GraphicsPath a;
        a.AddLine(17.4f, 2.6f, 19.8f, 6.6f);
        a.AddLine(19.8f, 6.6f, 15.4f, 7.4f);
        g.DrawPath(&pen, &a);
        break;
    }
    case I_CHECK: {
        g.DrawLine(&pen, 4.5f, 12.8f, 9.8f, 18.0f);
        g.DrawLine(&pen, 9.8f, 18.0f, 19.5f, 6.5f);
        break;
    }
    case I_X: {
        g.DrawLine(&pen, 6.5f, 6.5f, 17.5f, 17.5f);
        g.DrawLine(&pen, 17.5f, 6.5f, 6.5f, 17.5f);
        break;
    }
    case I_CHEV: {
        g.DrawLine(&pen, 9.5f, 5.5f, 16.0f, 12.0f);
        g.DrawLine(&pen, 16.0f, 12.0f, 9.5f, 18.5f);
        break;
    }
    case I_DOWN: {
        g.DrawLine(&pen, 12.0f, 3.5f, 12.0f, 15.0f);
        g.DrawLine(&pen, 6.8f, 10.0f, 12.0f, 15.2f);
        g.DrawLine(&pen, 17.2f, 10.0f, 12.0f, 15.2f);
        g.DrawLine(&pen, 4.0f, 19.5f, 20.0f, 19.5f);
        break;
    }
    case I_UP: {
        g.DrawLine(&pen, 12.0f, 15.0f, 12.0f, 3.5f);
        g.DrawLine(&pen, 6.8f, 8.5f, 12.0f, 3.3f);
        g.DrawLine(&pen, 17.2f, 8.5f, 12.0f, 3.3f);
        g.DrawLine(&pen, 4.0f, 19.5f, 20.0f, 19.5f);
        break;
    }
    case I_TRASH: {
        g.DrawLine(&pen, 4.0f, 6.5f, 20.0f, 6.5f);
        g.DrawLine(&pen, 9.5f, 6.5f, 9.5f, 3.8f);
        g.DrawLine(&pen, 9.5f, 3.8f, 14.5f, 3.8f);
        g.DrawLine(&pen, 14.5f, 3.8f, 14.5f, 6.5f);
        GraphicsPath p;
        p.AddLine(6.0f, 6.5f, 7.2f, 20.2f);
        p.AddLine(7.2f, 20.2f, 16.8f, 20.2f);
        p.AddLine(16.8f, 20.2f, 18.0f, 6.5f);
        g.DrawPath(&pen, &p);
        break;
    }
    case I_MIN:  g.DrawLine(&pen, 6.0f, 12.0f, 18.0f, 12.0f); break;
    case I_MAX:  g.DrawRectangle(&pen, 6.5f, 6.5f, 11.0f, 11.0f); break;
    case I_CLOSE: g.DrawLine(&pen, 6.8f, 6.8f, 17.2f, 17.2f); g.DrawLine(&pen, 17.2f, 6.8f, 6.8f, 17.2f); break;
    case I_INBOX: {
        GraphicsPath p;
        p.AddLine(3.2f, 13.0f, 3.2f, 19.0f);
        p.AddLine(3.2f, 19.0f, 20.8f, 19.0f);
        p.AddLine(20.8f, 19.0f, 20.8f, 13.0f);
        p.AddLine(20.8f, 13.0f, 16.4f, 13.0f);
        p.AddLine(16.4f, 13.0f, 14.8f, 15.6f);
        p.AddLine(14.8f, 15.6f, 9.2f, 15.6f);
        p.AddLine(9.2f, 15.6f, 7.6f, 13.0f);
        p.AddLine(7.6f, 13.0f, 3.2f, 13.0f);
        g.DrawPath(&pen, &p);
        g.DrawLine(&pen, 12.0f, 3.0f, 12.0f, 10.5f);
        g.DrawLine(&pen, 8.6f, 7.4f, 12.0f, 10.8f);
        g.DrawLine(&pen, 15.4f, 7.4f, 12.0f, 10.8f);
        break;
    }
    case I_LINK: {
        g.DrawArc(&pen, 2.6f, 8.6f, 12.0f, 12.0f, 90.0f, 180.0f);
        g.DrawArc(&pen, 9.4f, 3.4f, 12.0f, 12.0f, 270.0f, 180.0f);
        g.DrawLine(&pen, 8.4f, 12.0f, 15.6f, 12.0f);
        break;
    }
    case I_SHIELD: {
        GraphicsPath p;
        p.AddLine(12.0f, 2.6f, 19.6f, 5.8f);
        p.AddLine(19.6f, 5.8f, 19.6f, 12.2f);
        p.AddCurve(new PointF[]{ PointF(19.6f,17.4f), PointF(15.6f,20.4f), PointF(12.0f,21.4f),
                                 PointF(8.4f,20.4f), PointF(4.4f,17.4f), PointF(4.4f,12.2f) }, 6);
        p.AddLine(4.4f, 12.2f, 4.4f, 5.8f);
        p.CloseFigure();
        g.DrawPath(&pen, &p);
        break;
    }
    }
    g.Restore(st);
}

// ------------------------------------------------------------------ widgets
static long long akey(int id, int arg) { return ((long long)id << 20) | (arg & 0xFFFFF); }

static float anim_val(long long k, float target) {
    float& v = g_anim[k];
    v += (target - v) * 0.30f;
    if (fabsf(target - v) < 0.008f) v = target;
    else g_animating = true;
    return v;
}

static void add_hit(float x, float y, float w, float h, int id, int arg = -1) {
    g_hits.push_back({ x, y, w, h, id, arg });
}

static void switch_widget(Graphics& g, float x, float y, bool on, int id, float alpha) {
    float w = S(42), h = S(23);
    RectF r(x, y, w, h);
    Color track = on ? C::accent : Color(255, 48, 55, 68);
    if (on) track = mix(track, Color(255, 255, 255), alpha * 0.10f);
    else track = mix(track, Color(255, 255, 255), alpha * 0.08f);
    fill_round(g, r, h / 2, track);
    float kd = h - S(4);
    float kx = on ? x + w - kd - S(2) : x + S(2);
    SolidBrush sh(Color(40, 0, 0, 0));
    g.FillEllipse(&sh, kx, y + S(2) + S(1.5f), kd, kd);
    SolidBrush kb(Color(255, 255, 255, 255));
    g.FillEllipse(&kb, kx, y + S(2), kd, kd);
    add_hit(x - S(4), y - S(6), w + S(8), h + S(12), id);
}

static void ghost_button(Graphics& g, const RectF& r, const std::wstring& label, Icon ic,
                         int id, float alpha, float extraW = 0) {
    Color bg = mix(Color(12, 255, 255, 255), Color(40, 255, 255, 255), alpha);
    fill_round(g, r, S(9), bg);
    stroke_round(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), S(9),
                 mix(C::line, C::lineSoft, 0), 1.0f);
    Color tc = mix(C::text2, C::text, alpha);
    float tw = text_w(g, label, F(S(12.5f)));
    float total = tw + (ic != I_X ? S(18) : 0);
    float cx = r.X + (r.Width - total) / 2;
    if (ic != I_X) icon(g, ic, RectF(cx, r.Y + (r.Height - S(13)) / 2, S(13), S(13)), tc, 1.6f);
    text(g, label, RectF(cx + (ic != I_X ? S(18) : 0), r.Y, tw + S(2), r.Height), F(S(12.5f)), tc, 0, false);
    add_hit(r.X, r.Y, r.Width, r.Height, id);
}

static void icon_button(Graphics& g, const RectF& r, Icon ic, int id, int arg, float alpha,
                        float baseAlpha = 26, float hoverAlpha = 60) {
    if (alpha > 0.01f) fill_round(g, r, S(8), Color((BYTE)(baseAlpha + (hoverAlpha - baseAlpha) * alpha), 255, 255, 255));
    Color c = mix(C::text3, C::text, alpha);
    icon(g, ic, RectF(r.X + r.Width / 2 - S(8), r.Y + r.Height / 2 - S(8), S(16), S(16)), c, 1.7f);
    add_hit(r.X, r.Y, r.Width, r.Height, id, arg);
}

static void card(Graphics& g, const RectF& r) {
    fill_round(g, r, S(14), C::card);
    stroke_round(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), S(14), C::line, 1.0f);
}

static void avatar(Graphics& g, const RectF& r, const std::wstring& name, bool usb, bool dim = false) {
    float h = 0;
    for (wchar_t c : name) h += c * 37.0f;
    h = fmodf(h, 360.0f) / 360.0f;
    Color a = hue_color(h, 0.52f, 0.92f);
    Color b = hue_color(fmodf(h + 0.08f, 1.0f), 0.62f, 0.72f);
    if (dim) {
        a = Color(255, 70, 76, 88);
        b = Color(255, 56, 62, 74);
    }
    LinearGradientBrush lg(PointF(r.X, r.Y), PointF(r.GetRight(), r.GetBottom()), a, b);
    g.FillEllipse(&lg, r);
    SolidBrush tb(Color(255, 255, 255, 255));
    if (!name.empty()) {
        std::wstring ch(1, name[0]);
        if (ch[0] >= L'a' && ch[0] <= L'z') ch[0] -= 32;
        Font* f = F(r.Height * 0.46f, FontStyleBold);
        StringFormat sf; sf.SetAlignment(StringAlignmentCenter); sf.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(ch.c_str(), 1, f, r, &sf, &tb);
    }
}

// ------------------------------------------------------------------ page: transfer
// Animated "searching" placeholder. Drawn on its own so the paint loop can
// repaint just this rect while peers are being discovered.
static void draw_searching_block(Graphics& g, const RectF& c2, float listTop, float listBottom) {
    float cx = c2.X + c2.Width / 2;
    float R = S(23);                                  // badge radius
    float blockH = S(166);
    // Keep the block fully inside the list area even when the window is short.
    float avail = listBottom - listTop;
    if (avail < blockH) blockH = avail;
    float cy = listTop + (listBottom - listTop - blockH) / 2;
    if (cy < listTop) cy = listTop;
    float cyc = cy + R;                               // badge centre
    float t = (float)((GetTickCount64() % 600000) / 1000.0);
    g_searchPulse = true;
    // Bounds of everything that actually moves: ripples + badge + the dot row.
    // It deliberately stops above the first text line (cy+S(82)) so the partial
    // repaint can fill this rect with the card colour without erasing the text.
    g_searchRect = RectF(cx - R - S(24), cy - S(24), (R + S(24)) * 2, S(97));
    g_searchC2 = c2; g_searchTop = listTop; g_searchBottom = listBottom;

    // expanding ripples, kept clear of the dots below
    for (int i = 0; i < 2; i++) {
        float ph = t * 0.55f + i * 0.5f;
        ph -= floorf(ph);
        float r = R + ph * S(19);
        int a = (int)(72 * (1.0f - ph) * (1.0f - ph));
        if (a > 3) {
            Pen pen(Color((BYTE)a, 96, 148, 255), S(1.1f));
            g.DrawEllipse(&pen, cx - r, cyc - r, r * 2, r * 2);
        }
    }

    // breathing halo + glyph
    float br = 0.5f + 0.5f * sinf(t * 2.1f);
    fill_round(g, RectF(cx - R, cy, R * 2, R * 2), R, Color((BYTE)(30 + 18 * br), 34, 48, 78));
    icon(g, I_WIFI, RectF(cx - S(16), cy + S(7), S(32), S(32)),
         mix(Color(255, 58, 68, 86), Color(255, 116, 160, 245), 0.20f + 0.40f * br), 1.7f);

    // three dots lighting up in turn
    for (int i = 0; i < 3; i++) {
        float ph = t * 1.35f - i * 0.30f;
        ph -= floorf(ph);
        float lit = ph < 0.36f ? 1.0f - ph / 0.36f : 0.0f;
        float r = S(2.3f) + S(0.8f) * lit;
        float dx = cx + (i - 1) * S(11);
        float dy = cy + S(66) - S(3.5f) * lit;
        SolidBrush db(Color((BYTE)(64 + 176 * lit), 120, 165, 255));
        g.FillEllipse(&db, dx - r, dy - r, r * 2, r * 2);
    }

    // The two info lines are static; they are skipped on partial repaints, so
    // they are drawn only when the full scene is being built.
    if (!g_searchPartial) {
        text(g, L"正在搜索附近设备", RectF(c2.X, cy + S(82), c2.Width, S(22)), F(S(12.2f)), C::text2, 1, false);
        text(g, L"两台设备连到同一个 Wi-Fi 即可互相发现", RectF(c2.X + S(12), cy + S(106), c2.Width - S(24), S(20)), F(S(10.4f)), C::text3, 1);
        text(g, L"手机开启「USB 网络共享」也能直连", RectF(c2.X + S(12), cy + S(124), c2.Width - S(24), S(20)), F(S(10.4f)), C::text3, 1);
    }
}

// ------------------------------------------------------------------ QR card
// Encoded once and reused until the address or port actually changes.
static std::string              g_qrPayload;
static std::vector<unsigned char> g_qrMods;
static int                      g_qrN = 0;

// "airlink://<ip>:<port>?n=<name>" - the phone parses this after scanning.
// Prefers a real LAN address: a USB-tether address only works with the cable
// plugged in, so it is used only when no wireless address exists.
static std::string qr_payload(std::wstring* shown, bool* isUsb = nullptr) {
    std::string ip;
    bool usb = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        // Choose an address a phone on the same Wi-Fi can actually reach:
        // real adapters first, then USB tethering, never a host-only VM link.
        int bestRank = 99;
        for (auto& a : g_local) {
            int rank = a.virt ? 3 : (a.usb ? 2 : (a.primary ? 0 : 1));
            if (rank < bestRank) { bestRank = rank; ip = a.ip; usb = a.usb; }
        }
        if (bestRank == 3) ip.clear();     // only virtual links exist - do not lie
    }
    if (isUsb) *isUsb = usb;
    if (ip.empty()) return "";
    if (shown) *shown = utf8_to_w(ip) + L":" + std::to_wstring(g_set.port);

    std::string s = "airlink://" + ip + ":" + std::to_string(g_set.port);
    if (!g_set.deviceName.empty()) {
        s += "?n=";
        for (unsigned char c : w_to_utf8(g_set.deviceName)) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
                s += (char)c;
            } else {
                char b[8];
                snprintf(b, sizeof(b), "%%%02X", c);
                s += b;
            }
            if (s.size() > 150) break;     // keep the symbol within a sane version
        }
    }
    return s;
}

// "airlink://[<v6>]:<port>?code=<6 digits>" - the phone scans this from the WAN
// page and can connect from another network without typing anything.
static std::string g_wanPayload;
static std::vector<unsigned char> g_wanMods;
static int g_wanN = 0;

static std::string wan_qr_payload() {
    std::string v6;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& a : g_local) {
            if (a.ip.find(':') != std::string::npos) { v6 = a.ip; break; }
        }
    }
    if (v6.empty()) return "";
    return "airlink://[" + v6 + "]:" + std::to_string(g_set.port) + "?code=" + g_set.wanCode;
}

static void draw_qr_mods(Graphics& g, const RectF& box,
                         const std::vector<unsigned char>& mods, int n) {
    if (n <= 0 || mods.empty()) return;
    float avail = std::min(box.Width, box.Height);
    float mod = avail / (float)n;
    int ox = (int)floorf(box.X + (box.Width - mod * n) / 2.0f);
    int oy = (int)floorf(box.Y + (box.Height - mod * n) / 2.0f);
    SolidBrush dark(Color(255, 16, 20, 26));
    for (int row = 0; row < n; row++) {
        int y0 = oy + (int)floorf(row * mod);
        int y1 = oy + (int)floorf((row + 1) * mod);
        int col = 0;
        while (col < n) {
            if (!mods[row * n + col]) { col++; continue; }
            int start = col;
            while (col < n && mods[row * n + col]) col++;
            int x0 = ox + (int)floorf(start * mod);
            int x1 = ox + (int)floorf(col * mod);
            g.FillRectangle(&dark, x0, y0, x1 - x0, y1 - y0);
        }
    }
}
// Draws the symbol into `box`, snapped to whole pixels so it stays crisp.
static void draw_qr(Graphics& g, const RectF& box) {
    if (g_qrN <= 0 || g_qrMods.empty()) return;
    int n = g_qrN;
    float avail = std::min(box.Width, box.Height);
    float mod = avail / (float)n;
    int ox = (int)floorf(box.X + (box.Width - mod * n) / 2.0f);
    int oy = (int)floorf(box.Y + (box.Height - mod * n) / 2.0f);
    SolidBrush dark(Color(255, 16, 20, 26));
    for (int row = 0; row < n; row++) {
        int y0 = oy + (int)floorf(row * mod);
        int y1 = oy + (int)floorf((row + 1) * mod);
        int col = 0;
        while (col < n) {
            if (!g_qrMods[row * n + col]) { col++; continue; }
            int start = col;
            while (col < n && g_qrMods[row * n + col]) col++;
            int x0 = ox + (int)floorf(start * mod);
            int x1 = ox + (int)floorf(col * mod);
            g.FillRectangle(&dark, x0, y0, x1 - x0, y1 - y0);
        }
    }
}

// Returns the height the card needs, or 0 when there is no LAN address yet.
static float qr_card_height() { return S(176); }

static void draw_qr_card(Graphics& g, const RectF& c) {
    std::wstring shown;
    std::string payload = qr_payload(&shown);
    if (payload.empty()) return;

    if (payload != g_qrPayload) {
        int n = 0;
        std::vector<unsigned char> mods = qr_encode(payload, &n);
        if (n > 0) {
            g_qrPayload = payload;
            g_qrMods = mods;
            g_qrN = n;
        }
    }

    card(g, c);

    // white plate: the symbol needs a light background to scan reliably
    float side = S(148);
    RectF plate(c.X + S(14), c.Y + (c.Height - side) / 2, side, side);
    fill_round(g, plate, S(10), Color(255, 255, 255, 255));
    RectF inner(plate.X + S(14), plate.Y + S(14), plate.Width - S(28), plate.Height - S(28));
    draw_qr(g, inner);

    float tx = plate.GetRight() + S(18);
    float tw = c.GetRight() - S(16) - tx;
    float ty0 = c.Y + S(18);
    text(g, L"扫码连接手机", RectF(tx, ty0, tw, S(24)),
         F(S(14.0f), FontStyleBold), C::text, 0, false);
    text(g, L"手机 AirLink 扫左边的码", RectF(tx, ty0 + S(28), tw, S(20)),
         F(S(10.6f)), C::text3, 0);
    text(g, shown, RectF(tx, ty0 + S(52), tw, S(22)), FM(S(12.4f)), C::accent, 0);
    text(g, L"同一 Wi-Fi 下即可互传", RectF(tx, ty0 + S(78), tw, S(20)),
         F(S(10.4f)), C::text3, 0);

    // a small "clean" hint row, mirroring the settings-panel tone
    {
        RectF pill(tx, ty0 + S(104), std::min(tw, S(150)), S(26));
        fill_round(g, pill, S(8), Color(255, 26, 32, 44));
        icon(g, I_SHIELD, RectF(pill.X + S(9), pill.Y + S(7), S(12), S(12)), C::green, 1.5f);
        text(g, L"不经过服务器", RectF(pill.X + S(26), pill.Y, pill.Width - S(32), pill.Height),
             F(S(9.8f)), Color(255, 120, 190, 160), 0, false);
    }
}

static void draw_page_transfer(Graphics& g, float x0, float x1, float y0, float y1) {
    float leftW = S(342);
    if (x1 - x0 < S(760)) leftW = (x1 - x0) * 0.42f;
    float rightX = x0 + leftW + S(16);
    float rightW = x1 - rightX;
    float cy = y0 + S(44);

    // ---------------- card: this device
    RectF c1(x0, cy, leftW, S(214));
    card(g, c1);
    float px = c1.X + S(18), py = c1.Y + S(16);

    avatar(g, RectF(px, py, S(42), S(42)), g_set.deviceName, false);

    std::wstring nm = g_set.deviceName;
    text(g, nm, RectF(px + S(54), py + S(1), leftW - S(54) - S(76), S(22)), F(S(14.5f), FontStyleBold), C::text, 0);
    text(g, L"本机 · Windows", RectF(px + S(54), py + S(23), leftW - S(54) - S(20), S(18)), F(S(11.0f)), C::text3, 0);


    // status pill
    {
        float pw = S(58), ph = S(22);
        RectF pr(c1.GetRight() - S(18) - pw, py + S(9), pw, ph);
        fill_round(g, pr, ph / 2, Color(255, 30, 46, 38));
        SolidBrush d(Color(255, 63, 214, 140));
        g.FillEllipse(&d, pr.X + S(9), pr.Y + ph / 2 - S(2.5f), S(5), S(5));
        text(g, L"在线", RectF(pr.X + S(18), pr.Y, pw - S(22), ph), F(S(10.5f)), Color(255, 90, 200, 148), 0, false);
    }

    hline(g, c1.X + S(18), c1.Y + S(72), leftW - S(36), C::lineSoft);

    // addresses
    std::vector<LocalAddr> locals;
    { std::lock_guard<std::mutex> lk(g_mu); locals = g_local; }
    float ay = c1.Y + S(84);
    if (locals.empty()) {
        text(g, L"未检测到网络连接", RectF(px, ay, leftW - S(36), S(22)), F(S(12.0f)), C::text3, 0);
        ay += S(26);
    }
    int shown = 0;
    for (auto& la : locals) {
        if (shown >= 3) break;
        shown++;
        RectF ib(px, ay + S(2), S(15), S(15));
        icon(g, la.usb ? I_USB : I_WIFI, ib, (la.usb || la.primary) ? C::accent : C::text3, 1.5f);
        std::wstring addr = utf8_to_w(la.ip) + L":" + std::to_wstring(g_set.port);
        text(g, addr, RectF(px + S(23), ay, leftW - S(23) - S(70), S(20)),
             FM(S(12.0f)), la.primary ? C::text : C::text2, 0);
        if (la.usb) {
            RectF tag(c1.GetRight() - S(18) - S(52), ay + S(1), S(52), S(18));
            fill_round(g, tag, S(6), Color(255, 34, 46, 74));
            text(g, L"USB 直连", RectF(tag.X, tag.Y, tag.Width, tag.Height), F(S(9.5f)), Color(255, 126, 160, 255), 1, false);
        } else if (la.primary) {
            RectF tag(c1.GetRight() - S(18) - S(46), ay + S(1), S(46), S(18));
            fill_round(g, tag, S(6), Color(255, 28, 42, 40));
            text(g, L"当前", RectF(tag.X, tag.Y, tag.Width, tag.Height), F(S(9.5f)), Color(255, 90, 205, 150), 1, false);
        }
        ay += S(25);
    }

    // bottom buttons: copy address + device files
    {
        float by = c1.GetBottom() - S(18) - S(34);
        float fw = S(104);
        RectF cb(c1.X + S(18), by, leftW - S(36) - fw - S(8), S(34));
        RectF fb(c1.GetRight() - S(18) - fw, by, fw, S(34));

        float a = anim_val(akey(H_COPY, -1), (g_hoverId == H_COPY) ? 1.f : 0.f);
        Color bg = mix(Color(10, 255, 255, 255), Color(34, 90, 141, 255), a);
        fill_round(g, cb, S(9), bg);
        Color tc = mix(C::text2, C::text, a);
        icon(g, I_COPY, RectF(cb.X + S(12), cb.Y + S(10), S(14), S(14)), tc, 1.6f);
        text(g, L"复制地址", RectF(cb.X + S(34), cb.Y, cb.Width - S(44), cb.Height), F(S(12.0f)), tc, 0);
        add_hit(cb.X, cb.Y, cb.Width, cb.Height, H_COPY);

        float fa = anim_val(akey(H_OPEN_FILES, -1), (g_hoverId == H_OPEN_FILES) ? 1.f : 0.f);
        bool hasPeer = false;
        { std::lock_guard<std::mutex> lk(g_mu); hasPeer = !g_peers.empty(); }
        Color fbg = hasPeer ? mix(Color(255, 34, 44, 66), Color(255, 48, 74, 122), fa)
                            : Color(255, 24, 28, 36);
        fill_round(g, fb, S(9), fbg);
        if (hasPeer) stroke_round(g, RectF(fb.X + 0.5f, fb.Y + 0.5f, fb.Width - 1, fb.Height - 1),
                                  S(9), Color(255, 52, 74, 122), 1.0f);
        Color fc = hasPeer ? mix(Color(255, 160, 190, 255), Color(255, 205, 220, 255), fa)
                           : Color(255, 78, 88, 104);
        icon(g, I_FOLDER, RectF(fb.X + S(10), fb.Y + S(9), S(15), S(15)), fc, 1.7f);
        text(g, L"设备文件", RectF(fb.X + S(30), fb.Y, fb.Width - S(38), fb.Height),
             F(S(11.4f)), fc, 0, false);
        add_hit(fb.X, fb.Y, fb.Width, fb.Height, H_OPEN_FILES);
    }

    // ---------------- card: nearby
    float py2 = c1.GetBottom() + S(16);
    RectF c2(x0, py2, leftW, y1 - py2);
    card(g, c2);

    text(g, L"附近设备", RectF(c2.X + S(18), c2.Y + S(10), S(120), S(28)), F(S(13.0f), FontStyleBold), C::text, 0);

    int peerCount = 0;
    { std::lock_guard<std::mutex> lk(g_mu); peerCount = (int)g_peers.size(); }
    if (peerCount > 0) {
        float nw = S(20);
        RectF nb(c2.X + S(18) + text_w(g, L"附近设备", F(S(13.0f), FontStyleBold)) + S(8), c2.Y + S(15), nw, S(18));
        fill_round(g, nb, S(9), Color(255, 34, 42, 56));
        text(g, std::to_wstring(peerCount), nb, F(S(10.5f)), C::text2, 1, false);
    }
    {
        RectF rb(c2.GetRight() - S(18) - S(28), c2.Y + S(10), S(28), S(28));
        float a = anim_val(akey(H_REFRESH, -1), g_hoverId == H_REFRESH ? 1.f : 0.f);
        icon_button(g, rb, I_REFRESH, H_REFRESH, -1, a, 0, 46);
    }

    // list
    float listTop = c2.Y + S(46);
    float listBottom = c2.GetBottom() - S(12);
    g.SetClip(RectF(c2.X + S(1), listTop, c2.Width - S(2), listBottom - listTop));
    std::vector<Peer> peers;
    { std::lock_guard<std::mutex> lk(g_mu); peers = g_peers; }
    if (peers.empty()) {
        draw_searching_block(g, c2, listTop, listBottom);
    } else {
        float ry = listTop;
        for (size_t i = 0; i < peers.size(); i++) {
            if (ry > listBottom) break;
            float rh = S(58);
            RectF rr(c2.X + S(10), ry, c2.Width - S(20), rh - S(6));
            bool down = peers[i].linkDown;
            bool sel = (g_selPeer == peers[i].id) && !down;
            float ha = anim_val(akey(H_PEER, (int)i), (g_hoverId == H_PEER && g_hoverArg == (int)i) ? 1.f : 0.f);
            if (sel) {
                fill_round(g, rr, S(10), Color(255, 30, 40, 64));
                stroke_round(g, RectF(rr.X + 0.5f, rr.Y + 0.5f, rr.Width - 1, rr.Height - 1), S(10), Color(255, 44, 62, 104), 1.0f);
            } else if (ha > 0.01f) {
                fill_round(g, rr, S(10), Color((BYTE)(255 * ha), 28, 33, 43));
            }
            avatar(g, RectF(rr.X + S(10), rr.Y + (rr.Height - S(34)) / 2, S(34), S(34)),
                   utf8_to_w(peers[i].name), peers[i].usb, down);
            std::wstring name = utf8_to_w(peers[i].name);
            float tx = rr.X + S(54);
            float btnW = S(60);
            text(g, name, RectF(tx, rr.Y + S(8), rr.Width - S(54) - btnW - S(10), S(20)),
                 F(S(12.8f)), down ? C::text3 : C::text, 0);
            std::wstring sub = utf8_to_w(peers[i].ip);
            if (peers[i].usb) sub += L" · USB";
            text(g, sub, RectF(tx, rr.Y + S(28), rr.Width - S(54) - btnW - S(10), S(18)), F(S(10.5f)), C::text3, 0);

            RectF sb(rr.GetRight() - S(10) - btnW, rr.Y + (rr.Height - S(28)) / 2, btnW, S(28));
            // "connected" is a shared session: whichever side started it, both
            // lists show the same state.
            bool conn = false;
            {
                std::string sid2 = net_session_id();
                conn = !sid2.empty() ? net_session_is(peers[i].ip)
                                     : (g_selPeer == peers[i].id);
            }
            if (conn) {
                // status chip at the far right, actions to its left
                float cw = S(64);
                RectF chip(rr.GetRight() - S(10) - cw, rr.Y + (rr.Height - S(26)) / 2, cw, S(26));
                fill_round(g, chip, S(13), Color(46, 54, 206, 130));
                text(g, L"已连接", chip, F(S(10.6f)), Color(255, 120, 226, 168), 1, false);

                float dw = S(54);
                RectF db(rr.GetRight() - S(10) - cw - S(8) - dw, sb.Y, dw, S(28));
                float da = anim_val(akey(H_PEER_DISCONNECT, (int)i),
                                    (g_hoverId == H_PEER_DISCONNECT && g_hoverArg == (int)i) ? 1.f : 0.f);
                fill_round(g, db, S(8), mix(Color(255, 40, 32, 34), Color(255, 72, 38, 40), da));
                text(g, L"断开", db, F(S(10.6f)),
                     mix(Color(255, 196, 108, 104), Color(255, 240, 140, 132), da), 1, false);
                add_hit(db.X, db.Y, db.Width, db.Height, H_PEER_DISCONNECT, (int)i);

                // no send button here: files are dropped on the zone on the right
            } else if (down) {
                // peer walked away: keep the row, show a muted "disconnected" chip
                fill_round(g, sb, S(8), Color(255, 34, 28, 30));
                text(g, L"已断开", sb, F(S(11.0f)), Color(255, 196, 108, 104), 1, false);
            } else {
                // not connected: one button only, and it is "连接"
                float cw = S(62);
                RectF cb(rr.GetRight() - S(10) - cw, rr.Y + (rr.Height - S(28)) / 2, cw, S(28));
                float ca = anim_val(akey(H_PEER_CONNECT, (int)i),
                                    (g_hoverId == H_PEER_CONNECT && g_hoverArg == (int)i) ? 1.f : 0.f);
                fill_round(g, cb, S(8), mix(Color(50, 90, 141, 255), Color(90, 110, 162, 255), ca));
                text(g, L"连接", cb, F(S(11.2f)),
                     mix(Color(255, 140, 176, 255), Color(255, 186, 210, 255), ca), 1, false);
                add_hit(cb.X, cb.Y, cb.Width, cb.Height, H_PEER_CONNECT, (int)i);
            }
            add_hit(rr.X, rr.Y, rr.Width - S(150), rr.Height, H_PEER, (int)i);
            ry += rh;
        }
    }
    g.ResetClip();

    // ---------------- QR card (only when there is a LAN address to share)
    float listTopRight = cy;
    {
        std::wstring shown;
        bool hasAddr = !qr_payload(&shown).empty();
        float avail = y1 - cy;
        if (hasAddr && avail >= S(470)) {
            RectF qc(rightX, cy, rightW, S(190));
            draw_qr_card(g, qc);
            listTopRight = qc.GetBottom() + S(14);
        }
    }

    // ---------------- drop zone
    float dzH = (listTopRight > cy) ? S(132) : S(184);
    RectF dz(rightX, listTopRight, rightW, dzH);
    {
        bool hover = (g_hoverId == H_DROP);
        float a = anim_val(akey(H_DROP, -1), hover ? 1.f : 0.f);
        fill_round(g, dz, S(14), mix(C::card2, Color(255, 24, 33, 52), a));
        Color border = mix(Color(255, 56, 65, 84), C::accent, a * 0.9f);
        stroke_round(g, RectF(dz.X + 1, dz.Y + 1, dz.Width - 2, dz.Height - 2), S(13), border, 1.3f, DashStyleDash);

        std::wstring target;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& p : g_peers) if (p.id == g_selPeer) { target = utf8_to_w(p.name); break; }
        }

        float cx = dz.X + dz.Width / 2;
        float shrink = (dzH < S(160)) ? S(0.62f) : 1.0f;
        float iconSz = S(52) * shrink;
        float titleH = S(24);
        bool showSub = dzH >= S(160);
        // the whole block is measured first so it can be centred as a unit
        int subLines = 0;
        if (showSub) subLines = target.empty() ? 2 : 1;
        float subH = subLines > 0 ? (subLines * S(20) + S(8)) : 0.f;
        float blockH = iconSz + S(14) + titleH + subH;
        float startY = dz.Y + std::max(S(12), (dz.Height - blockH) / 2.f);

        RectF circ(cx - iconSz / 2, startY, iconSz, iconSz);
        fill_round(g, circ, iconSz / 2, Color((BYTE)(255 - 120 * a), 30, 38, 60));
        float icSz = iconSz * 0.46f;
        icon(g, target.empty() ? I_INBOX : I_SEND,
             RectF(circ.X + (iconSz - icSz) / 2, circ.Y + (iconSz - icSz) / 2, icSz, icSz),
             mix(Color(255, 122, 150, 214), C::accent, a), 1.6f);

        float t1 = circ.GetBottom() + S(14);
        if (target.empty()) {
            text(g, L"拖放文件到此处", RectF(dz.X, t1, dz.Width, titleH), F(S(14.0f), FontStyleBold), C::text, 1, false);
            if (showSub) {
                text(g, L"右侧列表出现设备后，选择设备即可发送", RectF(dz.X + S(10), t1 + S(28), dz.Width - S(20), S(20)),
                     F(S(10.8f)), C::text3, 1);
                text(g, L"也可以点击此处先选择要发送的文件", RectF(dz.X + S(10), t1 + S(48), dz.Width - S(20), S(20)),
                     F(S(10.8f)), C::text3, 1);
            }
        } else {
            text(g, L"拖放文件，发送到 " + target, RectF(dz.X, t1, dz.Width, titleH), F(S(14.0f), FontStyleBold), C::text, 1);
            if (showSub)
                text(g, L"或点击此处选择要发送的文件 · 支持任意格式", RectF(dz.X + S(10), t1 + S(28), dz.Width - S(20), S(20)),
                     F(S(10.8f)), C::text3, 1);
        }
        add_hit(dz.X, dz.Y, dz.Width, dz.Height, H_DROP);
    }

    // ---------------- transfers
    float ty = dz.GetBottom() + S(14);
    RectF c3(rightX, ty, rightW, y1 - ty);
    card(g, c3);
    text(g, L"传输任务", RectF(c3.X + S(18), c3.Y + S(10), S(120), S(28)), F(S(13.0f), FontStyleBold), C::text, 0);

    std::vector<Transfer> tx;
    { std::lock_guard<std::mutex> lk(g_mu); tx = g_tx; }
    std::reverse(tx.begin(), tx.end());

    float listTop2 = c3.Y + S(46);
    float listBottom2 = c3.GetBottom() - S(10);
    float viewH = listBottom2 - listTop2;
    float rowH = S(76) + S(8);
    float contentH = tx.size() * rowH;
    float maxScroll = std::max(0.f, contentH - viewH);
    if (g_scroll[0] < 0) g_scroll[0] = 0;
    if (g_scroll[0] > maxScroll) g_scroll[0] = maxScroll;

    g.SetClip(RectF(c3.X + S(1), listTop2, c3.Width - S(2), viewH));
    if (tx.empty()) {
        float ccy = listTop2 + viewH / 2 - S(34);
        icon(g, I_INBOX, RectF(c3.X + c3.Width / 2 - S(20), ccy, S(40), S(40)), Color(255, 46, 54, 70), 1.6f);
        text(g, L"暂无传输任务", RectF(c3.X, ccy + S(50), c3.Width, S(22)), F(S(12.0f)), C::text2, 1, false);
        text(g, L"从上方拖入文件即可开始传输", RectF(c3.X, ccy + S(72), c3.Width, S(20)), F(S(10.5f)), C::text3, 1, false);
    } else {
        float ry = listTop2 - g_scroll[0];
        for (size_t i = 0; i < tx.size(); i++) {
            const Transfer& t = tx[i];
            if (ry + rowH < listTop2 - S(4) || ry > listBottom2) { ry += rowH; continue; }
            RectF rr(c3.X + S(12), ry, c3.Width - S(24), S(76));

            Color rbg = C::card2;
            bool waiting = t.state == TS_Waiting;
            if (waiting) rbg = Color(255, 26, 30, 44);
            fill_round(g, rr, S(12), rbg);
            stroke_round(g, RectF(rr.X + 0.5f, rr.Y + 0.5f, rr.Width - 1, rr.Height - 1), S(12), C::lineSoft, 1.0f);

            // icon tile
            RectF tile(rr.X + S(14), rr.Y + S(14), S(34), S(34));
            Color tc = t.state == TS_Done ? C::green
                     : t.state == TS_Failed ? C::red
                     : t.state == TS_Canceled ? C::text3
                     : t.state == TS_Rejected ? C::orange
                     : t.dir == TD_In ? Color(255, 110, 190, 255) : C::accent;
            fill_round(g, tile, S(10), with_alpha(tc, 30));
            icon(g, t.dir == TD_In ? I_DOWN : I_UP, RectF(tile.X + S(9), tile.Y + S(9), S(16), S(16)), tc, 1.8f);

            float tx0 = tile.GetRight() + S(12);
            float btnW = S(28);
            float nameW = rr.Width - (tx0 - rr.X) - S(14) - btnW - S(8) - S(56);

            text(g, t.curName(), RectF(tx0, rr.Y + S(12), nameW, S(20)), F(S(12.8f)), C::text, 0);

            // percent + status
            wchar_t pct[16] = { 0 };
            swprintf(pct, 16, L"%d%%", t.progress_permille() / 10);
            std::wstring pctS = pct;
            Color pc = t.state == TS_Done ? C::green : t.state == TS_Failed ? C::red : C::text2;
            text(g, pctS, RectF(rr.GetRight() - S(14) - btnW - S(8) - S(52), rr.Y + S(12), S(52), S(20)),
                 FM(S(11.5f)), pc, 2, false);

            std::wstring st;
            switch (t.state) {
            case TS_Waiting: st = L"等待你确认接收…"; break;
            case TS_Active: {
                std::wstring dirS = t.dir == TD_In ? L"接收中" : L"发送中";
                st = dirS + L" · " + fmt_size(t.total) + L" · " + fmt_speed(t.speed);
                if (t.files.size() > 1) st += L" · " + std::to_wstring(t.files.size()) + L" 个文件";
                break;
            }
            case TS_Done: st = L"已完成 · " + fmt_size(t.total); break;
            case TS_Failed: st = t.note.empty() ? L"传输失败" : t.note; break;
            case TS_Canceled: st = L"已取消"; break;
            case TS_Rejected: st = L"对方拒绝接收"; break;
            }
            text(g, st, RectF(tx0, rr.Y + S(32), rr.Width - (tx0 - rr.X) - S(14) - S(2), S(18)), F(S(10.6f)),
                 t.state == TS_Failed ? C::red : C::text3, 0);

            // progress bar
            RectF pb(rr.X + S(14), rr.Y + S(58), rr.Width - S(28), S(5));
            fill_round(g, pb, S(2.5f), Color(255, 30, 36, 47));
            int pm = t.progress_permille();
            if (pm > 0) {
                float pw = std::max(S(5), pb.Width * pm / 1000.0f);
                RectF f(pb.X, pb.Y, pw, pb.Height);
                if (t.state == TS_Done) {
                    fill_round(g, f, S(2.5f), C::green);
                } else if (t.state == TS_Failed || t.state == TS_Canceled || t.state == TS_Rejected) {
                    fill_round(g, f, S(2.5f), with_alpha(C::red, 190));
                } else {
                    GraphicsPath* p = rp(f, S(2.5f));
                    LinearGradientBrush lg(PointF(f.X, f.Y), PointF(f.GetRight(), f.Y), C::accent, C::accent2);
                    g.FillPath(&lg, p);
                    delete p;
                }
            }

            // action button
            RectF ab(rr.GetRight() - S(14) - btnW, rr.Y + S(14), btnW, btnW);
            if (waiting) {
                float sa = anim_val(akey(H_TX_ACTION, t.id * 2 + 1), g_hoverId == H_TX_ACTION && g_hoverArg == t.id * 2 + 1 ? 1.f : 0.f);
                RectF a1(ab.X - S(66), ab.Y, S(58), S(28));
                fill_round(g, a1, S(8), mix(Color(255, 40, 60, 48), Color(255, 63, 214, 140), 0.25f + sa * 0.2f));
                text(g, L"接收", a1, F(S(11.0f)), Color(255, 90, 220, 160), 1, false);
                add_hit(a1.X, a1.Y, a1.Width, a1.Height, H_TX_ACTION, t.id * 2 + 1);

                RectF a2(ab.X, ab.Y, btnW, S(28));
                float sa2 = anim_val(akey(H_TX_ACTION, t.id * 2 + 2), g_hoverId == H_TX_ACTION && g_hoverArg == t.id * 2 + 2 ? 1.f : 0.f);
                fill_round(g, a2, S(8), mix(Color(255, 40, 44, 52), Color(255, 70, 46, 46), sa2));
                icon(g, I_X, RectF(a2.X + S(8), a2.Y + S(8), S(12), S(12)), Color(255, 200, 120, 120), 1.7f);
                add_hit(a2.X, a2.Y, a2.Width, a2.Height, H_TX_ACTION, t.id * 2 + 2);
            } else {
                float a = anim_val(akey(H_TX_ACTION, t.id * 2), g_hoverId == H_TX_ACTION && g_hoverArg == t.id * 2 ? 1.f : 0.f);
                bool active = t.state == TS_Active;
                Icon ic = active ? I_X : (t.state == TS_Done ? I_FOLDER : I_X);
                Color bc = active ? mix(C::text3, C::text, a) : mix(C::text3, C::text, a);
                fill_round(g, ab, S(8), Color((BYTE)(18 + 30 * a), 255, 255, 255));
                icon(g, ic, RectF(ab.X + S(8), ab.Y + S(8), S(12), S(12)), bc, 1.7f);
                add_hit(ab.X, ab.Y, ab.Width, ab.Height, H_TX_ACTION, t.id * 2);
            }
            ry += rowH;
        }
    }
    g.ResetClip();

    // scrollbar
    if (maxScroll > 0) {
        float trackH = viewH;
        float thumbH = std::max(S(30), trackH * viewH / contentH);
        float thumbY = listTop2 + (trackH - thumbH) * (g_scroll[0] / maxScroll);
        fill_round(g, RectF(c3.GetRight() - S(7), thumbY, S(3), thumbH), S(1.5f), Color(70, 255, 255, 255));
    }
}

// ------------------------------------------------------------------ page: files
// Path helpers work on the wire form ("C:/Users/Public"), which uses '/' on
// every platform so the phone and the PC speak the same thing.
static std::string trim_slashes(const std::string& p) {
    std::string s = p;
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// Android reports storage volumes as /storage/emulated/0 or /storage/XXXX-XXXX.
// Those are "roots" from the user's point of view: one level inside the volume
// chooser, so stepping up has to land back on the volume list.
static bool is_android_volume_root(const std::string& p) {
    std::string s = trim_slashes(p);
    if (s == "/storage/emulated") return true;
    size_t a = s.find_last_of('/');
    if (a == std::string::npos) return false;
    std::string parent = s.substr(0, a);
    return parent == "/storage" || parent == "/storage/emulated";
}

static std::string path_parent(const std::string& p) {
    if (p == "/" || p.empty()) return "/";
    if (is_android_volume_root(p)) return "/";
    std::string s = trim_slashes(p);
    size_t pos = s.find_last_of('/');
    if (pos == std::string::npos) return "/";
    std::string parent = s.substr(0, pos);
    if (parent.empty()) return "/";
    // "C:" rather than "" for drive roots
    return parent;
}

// What the breadcrumb shows. A phone volume root reads as "/" so the operator
// is not confronted with /storage/emulated/0.
static std::wstring path_display(const std::string& p) {
    if (is_android_volume_root(p)) return L"/";
    return utf8_to_w(p);
}

static std::string path_join(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == "/") return "/" + name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

static std::wstring fmt_stamp(uint64_t t) {
    if (!t) return L"";
    SYSTEMTIME st{};
    if (t > 100000000000000ULL) {          // Windows FILETIME
        FILETIME ft; ft.dwLowDateTime = (DWORD)(t & 0xFFFFFFFF); ft.dwHighDateTime = (DWORD)(t >> 32);
        if (!FileTimeToSystemTime(&ft, &st)) return L"";
    } else {                                // unix seconds
        LONGLONG ll = (LONGLONG)t * 10000000LL + 116444736000000000LL;
        FILETIME ft; ft.dwLowDateTime = (DWORD)(ll & 0xFFFFFFFF); ft.dwHighDateTime = (DWORD)(ll >> 32);
        if (!FileTimeToSystemTime(&ft, &st)) return L"";
    }
    wchar_t b[32];
    swprintf(b, 32, L"%04u-%02u-%02u", st.wYear, st.wMonth, st.wDay);
    return b;
}

static void br_load(const std::string& path);

// Kick off a directory listing on a worker thread.
static void br_open_peer(const Peer& p) {
    {
        std::lock_guard<std::mutex> lk(g_brMu);
        if (g_brPeerId == p.id && g_brState != 0) return;   // already on it
        g_brPeerId = p.id;
        g_brIp = p.ip;
        g_brPort = p.port;
        g_brPeerName = utf8_to_w(p.name);
        g_brState = 1;
        g_scroll[3] = 0;
        g_brSel = -1;
    }
    br_load("/");
}

static void br_load(const std::string& path) {
    std::string ip; int port;
    {
        std::lock_guard<std::mutex> lk(g_brMu);
        g_brPath = path;
        g_brState = 1;
        g_brErr.clear();
        ip = g_brIp; port = g_brPort;
    }
    g_brSel = -1;
    post_refresh();
    std::thread([ip, port, path] {
        std::vector<RemoteEntry> got;
        std::wstring err;
        bool ok = net_fs_list(ip, port, path, got, err);
        {
            std::lock_guard<std::mutex> lk(g_brMu);
            if (ok) {
                g_brEntries = std::move(got);
                g_brState = 3;
            } else {
                g_brErr = err;
                g_brState = 2;
            }
        }
        post_refresh();
    }).detach();
}

// Copy one remote file into the receive folder.
static void br_download(int idx) {
    RemoteEntry e;
    std::string ip; int port; std::wstring dir;
    {
        std::lock_guard<std::mutex> lk(g_brMu);
        if (idx < 0 || idx >= (int)g_brEntries.size()) return;
        if (g_brDlActive) return;
        e = g_brEntries[idx];
        if (e.isDir) return;
        ip = g_brIp; port = g_brPort;
        dir = g_set.saveDir;
        g_brDlActive = true;
        g_brDlName = e.name;
        g_brDlDone = 0; g_brDlTotal = e.size;
    }
    post_refresh();
    std::thread([ip, port, e, dir] {
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring local = dir + L"\\" + e.name;
        if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES) {
            // never overwrite: pick "name (n).ext"
            size_t dot = e.name.find_last_of(L'.');
            std::wstring base = dot == std::wstring::npos ? e.name : e.name.substr(0, dot);
            std::wstring ext = dot == std::wstring::npos ? L"" : e.name.substr(dot);
            for (int i = 1; i < 9999; i++) {
                std::wstring cand = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
                if (GetFileAttributesW(cand.c_str()) == INVALID_FILE_ATTRIBUTES) { local = cand; break; }
            }
        }
        std::wstring err;
        bool ok = net_fs_fetch(ip, port, e.path, local, err, [](uint64_t done, uint64_t total) {
            std::lock_guard<std::mutex> lk(g_brMu);
            g_brDlDone = done;
            g_brDlTotal = total;
        });
        {
            std::lock_guard<std::mutex> lk(g_brMu);
            g_brDlActive = false;
        }
        // A copy from the device browser is a normal transfer, so it belongs in
        // the history exactly like a received push does.
        if (ok) {
            Transfer rec;
            rec.dir = TD_In;
            rec.state = TS_Done;
            FileEntry it;
            {
                size_t cut = local.find_last_of(L"\\/");
                it.name = cut == std::wstring::npos ? local : local.substr(cut + 1);
            }
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(local.c_str(), GetFileExInfoStandard, &fad)) {
                it.size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            }
            rec.files.push_back(it);
            rec.total = it.size;
            rec.peerIp = ip;
            rec.peerPort = port;
            history_append(rec);
            g_hist = history_load_lines();
        }
        toast(ok ? (L"已复制 " + e.name) : (L"复制失败：" + err));
        post_refresh();
    }).detach();
}

static void draw_page_files(Graphics& g, float x0, float x1, float y0, float y1) {
    float cw = x1 - x0;
    RectF c(x0, y0 + S(46), cw, y1 - y0 - S(46));
    card(g, c);

    std::string path; std::wstring peerName, err;
    std::vector<RemoteEntry> ents;
    int state; int sel; bool dl; std::wstring dlName; uint64_t dDone = 0, dTotal = 0;
    {
        std::lock_guard<std::mutex> lk(g_brMu);
        path = g_brPath; peerName = g_brPeerName; err = g_brErr;
        ents = g_brEntries; state = g_brState; sel = g_brSel;
        dl = g_brDlActive; dlName = g_brDlName;
        dDone = g_brDlDone; dTotal = g_brDlTotal;
    }

    // ---- toolbar
    float tbH = S(48);
    RectF tb(c.X + S(14), c.Y + S(12), c.Width - S(28), S(34));
    fill_round(g, tb, S(9), Color(255, 15, 18, 24));
    stroke_round(g, RectF(tb.X + 0.5f, tb.Y + 0.5f, tb.Width - 1, tb.Height - 1), S(9), C::line, 1.0f);

    {
        RectF up(tb.X + S(4), tb.Y + S(4), S(26), S(26));
        bool canUp = (path != "/" && !path.empty());
        float a = anim_val(akey(H_FS_UP, -1), (g_hoverId == H_FS_UP && canUp) ? 1.f : 0.f);
        if (a > 0.01f) fill_round(g, up, S(7), Color((BYTE)(26 * a), 255, 255, 255));
        icon(g, I_UP, RectF(up.X + S(5), up.Y + S(5), S(16), S(16)),
             canUp ? mix(C::text3, C::text, a) : Color(255, 60, 68, 82), 1.7f);
        add_hit(up.X, up.Y, up.Width, up.Height, H_FS_UP);
    }
    {
        RectF rf(tb.GetRight() - S(30), tb.Y + S(4), S(26), S(26));
        float a = anim_val(akey(H_FS_REFRESH, -1), g_hoverId == H_FS_REFRESH ? 1.f : 0.f);
        if (a > 0.01f) fill_round(g, rf, S(7), Color((BYTE)(26 * a), 255, 255, 255));
        icon(g, I_REFRESH, RectF(rf.X + S(5), rf.Y + S(5), S(16), S(16)), mix(C::text3, C::text, a), 1.6f);
        add_hit(rf.X, rf.Y, rf.Width, rf.Height, H_FS_REFRESH);
    }
    text(g, path_display(path), RectF(tb.X + S(38), tb.Y, tb.Width - S(76), tb.Height),
         F(S(11.6f)), C::text2, 0);
    if (!peerName.empty()) {
        // device chip on the right of the title row
        std::wstring nm = peerName;
        float tw = text_w(g, nm, F(S(10.5f)));
        RectF chip(x1 - S(28) - tw - S(20), y0 + S(8), tw + S(20), S(24));
        fill_round(g, chip, S(12), Color(255, 30, 42, 68));
        text(g, nm, chip, F(S(10.5f)), Color(255, 170, 195, 255), 1, false);
    }

    // ---- list
    float listTop = tb.GetBottom() + S(10);
    float footH = S(52);
    float listBottom = c.GetBottom() - footH - S(10);
    RectF view(c.X + S(14), listTop, c.Width - S(28), listBottom - listTop);
    g.SetClip(view);

    if (state == 1) {
        text(g, L"正在读取目录…", RectF(view.X, view.Y + view.Height / 2 - S(12), view.Width, S(24)),
             F(S(12.0f)), C::text3, 1, false);
    } else if (state == 2) {
        text(g, L"无法打开该目录", RectF(view.X, view.Y + view.Height / 2 - S(26), view.Width, S(24)),
             F(S(12.4f)), C::text2, 1, false);
        text(g, err, RectF(view.X + S(20), view.Y + view.Height / 2 - S(2), view.Width - S(40), S(22)),
             F(S(10.6f)), C::text3, 1);
    } else if (ents.empty()) {
        text(g, L"这个文件夹是空的", RectF(view.X, view.Y + view.Height / 2 - S(12), view.Width, S(24)),
             F(S(12.0f)), C::text3, 1, false);
    } else {
        float rowH = S(46);
        float maxScroll = std::max(0.f, rowH * ents.size() - view.Height);
        if (g_scroll[3] < 0) g_scroll[3] = 0;
        if (g_scroll[3] > maxScroll) g_scroll[3] = maxScroll;
        float ry = view.Y - g_scroll[3];
        for (size_t i = 0; i < ents.size(); i++) {
            if (ry + rowH < view.Y) { ry += rowH; continue; }
            if (ry > view.Y + view.Height) break;
            RectF rr(view.X, ry, view.Width, rowH - S(4));
            bool isSel = ((int)i == sel);
            float ha = anim_val(akey(H_FS_ENTRY, (int)i),
                                (g_hoverId == H_FS_ENTRY && g_hoverArg == (int)i) ? 1.f : 0.f);
            if (isSel) fill_round(g, rr, S(8), Color(255, 30, 40, 64));
            else if (ha > 0.01f) fill_round(g, rr, S(8), Color((BYTE)(255 * ha * 0.06f), 255, 255, 255));

            Color ic = ents[i].isDir ? Color(255, 240, 190, 90) : C::text2;
            icon(g, ents[i].isDir ? I_FOLDER : I_FILE,
                 RectF(rr.X + S(10), rr.Y + (rr.Height - S(18)) / 2, S(18), S(18)), ic, 1.6f);

            float nameX = rr.X + S(38);
            float btnW = ents[i].isDir ? S(20) : S(64);
            text(g, ents[i].name, RectF(nameX, rr.Y, rr.Width - S(38) - btnW - S(16), rr.Height),
                 F(S(12.0f)), ents[i].isDir ? C::text : C::text2, 0);

            std::wstring meta;
            if (!ents[i].isDir) meta = fmt_size(ents[i].size);
            std::wstring stamp = fmt_stamp(ents[i].mtime);
            if (!stamp.empty()) meta += (meta.empty() ? L"" : L"  ·  ") + stamp;
            if (!meta.empty())
                text(g, meta, RectF(rr.X + rr.Width - S(16) - S(180) - (ents[i].isDir ? 0 : S(64)),
                                    rr.Y, S(180), rr.Height), F(S(9.8f)), C::text3, 2);

            if (ents[i].isDir) {
                icon(g, I_CHEV, RectF(rr.GetRight() - S(22), rr.Y + (rr.Height - S(14)) / 2, S(14), S(14)),
                     Color(255, 78, 88, 104), 1.5f);
            } else {
                RectF db(rr.GetRight() - S(64), rr.Y + (rr.Height - S(26)) / 2, S(58), S(26));
                bool hot = (g_hoverId == H_FS_DOWNLOAD && g_hoverArg == (int)i);
                fill_round(g, db, S(7), hot ? Color(255, 60, 105, 200) : Color(255, 40, 60, 100));
                icon(g, I_DOWN, RectF(db.X + S(9), db.Y + S(7), S(12), S(12)), Color(255, 150, 185, 255), 1.6f);
                text(g, L"复制", RectF(db.X + S(24), db.Y, db.Width - S(28), db.Height),
                     F(S(10.2f)), Color(255, 168, 198, 255), 0, false);
                add_hit(db.X, db.Y, db.Width, db.Height, H_FS_DOWNLOAD, (int)i);
            }
            add_hit(rr.X, rr.Y, rr.Width - (ents[i].isDir ? S(0) : S(70)), rr.Height, H_FS_ENTRY, (int)i);
            ry += rowH;
        }
        if (maxScroll > 0) {
            float trackH = view.Height;
            float barH = std::max(S(28), trackH * view.Height / (rowH * ents.size()));
            float barY = view.Y + (trackH - barH) * (g_scroll[3] / maxScroll);
            fill_round(g, RectF(view.GetRight() - S(4), barY, S(3), barH), S(1.5f), Color(70, 255, 255, 255));
        }
    }
    g.ResetClip();

    // ---- footer
    {
        hline(g, c.X + S(14), c.GetBottom() - footH, c.Width - S(28), C::lineSoft);
        float fy = c.GetBottom() - footH + S(9);
        if (dl) {
            float pct = dTotal ? (float)((double)dDone / (double)dTotal) : 0.f;
            text(g, L"正在复制 " + dlName, RectF(c.X + S(18), fy, c.Width - S(40), S(18)),
                 F(S(10.6f)), C::text2, 0);
            RectF bar(c.X + S(18), fy + S(22), c.Width - S(36), S(5));
            fill_round(g, bar, S(2.5f), Color(255, 26, 32, 42));
            fill_round(g, RectF(bar.X, bar.Y, std::max(S(4), bar.Width * pct), bar.Height), S(2.5f), C::accent);
            g_animating = true;
        } else {
            std::wstring hint = L"点文件夹进入 · 点「复制」把文件存到 ";
            hint += g_set.saveDir;
            text(g, hint, RectF(c.X + S(18), fy, c.Width - S(36), S(20)), F(S(10.4f)), C::text3, 0);
        }
    }
}


static void draw_page_history(Graphics& g, float x0, float x1, float y0, float y1) {
    float hx = x1 - S(250);
    {
        RectF b(hx, y0 + S(4), S(110), S(32));
        ghost_button(g, b, L"打开文件夹", I_FOLDER, H_HIST_OPEN, anim_val(akey(H_HIST_OPEN, -1), g_hoverId == H_HIST_OPEN));
    }
    {
        RectF b(hx + S(120), y0 + S(4), S(92), S(32));
        ghost_button(g, b, L"清空记录", I_TRASH, H_HIST_CLEAR, anim_val(akey(H_HIST_CLEAR, -1), g_hoverId == H_HIST_CLEAR));
    }

    RectF c(x0, y0 + S(46), x1 - x0, y1 - y0 - S(46));
    card(g, c);

    float listTop = c.Y + S(10);
    float viewH = c.Height - S(20);
    float rowH = S(64);
    float contentH = g_hist.size() * rowH;
    float maxScroll = std::max(0.f, contentH - viewH);
    if (g_scroll[1] < 0) g_scroll[1] = 0;
    if (g_scroll[1] > maxScroll) g_scroll[1] = maxScroll;

    g.SetClip(RectF(c.X + S(1), listTop, c.Width - S(2), viewH));
    if (g_hist.empty()) {
        float ccy = listTop + viewH / 2 - S(30);
        icon(g, I_CLOCK, RectF(c.X + c.Width / 2 - S(19), ccy, S(38), S(38)), Color(255, 46, 54, 70), 1.6f);
        text(g, L"还没有传输记录", RectF(c.X, ccy + S(48), c.Width, S(22)), F(S(12.0f)), C::text2, 1, false);
        text(g, L"完成一次传输后，记录会显示在这里", RectF(c.X, ccy + S(70), c.Width, S(20)), F(S(10.5f)), C::text3, 1, false);
    } else {
        float ry = listTop - g_scroll[1];
        for (size_t i = 0; i < g_hist.size(); i++) {
            std::wstring line = g_hist[g_hist.size() - 1 - i];
            if (ry + rowH < listTop || ry > listTop + viewH) { ry += rowH; continue; }
            // one parser for both the current id-prefixed layout and old files
            HistRec rec;
            if (!hist_parse_line(line, rec)) { ry += rowH; continue; }
            bool inbound = rec.dir == "in";
            std::wstring name = rec.name.empty() ? L"文件" : utf8_to_w(rec.name);
            std::wstring size = utf8_to_w(rec.size);
            std::wstring st = utf8_to_w(rec.state);
            std::wstring tm = utf8_to_w(rec.time);
            uint64_t pgDone = 0, pgTotal = 0;
            const bool live = net_transfer_progress(rec.id, pgDone, pgTotal);

            RectF tile(c.X + S(18), ry + S(14), S(36), S(36));
            Color tc = inbound ? Color(255, 110, 190, 255) : C::accent;
            fill_round(g, tile, S(10), with_alpha(tc, 28));
            icon(g, inbound ? I_DOWN : I_UP, RectF(tile.X + S(10), tile.Y + S(10), S(16), S(16)), tc, 1.8f);

            text(g, name, RectF(tile.GetRight() + S(14), ry + S(12), c.Width - S(280), S(20)), F(S(12.5f)), C::text, 0);
            text(g, size + L" · " + (inbound ? L"接收" : L"发送") + L" · " + tm,
                 RectF(tile.GetRight() + S(14), ry + S(32), c.Width - S(280), S(18)), F(S(10.5f)), C::text3, 0);

            // running transfers show a progress bar instead of a bare chip
            if (live && pgTotal > 0) {
                float pct = (float)((double)pgDone / (double)pgTotal);
                if (pct > 1.f) pct = 1.f;
                RectF bar(c.GetRight() - S(230), ry + S(34), S(140), S(6));
                fill_round(g, bar, S(3), Color(255, 34, 40, 54));
                if (pct > 0.001f)
                    fill_round(g, RectF(bar.X, bar.Y, bar.Width * pct, bar.Height), S(3), C::accent);
                wchar_t pb[32];
                _snwprintf_s(pb, 32, _TRUNCATE, L"%.0f%%", pct * 100.0f);
                text(g, pb, RectF(bar.GetRight() + S(8), ry + S(26), S(50), S(20)),
                     F(S(10.4f)), C::accent, 0);
            }
            Color sc = st == L"完成" ? C::green : (st == L"已取消" ? C::text3 : (live ? C::accent : C::red));
            RectF chip(c.GetRight() - S(96), ry + S(18), S(60), S(24));
            fill_round(g, chip, S(12), with_alpha(sc, 26));
            text(g, st, chip, F(S(10.5f)), sc, 1, false);

            // finished rows can be removed, taking the counterpart file with them
            if (st == L"完成") {
                size_t srcIdx = g_hist.size() - 1 - i;
                RectF db(c.GetRight() - S(96) - S(8) - S(52), ry + S(20), S(52), S(24));
                float da = anim_val(akey(H_HIST_DEL, (int)srcIdx),
                                    (g_hoverId == H_HIST_DEL && g_hoverArg == (int)srcIdx) ? 1.f : 0.f);
                fill_round(g, db, S(7), mix(Color(255, 40, 32, 34), Color(255, 74, 36, 38), da));
                text(g, L"删除", db, F(S(10.2f)),
                     mix(Color(255, 190, 106, 102), Color(255, 240, 138, 130), da), 1, false);
                add_hit(db.X, db.Y, db.Width, db.Height, H_HIST_DEL, (int)srcIdx);
            }

            if (i + 1 < g_hist.size()) hline(g, c.X + S(18), ry + rowH - S(1), c.Width - S(36), C::lineSoft);
            ry += rowH;
        }
    }
    g.ResetClip();
}

// ------------------------------------------------------------------ page: settings
static void draw_page_settings(Graphics& g, float x0, float x1, float y0, float y1) {
    // The card follows the window: it fills the available width instead of
    // being pinned to a fixed 760 px.
    float cw = x1 - x0;
    const float rowH = 62, padTop = 18, infoBlock = 12 + 26 + 3 * 28, footer = 6 + 1 + 16 + 20 + 18;
    float needH = S(padTop + 7 * rowH + infoBlock + footer) + S(112);
    float availH = y1 - y0 - S(46);
    RectF c(x0, y0 + S(46), cw, std::min(needH, availH));
    card(g, c);

    // The card scrolls when the window is too short to show every row.
    float innerH = availH - S(6);
    float maxScroll = std::max(0.f, needH - c.Height);
    if (g_scroll[2] < 0) g_scroll[2] = 0;
    if (g_scroll[2] > maxScroll) g_scroll[2] = maxScroll;

    RectF view(c.X + S(2), c.Y + S(2), c.Width - S(4), c.Height - S(4));
    g.SetClip(view);
    size_t hitMark = g_hits.size();

    float px = c.X + S(22);
    float right = c.GetRight() - S(22);
    float y = c.Y + S(18) - g_scroll[2];

    auto row_label = [&](const std::wstring& t, const std::wstring& s) {
        text(g, t, RectF(px, y + S(12), S(260), S(22)), F(S(12.8f)), C::text, 0);
        if (!s.empty()) text(g, s, RectF(px, y + S(32), S(300), S(18)), F(S(10.4f)), C::text3, 0);
    };

    // device name
    row_label(L"设备名称", L"在其它设备的列表中显示这个名字");
    {
        RectF fb(right - S(260), y + S(15), S(260), S(32));
        bool foc = (g_edit == 1);
        fill_round(g, fb, S(8), Color(255, 15, 18, 24));
        stroke_round(g, RectF(fb.X + 0.5f, fb.Y + 0.5f, fb.Width - 1, fb.Height - 1), S(8), foc ? C::accent : C::line, 1.0f);
        std::wstring v = foc ? g_editText : g_set.deviceName;
        text(g, v, RectF(fb.X + S(11), fb.Y, fb.Width - S(22), fb.Height), F(S(12.2f)), C::text, 0);
        if (foc) {
            float tw = text_w(g, v.substr(0, g_caret), F(S(12.2f)));
            if ((GetTickCount64() / 530) % 2 == 0) {
                float cx = std::min(fb.X + S(11) + tw, fb.GetRight() - S(8));
                Pen pen(C::accent, 1.4f);
                g.DrawLine(&pen, cx, fb.Y + S(8), cx, fb.GetBottom() - S(8));
            }
            g_animating = true;
        }
        add_hit(fb.X, fb.Y, fb.Width, fb.Height, H_SET_NAME);
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // port
    row_label(L"监听端口", L"改端口后会自动重启接收服务");
    {
        RectF fb(right - S(120), y + S(15), S(120), S(32));
        bool foc = (g_edit == 2);
        fill_round(g, fb, S(8), Color(255, 15, 18, 24));
        stroke_round(g, RectF(fb.X + 0.5f, fb.Y + 0.5f, fb.Width - 1, fb.Height - 1), S(8), foc ? C::accent : C::line, 1.0f);
        std::wstring v = foc ? g_editText : std::to_wstring(g_set.port);
        text(g, v, RectF(fb.X + S(11), fb.Y, fb.Width - S(22), fb.Height), FM(S(12.2f)), C::text, 0);
        if (foc) {
            float tw = text_w(g, v.substr(0, g_caret), FM(S(12.2f)));
            if ((GetTickCount64() / 530) % 2 == 0) {
                float cx = std::min(fb.X + S(11) + tw, fb.GetRight() - S(8));
                Pen pen(C::accent, 1.4f);
                g.DrawLine(&pen, cx, fb.Y + S(8), cx, fb.GetBottom() - S(8));
            }
            g_animating = true;
        }
        add_hit(fb.X, fb.Y, fb.Width, fb.Height, H_SET_PORT);
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // save dir
    row_label(L"接收文件夹", L"收到的文件保存到此处");
    {
        RectF b1(right - S(96), y + S(15), S(96), S(32));
        ghost_button(g, b1, L"更改", I_FOLDER, H_SET_BROWSE, anim_val(akey(H_SET_BROWSE, -1), g_hoverId == H_SET_BROWSE));
        RectF b2(right - S(96) - S(8) - S(78), y + S(15), S(78), S(32));
        ghost_button(g, b2, L"打开", I_FOLDER, H_SET_OPEN_DIR, anim_val(akey(H_SET_OPEN_DIR, -1), g_hoverId == H_SET_OPEN_DIR));
        text(g, g_set.saveDir, RectF(right - S(96) - S(8) - S(78) - S(8) - S(300), y + S(21), S(296), S(22)),
             F(S(11.2f)), C::text3, 2);
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // auto accept
    row_label(L"自动接收文件", L"关闭后，每笔传入文件都需要手动确认");
    {
        RectF sr(right - S(42), y + S(20), S(42), S(23));
        switch_widget(g, sr.X, sr.Y, g_set.autoAccept, H_SET_AUTO, anim_val(akey(H_SET_AUTO, -1), g_hoverId == H_SET_AUTO));
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // allow browse
    row_label(L"允许对方浏览本机文件", L"开关立即生效，无需重启");
    {
        RectF sr(right - S(42), y + S(20), S(42), S(23));
        switch_widget(g, sr.X, sr.Y, g_set.allowBrowse, H_SET_ALLOW_BROWSE,
                      anim_val(akey(H_SET_ALLOW_BROWSE, -1), g_hoverId == H_SET_ALLOW_BROWSE));
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // keep the machine awake while AirLink is running
    row_label(L"保持唤醒电脑", L"开启后，屏幕不会因为空闲而自动睡眠或关闭");
    {
        RectF sr(right - S(42), y + S(20), S(42), S(23));
        switch_widget(g, sr.X, sr.Y, g_set.keepAwake, H_SET_KEEP_AWAKE,
                      anim_val(akey(H_SET_KEEP_AWAKE, -1), g_hoverId == H_SET_KEEP_AWAKE));
    }
    y += S(rowH);
    hline(g, px, y - S(1), c.Width - S(44), C::lineSoft);

    // info block
    y += S(12);
    text(g, L"连接方式", RectF(px, y, S(200), S(22)), F(S(12.8f)), C::text, 0);
    y += S(26);

    struct Tip { Icon ic; std::wstring t; };
    std::vector<Tip> tips = {
        { I_WIFI, L"Wi-Fi：两台设备接入同一路由即可互相发现，无需联网。" },
        { I_USB,  L"USB：手机开启「USB 网络共享」后，电脑会出现一块 USB 网卡，传输同样走这条线。" },
        { I_SHIELD, L"文件只在本机与对方之间直传，不经过任何服务器。" },
    };
    for (auto& t : tips) {
        icon(g, t.ic, RectF(px, y + S(1), S(15), S(15)), C::text3, 1.5f);
        text(g, t.t, RectF(px + S(24), y, c.Width - S(60), S(20)), F(S(11.2f)), C::text2, 0);
        y += S(28);
    }

    y += S(6);
    hline(g, px, y, c.Width - S(44), C::lineSoft);
    y += S(12);

    // about: a row that opens its own page, rather than a wall of text here
    {
        RectF ab(px, y, c.Width - S(44), S(46));
        float aa = anim_val(akey(H_SET_ABOUTOpen, -1), g_hoverId == H_SET_ABOUTOpen ? 1.f : 0.f);
        fill_round(g, ab, S(10), mix(Color(255, 26, 31, 41), Color(255, 34, 42, 58), aa));
        stroke_round(g, RectF(ab.X + 0.5f, ab.Y + 0.5f, ab.Width - 1, ab.Height - 1), S(10), C::lineSoft, 1.0f);
        icon(g, I_SHIELD, RectF(ab.X + S(14), ab.Y + S(15), S(16), S(16)),
             mix(C::text3, C::accent, aa), 1.6f);
        text(g, L"关于 AirLink", RectF(ab.X + S(42), ab.Y, ab.Width - S(90), ab.Height),
             F(S(12.6f)), C::text, 0);
        text(g, L"版本、协议与使用说明", RectF(ab.X + S(150), ab.Y, ab.Width - S(200), ab.Height),
             F(S(10.4f)), C::text3, 0);
        icon(g, I_CHEV, RectF(ab.GetRight() - S(30), ab.Y + S(16), S(14), S(14)),
             mix(C::text3, C::text, aa), 1.6f);
        add_hit(ab.X, ab.Y, ab.Width, ab.Height, H_SET_ABOUTOpen);
    }
    y += S(46) + S(10);

    // device management: history of everything we ever connected to
    {
        RectF ab(px, y, c.Width - S(44), S(46));
        float aa = anim_val(akey(H_SET_DEVICES, -1), g_hoverId == H_SET_DEVICES ? 1.f : 0.f);
        fill_round(g, ab, S(10), mix(Color(255, 26, 31, 41), Color(255, 34, 42, 58), aa));
        stroke_round(g, RectF(ab.X + 0.5f, ab.Y + 0.5f, ab.Width - 1, ab.Height - 1), S(10), C::lineSoft, 1.0f);
        icon(g, I_USB, RectF(ab.X + S(14), ab.Y + S(15), S(16), S(16)),
             mix(C::text3, C::accent, aa), 1.6f);
        text(g, L"设备管理", RectF(ab.X + S(42), ab.Y, ab.Width - S(90), ab.Height),
             F(S(12.6f)), C::text, 0);
        text(g, L"查看连接过的设备与每次连接的时间", RectF(ab.X + S(120), ab.Y, ab.Width - S(170), ab.Height),
             F(S(10.4f)), C::text3, 0);
        icon(g, I_CHEV, RectF(ab.GetRight() - S(30), ab.Y + S(16), S(14), S(14)),
             mix(C::text3, C::text, aa), 1.6f);
        add_hit(ab.X, ab.Y, ab.Width, ab.Height, H_SET_DEVICES);
    }
    y += S(46);

    g.ResetClip();
    // drop hit targets that scrolled out of view
    if (maxScroll > 0) {
        std::vector<Hit> kept;
        for (size_t i = 0; i < g_hits.size(); i++) {
            Hit& h = g_hits[i];
            if (i < hitMark) { kept.push_back(h); continue; }
            if (h.y + h.h <= view.Y || h.y >= view.GetBottom()) continue;
            kept.push_back(h);
        }
        g_hits.swap(kept);
    }
    if (maxScroll > 0) {
        float barH = std::max(S(28), view.Height * view.Height / needH);
        float barY = view.Y + (view.Height - barH) * (g_scroll[2] / maxScroll);
        fill_round(g, RectF(c.GetRight() - S(7), barY, S(3), barH), S(1.5f), Color(70, 255, 255, 255));
    }
}

// Dial a device the user typed in: prove the pairing code, then establish the
// session so the file browser can target it.
static void wan_connect_clicked() {
    std::wstring waddr = g_set.wanPeerAddr;
    std::wstring wcode = g_set.wanPeerCode;
    if (waddr.empty()) { toast(L"先填对方的 IPv6 地址"); return; }

    std::string spec = w_to_utf8(waddr);
    std::string host = spec;
    int port = g_set.port;
    if (spec.size() > 2 && spec[0] == '[') {
        size_t rb = spec.find(']');
        if (rb == std::string::npos) { toast(L"地址格式不对"); return; }
        host = spec.substr(1, rb - 1);
        if (rb + 1 < spec.size() && spec[rb + 1] == ':')
            port = atoi(spec.c_str() + rb + 2);
    } else if (spec.find(':') == spec.rfind(':')) {
        size_t c = spec.rfind(':');
        if (c != std::string::npos) {
            host = spec.substr(0, c);
            port = atoi(spec.c_str() + c + 1);
        }
    }
    if (host.empty() || port <= 0) { toast(L"地址格式不对"); return; }

    std::string code = w_to_utf8(wcode);
    std::string myId = net_self_id();
    std::wstring myName = g_set.deviceName;
    toast(L"正在连接 " + utf8_to_w(host) + L" …");

    std::thread([host, port, code, myId, myName] {
        if (!code.empty() && !net_auth_send(host, port, code)) {
            toast(L"配对码不对，或对方没有放行");
            return;
        }
        if (!net_session_connect(host, port, myId, w_to_utf8(myName), g_set.port)) {
            toast(L"对方没有响应");
            return;
        }
        // make it a real row so the device list and file browser can use it
        std::string pid = "wan-" + host;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            bool known = false;
            for (auto& p : g_peers) {
                if (p.ip == host) { p.lastSeen = GetTickCount64(); p.linkDown = false; pid = p.id; known = true; }
            }
            if (!known) {
                Peer np;
                np.id = pid;
                np.name = host;
                np.ip = host;
                np.port = port;
                np.lastSeen = GetTickCount64();
                np.linkDown = false;
                g_peers.push_back(np);
            }
        }
        net_session_set(pid, host, port);
        ui_set_selected_peer(pid);
        toast(L"已连接 " + utf8_to_w(host));
        post_refresh();
    }).detach();
}
// ------------------------------------------------------------------ page: WAN
static void draw_page_wan(Graphics& g, float x0, float x1, float y0, float y1) {
    RectF c(x0, y0 + S(46), x1 - x0, y1 - y0 - S(46));
    card(g, c);

    bool mapped = false, busy = false; int extPort = 0, inPort = 0;
    std::string extIp, publicIp, err;
    wan_status(mapped, busy, extPort, inPort, extIp, publicIp, err);

    float px = c.X + S(26);
    float y = c.Y + S(24);
    float right = c.GetRight() - S(26);

    text(g, L"广域网连接", RectF(px, y, S(300), S(26)), F(S(15.0f), FontStyleBold), C::text, 0, false);
    y += S(34);
    text(g, L"让不在同一个 Wi-Fi 的两台设备也能互传。需要在路由器上开一个端口。",
         RectF(px, y, c.Width - S(52), S(20)), F(S(11.2f)), C::text3, 0);
    y += S(30);

    // status pill
    {
        RectF pill(px, y, S(96), S(26));
        Color col = mapped ? Color(255, 92, 210, 150) : (busy ? Color(255, 220, 170, 90) : Color(255, 160, 120, 120));
        fill_round(g, pill, S(13), with_alpha(col, 40));
        text(g, mapped ? L"已映射" : (busy ? L"处理中" : L"未映射"), pill, F(S(11.0f)), col, 1, false);
        y += S(38);
    }

    auto kv = [&](const std::wstring& k, const std::wstring& v, const Color& vc) {
        text(g, k, RectF(px, y, S(110), S(22)), F(S(11.4f)), C::text3, 0);
        text(g, v, RectF(px + S(104), y, c.Width - S(180), S(22)), F(S(11.4f)), vc, 0);
        y += S(26);
    };

    // IPv6 needs no NAT: show the global address and skip port forwarding.
    std::string v6;
    {
        std::vector<LocalAddr> all = local_addrs_snapshot();
        for (auto& a : all) if (a.ip.find(':') != std::string::npos) { v6 = a.ip; break; }
    }
    if (!v6.empty()) {
        kv(L"公网 IPv6", utf8_to_w(v6), C::text);
        y += S(2);
        text(g, L"IPv6 直连，不需要在路由器上开端口。手机填上面的地址 + 配对码即可。",
             RectF(px, y, c.Width - S(52), S(34)), F(S(10.6f)), C::text3, 0);
        y += S(26);
        std::wstring full = L"[" + utf8_to_w(v6) + L"]:" + std::to_wstring(g_set.port);
        float qrSide = S(200);
        RectF qrBox(c.GetRight() - S(26) - qrSide, y, qrSide, qrSide);
        // white plate behind the symbol so it scans from any background
        fill_round(g, RectF(qrBox.X - S(6), qrBox.Y - S(6), qrBox.Width + S(12), qrBox.Height + S(12)),
                   S(8), Color(255, 255, 255, 255));
        std::string wp = wan_qr_payload();
        if (!wp.empty() && wp != g_wanPayload) {
            int n = 0;
            std::vector<unsigned char> mods = qr_encode(wp, &n);
            if (n > 0) { g_wanPayload = wp; g_wanMods = mods; g_wanN = n; }
        }
        draw_qr_mods(g, qrBox, g_wanMods, g_wanN);

        float bw = c.Width - S(52) - qrSide - S(16);
        RectF b(px, y, bw, S(40));
        fill_round(g, b, S(10), Color(255, 26, 31, 41));
        stroke_round(g, RectF(b.X + 0.5f, b.Y + 0.5f, b.Width - 1, b.Height - 1), S(10), C::line, 1.0f);
        text(g, full, RectF(b.X + S(14), b.Y, b.Width - S(120), b.Height), F(S(11.4f)), C::text, 0);
        RectF cp(b.GetRight() - S(84), b.Y + S(6), S(78), S(28));
        float ca = anim_val(akey(H_WAN_COPY, -1), g_hoverId == H_WAN_COPY ? 1.f : 0.f);
        fill_round(g, cp, S(7), mix(Color(255, 40, 46, 60), Color(255, 52, 66, 96), ca));
        text(g, L"复制", cp, F(S(10.6f)), Color(255, 170, 196, 255), 1, false);
        add_hit(cp.X, cp.Y, cp.Width, cp.Height, H_WAN_COPY);

        y += S(52);
    }

    std::wstring pub = utf8_to_w(publicIp.empty() ? extIp : publicIp);
    if (pub.empty()) pub = L"未获取到";
    if (v6.empty()) kv(L"公网地址", pub, pub == L"未获取到" ? C::text3 : C::text);
    kv(L"映射端口", mapped ? std::to_wstring(extPort) : L"—", C::text);
    kv(L"本机地址", utf8_to_w(net_primary_ip()) + L":" + std::to_wstring(g_set.port), C::text2);

    y += S(6);
    // pairing code
    {
        text(g, L"配对码", RectF(px, y, S(110), S(22)), F(S(11.4f)), C::text3, 0);
        text(g, utf8_to_w(g_set.wanCode), RectF(px + S(104), y, S(200), S(22)),
             F(S(13.0f), FontStyleBold), C::accent, 0);
        y += S(30);
        text(g, L"手机连接时要输入这 6 位，防止别人连进来。",
             RectF(px, y, c.Width - S(52), S(20)), F(S(10.4f)), C::text3, 0);
        y += S(28);
    }

    if (!err.empty() && !mapped) {
        text(g, utf8_to_w(err), RectF(px, y, c.Width - S(52), S(40)), F(S(11.0f)), C::red, 0);
        y += S(26);
        text(g, L"可在路由器上手动把 TCP " + std::to_wstring(g_set.port) +
                L" 转发到本机，然后用上面的公网地址连接。",
             RectF(px, y, c.Width - S(52), S(40)), F(S(10.6f)), C::text3, 0);
        y += S(30);
    }

    y += S(10);
    text(g, L"主动连接其它设备", RectF(px, y, S(300), S(22)), F(S(12.4f), FontStyleBold), C::text, 0);
    y += S(26);
    auto field = [&](const wchar_t* label, int mode, int id, const std::wstring& value,
                     const wchar_t* hint) {
        text(g, label, RectF(px, y + S(8), S(78), S(20)), F(S(10.6f)), C::text3, 0);
        RectF fb(px + S(80), y, c.Width - S(52) - S(80), S(32));
        bool foc = (g_edit == mode);
        fill_round(g, fb, S(8), Color(255, 15, 18, 24));
        stroke_round(g, RectF(fb.X + 0.5f, fb.Y + 0.5f, fb.Width - 1, fb.Height - 1), S(8),
                     foc ? C::accent : C::line, 1.0f);
        std::wstring v = foc ? g_editText : value;
        if (v.empty() && !foc) text(g, hint, RectF(fb.X + S(11), fb.Y, fb.Width - S(22), fb.Height),
                                     F(S(11.8f)), C::text3, 0);
        else text(g, v, RectF(fb.X + S(11), fb.Y, fb.Width - S(22), fb.Height), F(S(11.8f)), C::text, 0);
        if (foc) {
            float tw = text_w(g, v.substr(0, g_caret), F(S(11.8f)));
            if ((GetTickCount64() / 530) % 2 == 0) {
                float cx = std::min(fb.X + S(11) + tw, fb.GetRight() - S(8));
                Pen pen(C::accent, 1.4f);
                g.DrawLine(&pen, cx, fb.Y + S(8), cx, fb.GetBottom() - S(8));
            }
            g_animating = true;
        }
        add_hit(fb.X, fb.Y, fb.Width, fb.Height, id);
        y += S(40);
    };
    field(L"IPv6 地址", 3, H_WAN_ADDR, g_set.wanPeerAddr, L"[240e:...]:45678");
    field(L"配对码", 4, H_WAN_CODE, g_set.wanPeerCode, L"对方电脑显示的 6 位数字");
    {
        RectF b(px, y + S(4), S(140), S(38));
        float a2 = anim_val(akey(H_WAN_CONNECT, -1), g_hoverId == H_WAN_CONNECT ? 1.f : 0.f);
        fill_round(g, b, S(10), mix(Color(255, 52, 66, 96), Color(255, 66, 86, 130), a2));
        text(g, L"连接", b, F(S(11.6f)), Color(255, 235, 242, 255), 1, false);
        add_hit(b.X, b.Y, b.Width, b.Height, H_WAN_CONNECT);
    }
    y += S(52);

    {
        RectF b(px, y, S(140), S(38));
        float a = anim_val(akey(H_WAN_TOGGLE, -1), g_hoverId == H_WAN_TOGGLE ? 1.f : 0.f);
        Color bgc = mapped ? Color(255, 70, 38, 40) : Color(255, 60, 105, 200);
        fill_round(g, b, S(10), mix(with_alpha(bgc, 200), bgc, a));
        text(g, busy ? L"处理中…" : (mapped ? L"关闭映射" : L"开启映射"), b,
             F(S(11.6f)), Color(255, 240, 244, 255), 1, false);
        if (!busy) add_hit(b.X, b.Y, b.Width, b.Height, H_WAN_TOGGLE);
        y += S(46);
    }

    if (mapped) {
        std::wstring addr = pub + L":" + std::to_wstring(extPort);
        RectF b(px, y, c.Width - S(52), S(40));
        fill_round(g, b, S(10), Color(255, 26, 31, 41));
        stroke_round(g, RectF(b.X + 0.5f, b.Y + 0.5f, b.Width - 1, b.Height - 1), S(10), C::line, 1.0f);
        text(g, addr, RectF(b.X + S(14), b.Y, b.Width - S(120), b.Height), F(S(12.6f)), C::text, 0);
        RectF cp(b.GetRight() - S(84), b.Y + S(6), S(78), S(28));
        float ca = anim_val(akey(H_WAN_COPY, -1), g_hoverId == H_WAN_COPY ? 1.f : 0.f);
        fill_round(g, cp, S(7), mix(Color(255, 40, 46, 60), Color(255, 52, 66, 96), ca));
        text(g, L"复制", cp, F(S(10.6f)), Color(255, 170, 196, 255), 1, false);
        add_hit(cp.X, cp.Y, cp.Width, cp.Height, H_WAN_COPY);
    }
}
// ------------------------------------------------------- pages: device management
struct DevGroup {
    std::wstring name, ip;
    std::vector<std::wstring> stamps;   // newest last
};

static std::vector<DevGroup> load_device_groups() {
    std::vector<std::wstring> lines = device_log_lines();
    std::vector<DevGroup> out;
    for (auto& s : lines) {
        // name \t ip \t port \t timestamp
        std::vector<std::wstring> f;
        size_t p = 0;
        while (true) {
            size_t t = s.find(L'\t', p);
            if (t == std::wstring::npos) { f.push_back(s.substr(p)); break; }
            f.push_back(s.substr(p, t - p));
            p = t + 1;
        }
        if (f.size() < 4) continue;
        DevGroup* g = nullptr;
        for (auto& d : out) if (d.name == f[0] && d.ip == f[1]) { g = &d; break; }
        if (!g) { DevGroup d; d.name = f[0]; d.ip = f[1]; out.push_back(d); g = &out.back(); }
        g->stamps.push_back(f[3]);
    }
    return out;
}

static void draw_page_devices(Graphics& g, float x0, float x1, float y0, float y1) {
    // back chip
    {
        RectF b(x0, y0 + S(6), S(86), S(32));
        float a = anim_val(akey(H_DEV_BACK, -1), g_hoverId == H_DEV_BACK ? 1.f : 0.f);
        fill_round(g, b, S(9), mix(Color(255, 26, 31, 41), Color(255, 36, 44, 60), a));
        Pen pen(mix(C::text2, C::text, a), S(1.7f));
        pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, b.X + S(26), b.Y + S(16), b.X + S(18), b.Y + S(16));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(11));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(21));
        text(g, L"返回", RectF(b.X + S(30), b.Y, b.Width - S(36), b.Height), F(S(11.8f)),
             mix(C::text2, C::text, a), 0);
        add_hit(b.X, b.Y, b.Width, b.Height, H_DEV_BACK);
    }

    RectF c(x0, y0 + S(46), x1 - x0, y1 - y0 - S(46));
    card(g, c);

    std::vector<DevGroup> groups = load_device_groups();
    float listTop = c.Y + S(12);
    float viewH = c.Height - S(24);
    if (groups.empty()) {
        text(g, L"还没有连接过设备", RectF(c.X, listTop + viewH / 2 - S(24), c.Width, S(22)),
             F(S(12.0f)), C::text2, 1, false);
        text(g, L"连上任意一台设备后，这里会记录设备名和每次连接的时间",
             RectF(c.X, listTop + viewH / 2, c.Width, S(20)), F(S(10.5f)), C::text3, 1, false);
        return;
    }

    float rowH = S(60);
    float contentH = groups.size() * rowH;
    float maxScroll = std::max(0.f, contentH - viewH);
    if (g_scroll[5] < 0) g_scroll[5] = 0;
    if (g_scroll[5] > maxScroll) g_scroll[5] = maxScroll;

    g.SetClip(RectF(c.X + S(1), listTop, c.Width - S(2), viewH));
    float ry = listTop - g_scroll[5];
    for (size_t i = 0; i < groups.size(); i++) {
        if (ry + rowH < listTop - S(4)) { ry += rowH; continue; }
        if (ry > listTop + viewH) break;
        DevGroup& d = groups[i];
        float ha = anim_val(akey(H_SET_DEVICES + 10 + (int)i, -1),
                            (g_hoverId == H_SET_DEVICES + 10 && g_hoverArg == (int)i) ? 1.f : 0.f);
        if (ha > 0.01f) fill_round(g, RectF(c.X + S(12), ry + S(2), c.Width - S(24), rowH - S(6)),
                                   S(8), Color((BYTE)(18 * ha), 255, 255, 255));
        avatar(g, RectF(c.X + S(18), ry + (rowH - S(34)) / 2, S(34), S(34)), d.name, false);
        text(g, d.name, RectF(c.X + S(62), ry + S(8), c.Width - S(220), S(20)),
             F(S(12.6f)), C::text, 0);
        std::wstring sub = d.ip + L" · 连接过 " + std::to_wstring(d.stamps.size()) + L" 次";
        if (!d.stamps.empty()) sub += L" · 最近 " + d.stamps.back();
        text(g, sub, RectF(c.X + S(62), ry + S(28), c.Width - S(220), S(18)),
             F(S(10.2f)), C::text3, 0);

        // delete this device entirely
        RectF db(c.GetRight() - S(88), ry + (rowH - S(26)) / 2, S(58), S(26));
        float da = anim_val(akey(H_DEV_DELETE, (int)i),
                            (g_hoverId == H_DEV_DELETE && g_hoverArg == (int)i) ? 1.f : 0.f);
        fill_round(g, db, S(7), mix(Color(255, 40, 32, 34), Color(255, 74, 36, 38), da));
        text(g, L"删除", db, F(S(10.2f)),
             mix(Color(255, 196, 110, 106), Color(255, 242, 140, 132), da), 1, false);
        add_hit(db.X, db.Y, db.Width, db.Height, H_DEV_DELETE, (int)i);

        // whole row opens the time list
        icon(g, I_CHEV, RectF(c.GetRight() - S(24), ry + (rowH - S(14)) / 2, S(14), S(14)),
             Color(255, 78, 88, 104), 1.5f);
        add_hit(c.X + S(12), ry, c.Width - S(110), rowH, H_DEV_DELETE + 100, (int)i);
        ry += rowH;
    }
    g.ResetClip();
    (void)g_devSel;
}

static void draw_page_device_detail(Graphics& g, float x0, float x1, float y0, float y1) {
    {
        RectF b(x0, y0 + S(6), S(86), S(32));
        float a = anim_val(akey(H_DEV_BACK, -1), g_hoverId == H_DEV_BACK ? 1.f : 0.f);
        fill_round(g, b, S(9), mix(Color(255, 26, 31, 41), Color(255, 36, 44, 60), a));
        Pen pen(mix(C::text2, C::text, a), S(1.7f));
        pen.SetStartCap(LineCapRound); pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, b.X + S(26), b.Y + S(16), b.X + S(18), b.Y + S(16));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(11));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(21));
        text(g, L"返回", RectF(b.X + S(30), b.Y, b.Width - S(36), b.Height), F(S(11.8f)),
             mix(C::text2, C::text, a), 0);
        add_hit(b.X, b.Y, b.Width, b.Height, H_DEV_BACK);
    }

    RectF c(x0, y0 + S(46), x1 - x0, y1 - y0 - S(46));
    card(g, c);

    std::vector<DevGroup> groups = load_device_groups();
    DevGroup* d = nullptr;
    for (auto& g2 : groups) {
        if (g2.ip == g_devSel || g2.name == g_devSel) { d = &g2; break; }
    }
    if (!d) {
        text(g, L"请从「设备管理」里选择一台设备", RectF(c.X, c.Y + c.Height / 2, c.Width, S(22)), F(S(12.0f)), C::text2, 1, false);
        return;
    }

    float px = c.X + S(24);
    float y = c.Y + S(20);
    text(g, d->name, RectF(px, y, c.Width - S(48), S(28)), F(S(17.0f), FontStyleBold), C::text, 0, false);
    y += S(28);
    text(g, d->ip + L" · 共连接 " + std::to_wstring(d->stamps.size()) + L" 次",
         RectF(px, y, c.Width - S(48), S(20)), F(S(11.0f)), C::text3, 0);
    y += S(26);

    RectF del(c.GetRight() - S(120), c.Y + S(18), S(96), S(30));
    float da = anim_val(akey(H_DEV_DELETE, -1), g_hoverId == H_DEV_DELETE ? 1.f : 0.f);
    fill_round(g, del, S(8), mix(Color(255, 44, 34, 36), Color(255, 82, 40, 42), da));
    text(g, L"删除此设备", del, F(S(10.6f)),
         mix(Color(255, 200, 112, 108), Color(255, 246, 148, 140), da), 1, false);
    add_hit(del.X, del.Y, del.Width, del.Height, H_DEV_DELETE_ALL, -1);

    hline(g, px, y, c.Width - S(48), C::lineSoft);
    y += S(14);
    text(g, L"连接记录", RectF(px, y, S(200), S(20)), F(S(11.6f)), C::text2, 0);
    y += S(24);

    float listTop = y;
    float viewH = c.GetBottom() - S(16) - listTop;
    float rowH = S(30);
    float contentH = d->stamps.size() * rowH;
    float maxScroll = std::max(0.f, contentH - viewH);
    if (g_scroll[6] < 0) g_scroll[6] = 0;
    if (g_scroll[6] > maxScroll) g_scroll[6] = maxScroll;

    g.SetClip(RectF(c.X + S(1), listTop, c.Width - S(2), viewH));
    float ry = listTop - g_scroll[6];
    // newest first
    for (int i = (int)d->stamps.size() - 1; i >= 0; i--) {
        if (ry + rowH < listTop - S(4)) { ry += rowH; continue; }
        if (ry > listTop + viewH) break;
        SolidBrush dot(Color(255, 96, 132, 200));
        g.FillEllipse(&dot, RectF(px + S(3), ry + S(12), S(5), S(5)));
        text(g, d->stamps[i], RectF(px + S(18), ry, c.Width - S(80), rowH),
             F(S(11.4f)), C::text2, 0);
        ry += rowH;
    }
    g.ResetClip();
    if (maxScroll > 0) {
        float barH = std::max(S(24), viewH * viewH / contentH);
        float barY = listTop + (viewH - barH) * (g_scroll[6] / maxScroll);
        fill_round(g, RectF(c.GetRight() - S(6), barY, S(3), barH), S(1.5f), Color(70, 255, 255, 255));
    }
}

// ------------------------------------------------------------------ page: about
static void draw_page_about(Graphics& g, float x0, float x1, float y0, float y1) {    // back chip
    {
        RectF b(x0, y0 + S(6), S(86), S(32));
        float a = anim_val(akey(H_ABOUT_BACK, -1), g_hoverId == H_ABOUT_BACK ? 1.f : 0.f);
        fill_round(g, b, S(9), mix(Color(255, 26, 31, 41), Color(255, 36, 44, 60), a));
        Pen pen(mix(C::text2, C::text, a), S(1.7f));
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(26), b.Y + S(16) - S(0.01f));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(11));
        g.DrawLine(&pen, b.X + S(18), b.Y + S(16), b.X + S(22), b.Y + S(21));
        text(g, L"返回", RectF(b.X + S(30), b.Y, b.Width - S(36), b.Height), F(S(11.8f)),
             mix(C::text2, C::text, a), 0);
        add_hit(b.X, b.Y, b.Width, b.Height, H_ABOUT_BACK);
    }

    RectF c(x0, y0 + S(46), x1 - x0, y1 - y0 - S(46));
    card(g, c);

    float px = c.X + S(28);
    float right = c.GetRight() - S(28);
    float y = c.Y + S(26);

    // logo mark
    {
        RectF lr(px, y, S(46), S(46));
        GraphicsPath* p = rp(lr, S(13));
        LinearGradientBrush lg(PointF(lr.X, lr.Y), PointF(lr.GetRight(), lr.GetBottom()),
                               Color(255, 94, 145, 255), Color(255, 126, 92, 255));
        g.FillPath(&lg, p);
        delete p;
        icon(g, I_SEND, RectF(lr.X + S(13), lr.Y + S(13), S(20), S(20)), Color(255, 255, 255, 255), 1.7f);
    }
    text(g, L"AirLink 隔空快传", RectF(px + S(60), y + S(2), S(400), S(26)),
         F(S(17.0f), FontStyleBold), C::text, 0, false);
    text(g, L"Windows 版 1.0 · 单文件绿色版", RectF(px + S(60), y + S(28), S(400), S(20)),
         F(S(11.0f)), C::text3, 0);
    y += S(62);
    hline(g, px, y, c.Width - S(56), C::lineSoft);
    y += S(20);

    // spec table
    struct Line { std::wstring k, v; };
    std::vector<Line> lines = {
        { L"程序大小", L"约 532 KB，静态链接，无运行库依赖" },
        { L"支持系统", L"Windows 10 / 11（64 位）" },
        { L"传输端口", std::to_wstring(g_set.port) + L"（TCP）· 45679（UDP 发现）" },
        { L"传输方式", L"局域网点对点直连，文件不经过任何服务器" },
        { L"设备文件", L"可浏览对方整台设备的文件系统，按需复制" },
        { L"扫码连接", L"手机扫描本机二维码即可连上，无需手动输入 IP" },
        { L"设置位置", L"settings.ini 与历史记录存放在 %APPDATA%\\AirLink\\" },
    };
    for (auto& l : lines) {
        text(g, l.k, RectF(px, y, S(120), S(22)), F(S(11.6f)), C::text3, 0);
        text(g, l.v, RectF(px + S(110), y, c.Width - S(170), S(22)), F(S(11.6f)), C::text2, 0);
        y += S(26);
    }

    y += S(8);
    hline(g, px, y, c.Width - S(56), C::lineSoft);
    y += S(18);
    text(g, L"通信协议", RectF(px, y, S(200), S(22)), F(S(12.6f), FontStyleBold), C::text, 0);
    y += S(26);
    struct P { std::wstring k, v; };
    std::vector<P> proto = {
        { L"UDP 45679", L"广播发现：AIRLINK1\\t<id>\\t<名字>\\t<端口>" },
        { L"TCP 类型 1", L"推送文件：文件数 + 每个名字/大小，回 1 字节接受或拒绝" },
        { L"TCP 类型 2", L"列目录：路径 → 条目（名字/完整路径/是否目录/大小/时间）" },
        { L"TCP 类型 3", L"取文件：路径 → 大小 + 原始字节" },
        { L"TCP 类型 4", L"心跳探测，用来判断对方是否还在线" },
        { L"TCP 类型 5", L"主动断开通知" },
        { L"TCP 类型 6", L"删除文件：按文件名删接收目录下的副本" },
    };
    for (auto& l : proto) {
        text(g, l.k, RectF(px, y, S(120), S(22)), F(S(10.8f)), C::accent, 0);
        text(g, l.v, RectF(px + S(110), y, c.Width - S(170), S(22)), F(S(10.8f)), C::text3, 0);
        y += S(24);
    }

    y += S(10);
    hline(g, px, y, c.Width - S(56), C::lineSoft);
    y += S(18);
    text(g, L"两台设备连到同一个网络，打开本程序就能互相发现；"
            L"也可以直接用手机扫描首页的二维码连接。",
         RectF(px, y, c.Width - S(56), S(44)), F(S(11.2f)), C::text2, 0);
    y += S(30);
    text(g, L"文件只在本机与对方之间直接传输，不经过任何服务器，也不会留下副本。",
         RectF(px, y, c.Width - S(56), S(22)), F(S(11.2f)), C::text3, 0);
}

// ------------------------------------------------------------------ paint
BOOL ui_background_erase(HWND, HDC) { return TRUE; }

void ui_set_size(int w, int h) {
    g_w = w; g_h = h;
}

void ui_paint(HWND hwnd, HDC hdc) {
    ensure_gdiplus();

    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;
    g_w = W; g_h = H;

    static HBITMAP  sBmp = nullptr;
    static HDC      sMem = nullptr;
    static int      sW = 0, sH = 0;
    if (!sMem) sMem = CreateCompatibleDC(hdc);
    if (!sBmp || sW != W || sH != H) {
        if (sBmp) DeleteObject(sBmp);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = W;
        bi.bmiHeader.biHeight = -H;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        sBmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        sW = W; sH = H;
        sHasScene = false;      // fresh buffer -> must do a full paint first
    }
    HGDIOBJ old = SelectObject(sMem, sBmp);

    // Fast path: when only the "searching" pulse was invalidated, we can repaint
    // just that small rect on top of the cached scene instead of the whole window.
    {
        RECT cb{};
        bool partial = false;
        if (sHasScene && g_searchRect.Width > 0 && GetClipBox(hdc, &cb) != ERROR) {
            const float pad = 2;
            float l = g_searchRect.X - pad, t = g_searchRect.Y - pad;
            float r = g_searchRect.GetRight() + pad, b = g_searchRect.GetBottom() + pad;
            partial = cb.left >= (LONG)l && cb.top >= (LONG)t &&
                      cb.right <= (LONG)r && cb.bottom <= (LONG)b;
        }
        if (partial) {
            Graphics g(sMem);
            g.SetSmoothingMode(SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(PixelOffsetModeHalf);
            g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
            g.SetCompositingQuality(CompositingQualityHighQuality);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            g.SetClip(g_searchRect);
            SolidBrush bg(C::card);
            g.FillRectangle(&bg, g_searchRect);
            g_searchPulse = false;
            g_searchPartial = true;
            draw_searching_block(g, g_searchC2, g_searchTop, g_searchBottom);
            g_searchPartial = false;
            BitBlt(hdc, (int)g_searchRect.X, (int)g_searchRect.Y,
                   (int)g_searchRect.Width + 1, (int)g_searchRect.Height + 1, sMem,
                   (int)g_searchRect.X, (int)g_searchRect.Y, SRCCOPY);
            SelectObject(sMem, old);
            return;
        }
    }

    {
        Graphics g(sMem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.SetCompositingQuality(CompositingQualityHighQuality);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);

        g_animating = false;
        g_searchPulse = false;
        g_searchRect = RectF();     // a full paint re-establishes it if still searching
        g_hits.clear();

        // background
        SolidBrush bg(C::bg);
        g.FillRectangle(&bg, 0, 0, W, H);

        float s = g_s;
        float sideW = S(214);
        float titleH = S(52);

        // ---------------- sidebar
        {
            SolidBrush sb(C::side);
            g.FillRectangle(&sb, 0.0f, 0.0f, sideW, (REAL)H);
            Pen pen(C::lineSoft, 1.0f);
            g.DrawLine(&pen, sideW - 0.5f, 0.0f, sideW - 0.5f, (REAL)H);
        }

        // logo
        {
            RectF lr(S(18), S(16), S(34), S(34));
            GraphicsPath* p = rp(lr, S(10));
            LinearGradientBrush lg(PointF(lr.X, lr.Y), PointF(lr.GetRight(), lr.GetBottom()), Color(255, 94, 145, 255), Color(255, 126, 92, 255));
            g.FillPath(&lg, p);
            delete p;
            icon(g, I_SEND, RectF(lr.X + S(9), lr.Y + S(9), S(16), S(16)), Color(255, 255, 255, 255), 1.6f);

            text(g, L"AirLink", RectF(S(60), S(14), S(120), S(22)), F(S(15.5f), FontStyleBold), C::text, 0, false);
            text(g, L"隔空快传", RectF(S(60), S(34), S(120), S(18)), F(S(10.2f)), C::text3, 0, false);
        }

        // nav
        {
            const wchar_t* labels[5] = { L"快传", L"设备文件", L"记录", L"WAN连接", L"设置" };
            Icon icons[5] = { I_SEND, I_FOLDER, I_CLOCK, I_WIFI, I_SLIDERS };
            int ids[5] = { H_NAV_SEND, H_NAV_FILES, H_NAV_HIST, H_NAV_WAN, H_NAV_SET };
            int pages[5] = { 0, 3, 1, 7, 2 };
            float y = S(84);
            for (int i = 0; i < 5; i++) {
                RectF nr(S(12), y, sideW - S(24), S(42));
                bool active = (g_page == pages[i]);
                float ha = anim_val(akey(ids[i], -1), (g_hoverId == ids[i]) ? 1.f : 0.f);
                if (active) {
                    fill_round(g, nr, S(9), Color(255, 30, 42, 68));
                    RectF bar(nr.X + S(0), nr.Y + S(11), S(3), S(20));
                    fill_round(g, bar, S(1.5f), C::accent);
                } else if (ha > 0.01f) {
                    fill_round(g, nr, S(9), Color((BYTE)(12 * ha), 255, 255, 255));
                }
                Color ic = active ? C::accent : mix(C::text3, C::text, ha);
                Color lc = active ? Color(255, 216, 228, 255) : mix(C::text2, C::text, ha);
                icon(g, icons[i], RectF(nr.X + S(16), nr.Y + S(11), S(19), S(19)), ic, 1.75f);
                text(g, labels[i], RectF(nr.X + S(48), nr.Y, S(130), nr.Height), F(S(13.0f)), lc, 0, false);
                add_hit(nr.X, nr.Y, nr.Width, nr.Height, ids[i]);
                y += S(46);
            }
        }

        // sidebar bottom: receive toggle card
        {
            float ch = S(92);
            RectF cr(S(12), H - S(30) - ch, sideW - S(24), ch);
            fill_round(g, cr, S(12), Color(255, 21, 26, 34));
            stroke_round(g, RectF(cr.X + 0.5f, cr.Y + 0.5f, cr.Width - 1, cr.Height - 1), S(12), C::lineSoft, 1.0f);

            text(g, L"接收文件", RectF(cr.X + S(14), cr.Y + S(10), S(90), S(22)), F(S(12.0f)), C::text, 0);
            {
                RectF sr(cr.GetRight() - S(14) - S(40), cr.Y + S(11), S(40), S(22));
                bool on = g_set.autoAccept;
                switch_widget(g, sr.X, sr.Y, on, H_SET_AUTO, anim_val(akey(H_SET_AUTO, -1), g_hoverId == H_SET_AUTO));
            }
            hline(g, cr.X + S(14), cr.Y + S(40), cr.Width - S(28), C::lineSoft);
            {
                SolidBrush dot(has_usb_link() ? C::accent : Color(255, 63, 214, 140));
                g.FillEllipse(&dot, cr.X + S(15), cr.Y + S(56), S(6), S(6));
                std::wstring st = has_usb_link() ? L"USB 直连 · 端口 " : L"监听中 · 端口 ";
                st += std::to_wstring(g_set.port);
                text(g, st, RectF(cr.X + S(27), cr.Y + S(50), cr.Width - S(40), S(20)), F(S(10.5f)), C::text3, 0);
            }
            {
                int pc = 0; { std::lock_guard<std::mutex> lk(g_mu); pc = (int)g_peers.size(); }
                std::wstring st = std::to_wstring(pc) + L" 台设备在线";
                text(g, st, RectF(cr.X + S(14), cr.Y + S(70), cr.Width - S(24), S(18)), F(S(10.0f)), Color(255, 78, 88, 104), 0);
            }
        }

        // ---------------- window buttons
        {
            float bw = S(46), bh = S(38);
            float bx = W - bw * 3 - S(6);
            RectF r1(bx, S(6), bw, bh), r2(bx + bw, S(6), bw, bh), r3(bx + 2 * bw, S(6), bw, bh);
            auto wbtn = [&](const RectF& r, Icon ic, int id, bool danger) {
                float a = anim_val(akey(id, -1), g_hoverId == id ? 1.f : 0.f);
                if (a > 0.01f) {
                    fill_round(g, r, S(8), danger ? Color((BYTE)(235 * a), 232, 72, 72)
                                                  : Color((BYTE)(26 * a), 255, 255, 255));
                }
                icon(g, ic, RectF(r.X + r.Width / 2 - S(9), r.Y + r.Height / 2 - S(9), S(18), S(18)),
                     mix(C::text2, danger ? Color(255, 255, 255, 255) : C::text, a), 1.5f);
                add_hit(r.X, r.Y, r.Width, r.Height, id);
            };
            wbtn(r1, I_MIN, H_MIN, false);
            wbtn(r2, I_MAX, H_MAX, false);
            wbtn(r3, I_CLOSE, H_CLOSE, true);
        }

        // ---------------- content
        float x0 = sideW + S(26);
        float x1 = W - S(26);
        float y0 = titleH + S(16);
        float y1 = H - S(20);

        if (g_page == 0) {
            text(g, L"快传", RectF(x0, y0 - S(2), S(200), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_transfer(g, x0, x1, y0, y1);
        } else if (g_page == 3) {
            text(g, L"设备文件", RectF(x0, y0 - S(2), S(260), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_files(g, x0, x1, y0, y1);
        } else if (g_page == 1) {
            text(g, L"记录", RectF(x0, y0 - S(2), S(200), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_history(g, x0, x1, y0, y1);
        } else if (g_page == 4) {
            text(g, L"关于", RectF(x0 + S(100), y0 - S(2), S(200), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_about(g, x0, x1, y0, y1);
        } else if (g_page == 7) {
            text(g, L"WAN连接", RectF(x0, y0 - S(2), S(260), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_wan(g, x0, x1, y0, y1);
        } else if (g_page == 5) {
            text(g, L"设备管理", RectF(x0 + S(100), y0 - S(2), S(260), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_devices(g, x0, x1, y0, y1);
        } else if (g_page == 6) {
            text(g, L"设备详情", RectF(x0 + S(100), y0 - S(2), S(260), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_device_detail(g, x0, x1, y0, y1);
        } else {
            text(g, L"设置", RectF(x0, y0 - S(2), S(200), S(34)), F(S(20.0f), FontStyleBold), C::text, 0, false);
            draw_page_settings(g, x0, x1, y0, y1);
        }

        // ---------------- device row context menu
        if (g_ctxPeer >= 0) {
            std::wstring name;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (g_ctxPeer < (int)g_peers.size()) name = utf8_to_w(g_peers[g_ctxPeer].name);
            }
            if (name.empty()) {
                g_ctxPeer = -1;
            } else {
                float mw = S(190), mh = S(44);
                float mx = std::min(g_ctxX, W - mw - S(12));
                float my = std::min(g_ctxY, H - mh - S(12));
                if (mx < S(8)) mx = S(8);
                if (my < S(8)) my = S(8);
                // soft shadow then the card
                fill_round(g, RectF(mx + S(1), my + S(3), mw, mh), S(10), Color(90, 0, 0, 0));
                fill_round(g, RectF(mx, my, mw, mh), S(10), Color(255, 27, 33, 44));
                stroke_round(g, RectF(mx + 0.5f, my + 0.5f, mw - 1, mh - 1), S(10), Color(255, 58, 68, 88), 1.0f);
                float ha = anim_val(akey(H_CTX_DELETE, -1),
                                    (g_hoverId == H_CTX_DELETE) ? 1.f : 0.f);
                if (ha > 0.01f) fill_round(g, RectF(mx + S(4), my + S(4), mw - S(8), mh - S(8)), S(8),
                                           Color((BYTE)(48 * ha), 120, 52, 56));
                icon(g, I_TRASH, RectF(mx + S(14), my + (mh - S(15)) / 2, S(15), S(15)),
                     mix(Color(255, 200, 112, 108), Color(255, 244, 146, 138), ha), 1.5f);
                text(g, L"删除本条记录？", RectF(mx + S(38), my, mw - S(46), mh), F(S(11.6f)),
                     mix(Color(255, 208, 120, 116), Color(255, 246, 154, 146), ha), 0);
                add_hit(mx + S(4), my + S(4), mw - S(8), mh - S(8), H_CTX_DELETE, g_ctxPeer);
            }
        }

        // ---------------- toasts
        {
            std::lock_guard<std::mutex> lk(g_mu);
            ULONGLONG now = GetTickCount64();
            while (!g_toasts.empty() && g_toasts.front().second < now) g_toasts.erase(g_toasts.begin());
            if (!g_toasts.empty()) {
                std::wstring t = g_toasts.back().first;
                Font* f = F(S(12.0f));
                float tw = text_w(g, t, f);
                float pad = S(16);
                float w = tw + pad * 2 + S(20);
                RectF r(W / 2 - w / 2, H - S(72), w, S(40));
                fill_round(g, r, S(20), Color(255, 32, 38, 50));
                stroke_round(g, RectF(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1), S(20), C::line, 1.0f);
                icon(g, I_CHECK, RectF(r.X + S(14), r.Y + S(13), S(14), S(14)), C::green, 1.8f);
                text(g, t, RectF(r.X + S(36), r.Y, tw + S(4), r.Height), f, C::text, 0, false);
                if (g_toasts.back().second - now < 400) g_animating = true;
            }
        }
        g.DrawImage(nullptr, 0, 0, 0, 0);
    }

    BitBlt(hdc, 0, 0, W, H, sMem, 0, 0, SRCCOPY);
    sHasScene = true;
    SelectObject(sMem, old);
}

// ------------------------------------------------------------------ interaction
static Hit* hit_at(int x, int y) {
    for (int i = (int)g_hits.size() - 1; i >= 0; i--) {
        Hit& h = g_hits[i];
        if (x >= h.x && x < h.x + h.w && y >= h.y && y < h.y + h.h) return &h;
    }
    return nullptr;
}

void ui_mouse_move(int x, int y) {
    Hit* h = hit_at(x, y);
    int id = h ? h->id : -1, arg = h ? h->arg : -1;
    if (id != g_hoverId || arg != g_hoverArg) {
        g_hoverId = id; g_hoverArg = arg;
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}
void ui_mouse_leave() {
    if (g_hoverId != -1) { g_hoverId = -1; g_hoverArg = -1; InvalidateRect(g_hwnd, nullptr, FALSE); }
}
void ui_mouse_down(int x, int y) {
    Hit* h = hit_at(x, y);
    g_pressId = h ? h->id : -1;
    g_pressArg = h ? h->arg : -1;
}

// Right click on a device row opens a one-item menu that removes the row.
void ui_right_click(int x, int y) {
    Hit* h = hit_at(x, y);
    if (h && h->id == H_PEER && g_page == 0) {
        g_ctxPeer = h->arg;
        g_ctxX = (float)x;
        g_ctxY = (float)y;
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }
    if (g_ctxPeer >= 0) { g_ctxPeer = -1; InvalidateRect(g_hwnd, nullptr, FALSE); }
}
void ui_mouse_wheel(int x, int y, int delta) {
    float step = -delta / 120.0f * S(64);
    if (g_page == 0) g_scroll[0] += step;
    else if (g_page == 1) g_scroll[1] += step;
    else if (g_page == 3) g_scroll[3] += step;
    else if (g_page == 2) g_scroll[2] += step;
    else if (g_page == 5) g_scroll[5] += step;
    else if (g_page == 6) g_scroll[6] += step;
    InvalidateRect(g_hwnd, nullptr, FALSE);
    (void)x; (void)y;
}

void toast(const std::wstring& text) {
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_toasts.push_back({ text, GetTickCount64() + 2200 });
        if (g_toasts.size() > 4) g_toasts.erase(g_toasts.begin());
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

static void open_path(const std::wstring& p) {
    ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static void pick_and_send(const Peer& peer) {
    std::vector<std::wstring> files;
    std::vector<wchar_t> buf(64 * 1024, 0);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = L"所有文件\0*.*\0\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"选择要发送的文件";
    if (!GetOpenFileNameW(&ofn)) return;
    wchar_t* p = buf.data();
    std::wstring dir = p;
    p += dir.size() + 1;
    if (*p == 0) {
        files.push_back(dir);
    } else {
        while (*p) {
            std::wstring name = p;
            files.push_back(dir + L"\\" + name);
            p += name.size() + 1;
        }
    }
    if (!files.empty()) net_start_send(files, peer);
}

void ui_drop_files(std::vector<std::wstring> paths, int x, int y) {
    if (paths.empty()) return;
    // dropped on a peer row?
    Peer target;
    bool found = false;
    Hit* h = hit_at(x, y);
    if (h && (h->id == H_PEER || h->id == H_PEER_SEND)) {
        std::lock_guard<std::mutex> lk(g_mu);
        if (h->arg >= 0 && h->arg < (int)g_peers.size()) { target = g_peers[h->arg]; found = true; }
    }
    if (!found && !g_selPeer.empty()) {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& p : g_peers) if (p.id == g_selPeer) { target = p; found = true; break; }
    }
    if (!found && !g_peers.empty()) {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_peers.empty()) { target = g_peers[0]; found = true; }
    }
    if (!found) {
        toast(L"没有在线的设备，请先连接同一网络");
        return;
    }
    g_selPeer = target.id;
    net_start_send(paths, target);
}

void ui_set_selected_peer(const std::string& id) { g_selPeer = id; }
void ui_clear_selection_if_ip(const std::string& ip) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& p : g_peers) {
        if (p.ip == ip && g_selPeer == p.id) { g_selPeer.clear(); break; }
    }
}
void ui_reload_history() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_hist = history_load_lines();
}
void ui_focus_lost() { g_hoverId = -1; g_hoverArg = -1; }

static void commit_edit() {
    if (g_edit == 1) {
        std::wstring v = g_editText;
        while (!v.empty() && v.front() == L' ') v.erase(v.begin());
        while (!v.empty() && v.back() == L' ') v.pop_back();
        if (!v.empty()) g_set.deviceName = v;
        settings_save();
    } else if (g_edit == 2) {
        int p = _wtoi(g_editText.c_str());
        if (p >= 1024 && p <= 65535) {
            g_set.port = p;
            settings_save();
            net_restart_listener();
            toast(L"端口已更新");
        } else {
            toast(L"端口需在 1024 - 65535 之间");
        }
    } else if (g_edit == 3) {
        std::wstring v = g_editText;
        while (!v.empty() && v.front() == L' ') v.erase(v.begin());
        while (!v.empty() && v.back() == L' ') v.pop_back();
        g_set.wanPeerAddr = v;
        settings_save();
    } else if (g_edit == 4) {
        std::wstring v = g_editText;
        while (!v.empty() && v.front() == L' ') v.erase(v.begin());
        while (!v.empty() && v.back() == L' ') v.pop_back();
        g_set.wanPeerCode = v;
        settings_save();
    }
    g_edit = 0;
    g_editText.clear();
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ui_mouse_up(int x, int y) {
    Hit* h = hit_at(x, y);
    int id = h ? h->id : -1, arg = h ? h->arg : -1;
    if (id != g_pressId || arg != g_pressArg) { g_pressId = -1; return; }
    g_pressId = -1;

    if (g_edit && id != H_SET_NAME && id != H_SET_PORT && id != H_WAN_ADDR && id != H_WAN_CODE) commit_edit();

    // any click outside the context menu dismisses it
    if (g_ctxPeer >= 0 && id != H_CTX_DELETE) {
        g_ctxPeer = -1;
        if (id < 0) { InvalidateRect(g_hwnd, nullptr, FALSE); return; }
    }

    switch (id) {
    case H_MIN: ShowWindow(g_hwnd, SW_MINIMIZE); break;
    case H_MAX: ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
    case H_CLOSE: PostMessage(g_hwnd, WM_CLOSE, 0, 0); break;
    case H_NAV_SEND: g_page = 0; if (g_page == 1) g_hist = history_load_lines(); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case H_NAV_FILES: {
        if (net_session_id().empty()) { toast(L"请先连接一台设备，再浏览它的文件"); break; }
        // open the browser for the selected peer (or the only one there is)
        Peer p; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& q : g_peers) if (q.id == g_selPeer) { p = q; found = true; break; }
            if (!found && !g_peers.empty()) { p = g_peers[0]; found = true; g_selPeer = p.id; }
        }
        g_page = 3;
        g_scroll[3] = 0;
        if (found) {
            std::string cur;
            { std::lock_guard<std::mutex> lk(g_brMu); cur = g_brPeerId; }
            if (cur != p.id) br_open_peer(p);
        } else {
            std::lock_guard<std::mutex> lk(g_brMu);
            g_brPeerId.clear(); g_brEntries.clear(); g_brState = 0; g_brPeerName.clear();
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case H_OPEN_FILES: {
        Peer p; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& q : g_peers) if (q.id == g_selPeer) { p = q; found = true; break; }
            if (!found && !g_peers.empty()) { p = g_peers[0]; found = true; g_selPeer = p.id; }
        }
        if (!found) { toast(L"请先选择一台在线设备"); break; }
        g_page = 3;
        g_scroll[3] = 0;
        br_open_peer(p);
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case H_FS_UP: {
        std::string cur;
        { std::lock_guard<std::mutex> lk(g_brMu); cur = g_brPath; }
        std::string parent = path_parent(cur);
        if (parent != cur) { g_scroll[3] = 0; br_load(parent); }
        break;
    }
    case H_FS_REFRESH: {
        std::string cur;
        { std::lock_guard<std::mutex> lk(g_brMu); cur = g_brPath; }
        br_load(cur);
        break;
    }
    case H_FS_ENTRY: {
        RemoteEntry e; bool isDir = false;
        {
            std::lock_guard<std::mutex> lk(g_brMu);
            if (arg >= 0 && arg < (int)g_brEntries.size()) { e = g_brEntries[arg]; isDir = e.isDir; g_brSel = arg; }
        }
        if (!isDir) { InvalidateRect(g_hwnd, nullptr, FALSE); break; }
        g_scroll[3] = 0;
        br_load(e.path);
        break;
    }
    case H_FS_DOWNLOAD:
        br_download(arg);
        break;
    case H_NAV_HIST: g_page = 1; g_hist = history_load_lines(); InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case H_NAV_SET: g_page = 2; InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case H_COPY: {
        std::wstring s;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& a : g_local) { s = utf8_to_w(a.ip); break; }
        }
        if (s.empty()) { toast(L"没有可用的网络地址"); break; }
        s += L":" + std::to_wstring(g_set.port);
        if (OpenClipboard(g_hwnd)) {
            EmptyClipboard();
            size_t bytes = (s.size() + 1) * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hg) {
                memcpy(GlobalLock(hg), s.c_str(), bytes);
                GlobalUnlock(hg);
                SetClipboardData(CF_UNICODETEXT, hg);
            }
            CloseClipboard();
        }
        toast(L"已复制：" + s);
        break;
    }
    case H_REFRESH:
        net_refresh_local();
        toast(L"正在重新搜索设备");
        break;
    case H_DROP: {
        Peer target; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& p : g_peers) if (p.id == g_selPeer) { target = p; found = true; break; }
        }
        if (!found) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (!g_peers.empty()) { target = g_peers[0]; found = true; g_selPeer = target.id; }
        }
        if (!found) { toast(L"请先选择一台在线设备"); break; }
        pick_and_send(target);
        break;
    }
    case H_PEER:
        if (arg >= 0) {
            std::lock_guard<std::mutex> lk(g_mu);
            if (arg < (int)g_peers.size()) g_selPeer = g_peers[arg].id;
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_CTX_DELETE: {
        std::string ip; int port = g_set.port; std::wstring nm;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (arg >= 0 && arg < (int)g_peers.size()) {
                Peer p = g_peers[arg];
                ip = p.ip; port = p.port; nm = utf8_to_w(p.name);
                if (g_selPeer == p.id) g_selPeer.clear();
                g_peers.erase(g_peers.begin() + arg);
            }
        }
        g_ctxPeer = -1;
        if (!ip.empty()) {
            // hang up first, then drop the row
            std::thread([ip, port] { net_say_bye(ip, port); }).detach();
            toast(L"已删除设备记录：" + nm);
        }
        break;
    }
    case H_PEER_SEND:
        if (arg >= 0) {
            Peer p;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (arg >= (int)g_peers.size()) break;
                p = g_peers[arg];
                g_selPeer = p.id;
            }
            pick_and_send(p);
        }
        break;
    case H_TX_ACTION: {
        int txId = arg / 2, which = arg % 2;
        if (which == 0) {
            std::shared_ptr<Gate> gate;
            bool isActive = false;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& t : g_tx) if (t.id == txId) {
                    isActive = (t.state == TS_Active);
                    if (isActive) { if (t.cancel) t.cancel->store(true); }
                    else isActive = false;
                    break;
                }
            }
            if (isActive) { post_refresh(); }
            else {
                // open folder for completed
                open_path(g_set.saveDir);
            }
        } else {
            std::shared_ptr<Gate> gate;
            int decision = (which == 1) ? 1 : -1;
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& t : g_tx) if (t.id == txId) { gate = t.gate; break; }
            if (gate) gate->set(decision);
        }
        break;
    }
    case H_HIST_OPEN: open_path(g_set.saveDir); break;
    case H_HIST_CLEAR:
        DeleteFileW(history_path().c_str());
        g_hist.clear();
        toast(L"记录已清空");
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_WAN_ADDR:
        g_edit = 3; g_editText = g_set.wanPeerAddr; g_caret = (int)g_editText.size();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_WAN_CODE:
        g_edit = 4; g_editText = g_set.wanPeerCode; g_caret = (int)g_editText.size();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_WAN_CONNECT:
        wan_connect_clicked();
        break;
    case H_SET_NAME:
        g_edit = 1; g_editText = g_set.deviceName; g_caret = (int)g_editText.size();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_SET_PORT:
        g_edit = 2; g_editText = std::to_wstring(g_set.port); g_caret = (int)g_editText.size();
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    case H_SET_BROWSE: {
        BROWSEINFOW bi{};
        bi.hwndOwner = g_hwnd;
        bi.lpszTitle = L"选择接收文件夹";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
        if (pidl) {
            wchar_t path[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, path)) {
                g_set.saveDir = path;
                settings_save();
                toast(L"接收文件夹已更新");
            }
            CoTaskMemFree(pidl);
        }
        break;
    }
    case H_SET_OPEN_DIR: open_path(g_set.saveDir); break;
    case H_SET_AUTO:
        g_set.autoAccept = !g_set.autoAccept;
        settings_save();
        toast(g_set.autoAccept ? L"已开启自动接收" : L"已关闭自动接收");
        break;
    case H_SET_ALLOW_BROWSE:
        g_set.allowBrowse = !g_set.allowBrowse;
        settings_save();
        // the flag is checked on every request, so this takes effect immediately
        toast(g_set.allowBrowse ? L"其它设备现在可以浏览本机文件" : L"其它设备已无法浏览本机文件");
        break;
    case H_SET_KEEP_AWAKE:
        g_set.keepAwake = !g_set.keepAwake;
        settings_save();
        apply_keep_awake();
        toast(g_set.keepAwake ? L"已保持唤醒，电脑不会自动睡眠" : L"已恢复系统的睡眠策略");
        break;
    case H_SET_ABOUTOpen:
        g_page = 4;
        g_scroll[2] = 0;
        break;
    case H_NAV_WAN: g_page = 7; InvalidateRect(g_hwnd, nullptr, FALSE); break;
    case H_WAN_TOGGLE: {
        bool mapped = false, busy = false; int ep = 0, ip2 = 0;
        std::string e1, e2, e3;
        wan_status(mapped, busy, ep, ip2, e1, e2, e3);
        if (busy) break;
        if (mapped) { wan_close_async(); toast(L"已关闭端口映射"); }
        else { wan_open_async(g_set.port); toast(L"正在请求路由器开启端口…"); }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        break;
    }
    case H_WAN_COPY: {
        // Copy exactly what the page shows: the bracketed IPv6 when there is one,
        // otherwise the UPnP mapping.
        std::string v6;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& a : g_local) {
                if (a.ip.find(':') != std::string::npos) { v6 = a.ip; break; }
            }
        }
        std::wstring addr;
        if (!v6.empty()) {
            addr = L"[" + utf8_to_w(v6) + L"]:" + std::to_wstring(g_set.port);
        } else {
            bool mapped = false, busy = false; int ep = 0, ip2 = 0;
            std::string e1, pub2, e3;
            wan_status(mapped, busy, ep, ip2, e1, pub2, e3);
            addr = utf8_to_w(pub2.empty() ? e1 : pub2) + L":" + std::to_wstring(ep);
        }
        if (OpenClipboard(g_hwnd)) {
            EmptyClipboard();
            size_t bytes = (addr.size() + 1) * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (h) {
                void* dst = GlobalLock(h);
                if (dst) { memcpy(dst, addr.c_str(), bytes); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
            }
            CloseClipboard();
        }
        toast(L"已复制公网地址");
        break;
    }
    case H_SET_DEVICES:
        g_page = 5;
        g_scroll[5] = 0;
        break;
    case H_DEV_BACK:
        g_page = (g_page == 6) ? 5 : 2;
        break;
    case H_DEV_DELETE + 100: {
        // row click: open that device's timestamp list
        std::vector<DevGroup> gs = load_device_groups();
        if (arg >= 0 && arg < (int)gs.size()) {
            g_devSel = gs[arg].ip;
            g_page = 6;
            g_scroll[6] = 0;
        }
        break;
    }
    case H_DEV_DELETE: {
        // member of the list: drop every timestamp for that device
        std::vector<DevGroup> gs = load_device_groups();
        if (arg >= 0 && arg < (int)gs.size()) {
            device_log_remove(gs[arg].name, gs[arg].ip);
            toast(L"已删除设备记录：" + gs[arg].name);
        }
        break;
    }
    case H_DEV_DELETE_ALL: {
        std::vector<DevGroup> gs = load_device_groups();
        for (auto& d : gs) if (d.ip == g_devSel || d.name == g_devSel) {
            device_log_remove(d.name, d.ip);
            toast(L"已删除设备记录：" + d.name);
            break;
        }
        g_page = 5;
        break;
    }
    case H_ABOUT_BACK:
        g_page = 2;
        break;
    case H_PEER_CONNECT: {
        Peer p; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (arg >= 0 && arg < (int)g_peers.size()) { p = g_peers[arg]; found = true; }
        }
        if (!found) break;
        std::string myId = net_self_id();
        std::wstring myName = g_set.deviceName;
        int myPort = g_set.port;
        toast(L"正在连接 " + utf8_to_w(p.name) + L" …");
        std::thread([p, myId, myName, myPort] {
            bool ok = net_session_connect(p.ip, p.port, myId, w_to_utf8(myName), myPort);
            if (!ok) { toast(L"连接失败，对方可能已离线"); return; }
            net_session_set(p.id, p.ip, p.port);
            ui_set_selected_peer(p.id);
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& q : g_peers) if (q.id == p.id) q.linkDown = false;
            }
            // both sides end up with the same record list
            std::vector<HistRec> mine = history_records();
            net_hist_push(p.ip, p.port, mine);
            std::vector<HistRec> theirs = net_hist_pull(p.ip, p.port);
            for (auto& r : theirs) history_upsert(r);
            ui_reload_history();
            toast(L"已连接 " + utf8_to_w(p.name));
            post_refresh();
        }).detach();
        break;
    }
    case H_PEER_DISCONNECT: {
        Peer p; bool found = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (arg >= 0 && arg < (int)g_peers.size()) {
                p = g_peers[arg];
                g_peers[arg].linkDown = true;
                found = true;
            }
            if (g_selPeer == p.id) g_selPeer.clear();
        }
        if (found) {
            // clear the session locally first: browse and send stop immediately,
            // then tell the other side so its list flips too
            net_session_clear();
            std::string ip = p.ip;
            int port = p.port;
            std::thread([ip, port] { net_say_bye(ip, port); }).detach();
            toast(L"已断开与 " + utf8_to_w(p.name) + L" 的连接");
        }
        break;
    }
    case H_HIST_DEL: {
        std::wstring line;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (arg < 0 || arg >= (int)g_hist.size()) break;
            line = g_hist[arg];
        }
        HistRec rec;
        if (!hist_parse_line(line, rec)) break;
        const bool outbound = rec.dir == "out";
        const std::wstring realName = utf8_to_w(rec.realName);
        const std::string peerIp = rec.peerIp;
        const int peerPort = rec.port > 0 ? rec.port : g_set.port;
        const std::string recId = rec.id;

        // delete the real file, then drop the record; both on a worker thread
        std::thread([line, outbound, realName, peerIp, peerPort, recId] {
            bool ok = true;
            std::wstring err;
            if (!realName.empty()) {
                if (outbound) {
                    // the delivered copy sits on the peer
                    ok = net_fs_delete(peerIp, peerPort, w_to_utf8(realName), err);
                } else {
                    std::wstring local = g_set.saveDir + L"\\" + realName;
                    if (GetFileAttributesW(local.c_str()) != INVALID_FILE_ATTRIBUTES)
                        ok = DeleteFileW(local.c_str()) != 0;
                }
            }
            if (ok) {
                history_remove(line);
                std::string sid = net_session_id();
                if (!sid.empty() && !recId.empty() && !peerIp.empty())
                    net_hist_del(peerIp, peerPort, recId);
                // reload so the row disappears immediately instead of after a
                // page switch (this is why the record looked "not deleted")
                {
                    std::lock_guard<std::mutex> lk(g_mu);
                    g_hist = history_load_lines();
                }
                toast(L"已删除记录和对应的文件");
            } else {
                toast(L"删除失败：" + (err.empty() ? L"文件可能已被移动" : err));
            }
            post_refresh();
        }).detach();
        break;
    }
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ui_key(int vk, bool down, bool ctrl) {
    if (!down || !g_edit) return;
    if (vk == VK_RETURN) { commit_edit(); return; }
    if (vk == VK_ESCAPE) { g_edit = 0; g_editText.clear(); InvalidateRect(g_hwnd, nullptr, FALSE); return; }
    if (vk == VK_BACK) {
        if (g_caret > 0 && !g_editText.empty()) {
            g_editText.erase(g_editText.begin() + (g_caret - 1));
            g_caret--;
        }
    } else if (vk == VK_DELETE) {
        if (g_caret < (int)g_editText.size()) g_editText.erase(g_editText.begin() + g_caret);
    } else if (vk == VK_LEFT) {
        if (g_caret > 0) g_caret--;
    } else if (vk == VK_RIGHT) {
        if (g_caret < (int)g_editText.size()) g_caret++;
    } else if (vk == VK_HOME) {
        g_caret = 0;
    } else if (vk == VK_END) {
        g_caret = (int)g_editText.size();
    } else if (vk == 'V' && ctrl) {
        if (OpenClipboard(g_hwnd)) {
            HANDLE h = GetClipboardData(CF_UNICODETEXT);
            if (h) {
                auto* p = (wchar_t*)GlobalLock(h);
                if (p) {
                    std::wstring s = p;
                    GlobalUnlock(h);
                    if (g_edit == 2) {
                        for (auto& c : s) if (c < L'0' || c > L'9') c = L'0';
                    }
                    g_editText.insert(g_caret, s);
                    g_caret += (int)s.size();
                }
            }
            CloseClipboard();
        }
    } else if (vk == 'A' && ctrl) {
        g_caret = (int)g_editText.size();
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ui_char(wchar_t c) {
    if (!g_edit) return;
    if ((g_edit == 2 || g_edit == 4) && (c < L'0' || c > L'9')) return;
    if (c < 32) return;
    if (g_editText.size() > 60) return;
    g_editText.insert(g_editText.begin() + g_caret, c);
    g_caret++;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ui_reset_fonts() { reset_fonts(); }

// ------------------------------------------------------------------ headless render (dev aid)
static int encoder_clsid(const WCHAR* mime, CLSID* clsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* info = (ImageCodecInfo*)malloc(size);
    if (!info) return -1;
    GetImageEncoders(num, size, info);
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, mime) == 0) { *clsid = info[i].Clsid; free(info); return (int)i; }
    }
    free(info);
    return -1;
}

// Dev hook: jump straight into the files page at a known path.
void ui_open_browse(const std::string& wirePath) {
    Peer p; bool found = false;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_peers.empty()) { p = g_peers[0]; found = true; g_selPeer = p.id; }
    }
    g_page = 3;
    g_scroll[3] = 0;
    if (!found) return;
    {
        std::lock_guard<std::mutex> lk(g_brMu);
        g_brPeerId = p.id;
        g_brIp = p.ip;
        g_brPort = p.port;
        g_brPeerName = utf8_to_w(p.name);
    }
    br_load(wirePath);
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void ui_demo_state(int page) {
    if (page == 9) { g_page = 0; g_ctxPeer = 0; g_ctxX = 620; g_ctxY = 420; page = 0; }
    g_page = page;
    std::lock_guard<std::mutex> lk(g_mu);
    g_local.clear();
    LocalAddr a; a.ip = "192.168.1.7"; a.usb = false; a.primary = true; a.adapter = "Intel Wi-Fi 6"; g_local.push_back(a);
    LocalAddr b; b.ip = "192.168.42.100"; b.usb = true; b.adapter = "Remote NDIS"; g_local.push_back(b);
    LocalAddr c6; c6.ip = "240e:878:55:7142:4fcf:f038:b68:1b37"; c6.primary = true;
    c6.adapter = "Intel Wi-Fi 6"; g_local.push_back(c6);

    g_peers.clear();
    Peer p1; p1.id = "p1"; p1.name = "Pixel 8 Pro"; p1.ip = "192.168.1.23"; p1.port = 45678; p1.usb = false;
    Peer p2; p2.id = "p2"; p2.name = "Xiaomi 14"; p2.ip = "192.168.42.129"; p2.port = 45678; p2.usb = true;
    g_peers.push_back(p1); g_peers.push_back(p2);
    g_selPeer = "p1";

    g_tx.clear();
    Transfer t1;
    t1.id = 1; t1.dir = TD_Out; t1.state = TS_Active;
    t1.peerName = L"Pixel 8 Pro"; t1.peerAddr = L"192.168.1.23";
    FileEntry f1; f1.name = L"设计稿-最终版.psd"; f1.size = 48ull * 1024 * 1024; f1.done = 31ull * 1024 * 1024;
    t1.files.push_back(f1); t1.total = f1.size; t1.done = f1.done; t1.speed = 8.4 * 1024 * 1024;
    g_tx.push_back(t1);

    Transfer t2;
    t2.id = 2; t2.dir = TD_In; t2.state = TS_Done;
    t2.peerName = L"Xiaomi 14"; t2.peerAddr = L"192.168.42.129";
    FileEntry f2; f2.name = L"IMG_20241029_141233.jpg"; f2.size = 3ull * 1024 * 1024; f2.done = f2.size;
    t2.files.push_back(f2); t2.total = f2.size; t2.done = f2.size;
    g_tx.push_back(t2);

    g_hist.clear();
    g_hist.push_back(L"设计稿-最终版.psd\t48.0 MB\tout\t传输中\t10:42");
    g_hist.push_back(L"IMG_20241029_141233.jpg\t3.0 MB\tin\t完成\t10:31");
    g_hist.push_back(L"会议纪要.docx\t184 KB\tout\t完成\t昨天");
    g_hist.push_back(L"录屏 2024-10-27.mp4\t326 MB\tin\t完成\t昨天");
    g_hist.push_back(L"安装包.apk\t28.4 MB\tout\t已取消\t10-26");
    g_hist.push_back(L"截图 2024-10-25 183012.png\t1.2 MB\tin\t完成\t10-25");

    // device-files page preview
    {
        std::lock_guard<std::mutex> lk2(g_brMu);
        g_brPeerId = "p1";
        g_brPeerName = L"Pixel 8 Pro";
        g_brPath = "/storage/emulated/0/DCIM/Camera";
        g_brState = 3;
        g_brErr.clear();
        g_brSel = -1;
        g_brEntries.clear();
        auto add = [&](const wchar_t* n, bool d, uint64_t sz, const wchar_t* p) {
            RemoteEntry e; e.name = n; e.isDir = d; e.size = sz; e.path = w_to_utf8(p);
            e.mtime = 0; g_brEntries.push_back(e);
        };
        add(L"Screenshots",           true,  0,       L"/storage/emulated/0/DCIM/Camera/Screenshots");
        add(L"IMG_20241029_141233.jpg", false, 3145728, L"/storage/emulated/0/DCIM/Camera/IMG_20241029_141233.jpg");
        add(L"IMG_20241029_141301.jpg", false, 2814976, L"/storage/emulated/0/DCIM/Camera/IMG_20241029_141301.jpg");
        add(L"VID_20241028_203344.mp4", false, 268435456, L"/storage/emulated/0/DCIM/Camera/VID_20241028_203344.mp4");
        add(L"IMG_20241026_090512.jpg", false, 3407872, L"/storage/emulated/0/DCIM/Camera/IMG_20241026_090512.jpg");
    }
}

void ui_dump_hits(const std::wstring& path) {
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    fprintf(f, "count=%d\n", (int)g_hits.size());
    for (auto& h : g_hits)
        fprintf(f, "id=%-4d arg=%-4d  x=%7.1f y=%7.1f w=%7.1f h=%6.1f\n", h.id, h.arg, h.x, h.y, h.w, h.h);
    fclose(f);
}

void ui_shot(const std::wstring& path, float scale) {
    ensure_gdiplus();
    g_s = scale;
    reset_fonts();

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) { W = 1060; H = 748; }

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);

    ui_paint(g_hwnd, mem);

    {
        Gdiplus::Bitmap out(bmp, nullptr);
        CLSID clsid;
        if (encoder_clsid(L"image/png", &clsid) >= 0)
            out.Save(path.c_str(), &clsid, nullptr);
    }

    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

// Dev hook: write a QR symbol for `text` as a 1:1 PNG, then quit.
void ui_shot_qr(const std::string& text, const std::wstring& path, int pixelScale) {
    ensure_gdiplus();
    int n = 0;
    std::vector<unsigned char> mods = qr_encode(text, &n);
    if (n <= 0) return;

    const int quiet = 4;                       // quiet zone in modules
    int dim = (n + quiet * 2) * pixelScale;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = dim;
    bi.bmiHeader.biHeight = -dim;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeNone);
        SolidBrush white(Color(255, 255, 255, 255));
        g.FillRectangle(&white, 0, 0, dim, dim);
        SolidBrush dark(Color(255, 0, 0, 0));
        for (int row = 0; row < n; row++) {
            for (int col = 0; col < n; col++) {
                if (!mods[row * n + col]) continue;
                g.FillRectangle(&dark,
                                (col + quiet) * pixelScale, (row + quiet) * pixelScale,
                                pixelScale, pixelScale);
            }
        }
    }
    {
        Gdiplus::Bitmap out(bmp, nullptr);
        CLSID clsid;
        if (encoder_clsid(L"image/png", &clsid) >= 0) out.Save(path.c_str(), &clsid, nullptr);
    }
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void ui_dpi_changed(UINT dpi) {    g_s = dpi / 96.0f;
    reset_fonts();
    g_anim.clear();
    if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE);
}

bool ui_want_anim() {
    RECT r{};
    return ui_anim_tick(&r) != 0;
}

// Decide what the animation timer should repaint.
// 0 = nothing, 1 = whole window, 2 = only the rect in *rl.
int ui_anim_tick(RECT* rl) {
    if (g_animating || g_edit) return 1;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& t : g_tx) if (t.state == TS_Active || t.state == TS_Waiting) return 1;
        if (!g_toasts.empty()) return 1;
    }
    // Idle "searching" pulse: repaint only its own small rect, at ~15 fps.
    // This keeps an always-on background window from burning CPU.
    if (g_searchPulse && g_searchRect.Width > 0) {
        ULONGLONG now = GetTickCount64();
        if (now - g_lastFrame < 66) return 0;
        g_lastFrame = now;
        if (rl) {
            rl->left = (LONG)(g_searchRect.X - 2);
            rl->top = (LONG)(g_searchRect.Y - 2);
            rl->right = (LONG)(g_searchRect.GetRight() + 2);
            rl->bottom = (LONG)(g_searchRect.GetBottom() + 2);
        }
        return 2;
    }
    return 0;
}
