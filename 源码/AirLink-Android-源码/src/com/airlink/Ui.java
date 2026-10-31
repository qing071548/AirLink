package com.airlink;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.text.TextPaint;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.List;

/**
 * The whole UI is one custom view, drawn by hand.
 * Light theme, card based, with a settings stack whose rows open sub pages.
 */
public class Ui extends View {

    public static final int PAGE_SEND = 0, PAGE_FILES = 1, PAGE_HISTORY = 2, PAGE_SET = 3;
    public static final int PAGE_ABOUT = 4, PAGE_SAVEDIR = 5, PAGE_DEVICES = 6, PAGE_DEVICE_DETAIL = 7,
            PAGE_WAN = 9;

    private int page = PAGE_SEND;

    // ---- data handed in by the activity
    public static final class Row {
        public String title, sub, right;
        public int accent = Skin.TEXT;
        public boolean dir, selected, off;
        public boolean lastUsed;       // previously connected device: offer "继续连接"
        public int badge;              // 0 none, 1 ok, 2 warn, 3 err
        public Object tag;
    }

    private final List<Row> peers = new ArrayList<>();
    private final List<Row> files = new ArrayList<>();
    private final List<Row> history = new ArrayList<>();
    private final List<Row> saveDirs = new ArrayList<>();
    private final List<Row> devices = new ArrayList<>();
    private final List<Row> deviceStamps = new ArrayList<>();
    private String deviceKey = "", deviceName = "", deviceIp = "";

    private String selfName = "", selfAddr = "";
    private String filePath = "/", filePeer = "", fileErr = "";
    private int fileState = 0;
    private String statusLine = "", hint = "";
    private float progress = -1;
    private String progressLabel = "";
    private String aboutText = "";
    private Bitmap thumb;
    private String detailName = "", detailSub = "", detailPath = "";
    private int detailBadge = 0;

    // ---- geometry
    private float d, W, H, statusInset;
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final TextPaint tp = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();
    private final Path path = new Path();
    private Canvas canvas;

    private float scrollY = 0, maxScroll = 0, downY = 0, downScroll = 0;
    private boolean dragging = false;
    private final float[] tapY = new float[1];

    private OnAction cb;

    public interface OnAction {
        void onNav(int page);
        void onPeer(int i);
        void onPeerDisconnect(int i);
        void onScanQr();
        void onManualIp();
        void onPickFiles();
        void onFileEnter(int i);
        void onFileUp();
        void onFileRefresh();
        void onFileCopy(int i);
        void onHistoryDelete(int i);
        void onHistoryOpen(int i);
        void onEditName();
        void onToggleAllowBrowse();
        void onRequestStoragePerm();
        void onOpenSaveDirSettings();
        void onSaveDirChosen(int i);
        void onSaveDirManual();
        void onKeepAwake();
        void onOpenAbout();
        void onOpenDevices();
        void onBatterySetting();
        void onWanAddr();
        void onWanCode();
        void onWanConnect();
        void onMirrorTap(int nx, int ny);
        void onDeviceOpen(int i);
        void onDeviceDelete(int i);
        void onDeviceDeleteAll();
        void onBack();
    }

    public Ui(Context c) {
        super(c);
        d = getResources().getDisplayMetrics().density;
        setBackgroundColor(Skin.BG);
    }

    public void setCallback(OnAction a) { cb = a; }

    // ------------------------------------------------------------------ setters
    public void setSelf(String name, String addr) { selfName = name; selfAddr = addr; invalidate(); }
    public void setPeers(List<Row> r) { peers.clear(); peers.addAll(r); invalidate(); }
    public void setFiles(List<Row> r) { files.clear(); files.addAll(r); invalidate(); }
    public void setSaveDirs(List<Row> r) { saveDirs.clear(); saveDirs.addAll(r); invalidate(); }
    public void setDevices(List<Row> r) { devices.clear(); devices.addAll(r); invalidate(); }
    public void setDeviceStamps(List<Row> r) { deviceStamps.clear(); deviceStamps.addAll(r); invalidate(); }
    public void setDeviceKey(String name, String ip) {
        deviceName = name == null ? "" : name;
        deviceIp = ip == null ? "" : ip;
        deviceKey = deviceName + "\n" + deviceIp;
        invalidate();
    }

    public String deviceKey() { return deviceKey; }
    public void setFileState(int s, String path, String peer, String err) {
        fileState = s;
        filePath = path == null ? "/" : path;
        filePeer = peer == null ? "" : peer;
        fileErr = err == null ? "" : err;
        invalidate();
    }
    public void setHistory(List<Row> r) { history.clear(); history.addAll(r); invalidate(); }
    public void setStatus(String s) { statusLine = s == null ? "" : s; hint = statusLine; invalidate(); }
    public void setHint(String s) { hint = s == null ? "" : s; invalidate(); }
    public void setProgress(String label, float f) { progressLabel = label == null ? "" : label; progress = f; invalidate(); }
    public void setAbout(String s) { aboutText = s == null ? "" : s; invalidate(); }
    public void setDetail(String name, String sub, String path, int badge, Bitmap bmp) {
        detailName = name == null ? "" : name;
        detailSub = sub == null ? "" : sub;
        detailPath = path == null ? "" : path;
        detailBadge = badge;
        thumb = bmp;
        invalidate();
    }
    public void setStatusInset(float px) { statusInset = px; invalidate(); }

    public int page() { return page; }
    public String selfAddr() { return selfAddr; }
    public void goPage(int pg) { page = pg; scrollY = 0; invalidate(); if (cb != null) cb.onNav(pg); }

    // ------------------------------------------------------------------ helpers
    private float dp(float v) { return v * d; }

    private void text(String s, float x, float y, float size, int color, Typeface tf) {
        tp.setTextSize(dp(size));
        tp.setColor(color);
        tp.setTypeface(tf);
        if (canvas != null) canvas.drawText(s, x, y, tp);
    }

    private void textRight(String s, float right, float y, float size, int color, Typeface tf) {
        tp.setTextSize(dp(size));
        tp.setColor(color);
        tp.setTypeface(tf);
        if (canvas != null) canvas.drawText(s, right - tp.measureText(s), y, tp);
    }

    private void textCenter(String s, float cx, float y, float size, int color, Typeface tf) {
        tp.setTextSize(dp(size));
        tp.setColor(color);
        tp.setTypeface(tf);
        if (canvas != null) canvas.drawText(s, cx - tp.measureText(s) / 2f, y, tp);
    }

    private float width(String s, float size, Typeface tf) {
        tp.setTextSize(dp(size));
        tp.setTypeface(tf);
        return tp.measureText(s);
    }

    private String ellipsize(String s, float maxW, float size) {
        if (s == null) return "";
        tp.setTextSize(dp(size));
        if (tp.measureText(s) <= maxW) return s;
        int lo = 0, hi = s.length();
        while (lo < hi) {
            int mid = (lo + hi + 1) / 2;
            if (tp.measureText(s.substring(0, mid) + "…") <= maxW) lo = mid; else hi = mid - 1;
        }
        return s.substring(0, Math.max(0, lo)) + "…";
    }

    private void round(float l, float t, float r, float b, float rad, int color) {
        rect.set(l, t, r, b);
        p.setStyle(Paint.Style.FILL);
        p.setColor(color);
        canvas.drawRoundRect(rect, dp(rad), dp(rad), p);
    }

    private void border(float l, float t, float r, float b, float rad, int color, float w) {
        rect.set(l, t, r, b);
        p.setStyle(Paint.Style.STROKE);
        p.setColor(color);
        p.setStrokeWidth(dp(w));
        canvas.drawRoundRect(rect, dp(rad), dp(rad), p);
        p.setStyle(Paint.Style.FILL);
    }

    /** White card with a hairline edge and a soft shadow. */
    private void card(float l, float t, float r, float b, float rad) {
        p.setColor(0x14000000);
        rect.set(l, t + dp(1.5f), r, b + dp(1.5f));
        canvas.drawRoundRect(rect, dp(rad), dp(rad), p);
        round(l, t, r, b, rad, Skin.CARD);
        border(l, t, r, b, rad, Skin.LINE, 1f);
    }

    private int pos = 0;   // running y while laying out a page

    private float sectionTitle(String s) {
        text(s, dp(20), pos + dp(14), 12.5f, Skin.TEXT3, Skin.MED);
        pos += dp(24);
        return pos;
    }

    // ------------------------------------------------------------------ draw
    @Override
    protected void onDraw(Canvas c) {
        canvas = c;
        W = getWidth();
        H = getHeight();
        if (mirrorPending != null) { mirrorBmp = mirrorPending; mirrorPending = null; mirrorHasFrame = true; }
        c.drawColor(Skin.BG);


        float navTop = H - dp(60);
        float headerBottom = statusInset + dp(56);

        drawHeader(navTop, headerBottom);

        c.save();
        c.clipRect(0, headerBottom, W, navTop);
        // Pages that scroll subtract scrollY themselves; the file browser keeps
        // its path bar fixed, so it must not inherit an offset here.
        pos = (int) (headerBottom + dp(10));
        maxScroll = 0;
        if (page == PAGE_SEND) drawSend();
        else if (page == PAGE_FILES) drawFiles(headerBottom, navTop);
        else if (page == PAGE_HISTORY) drawHistory(navTop);
        else if (page == PAGE_SET) drawSettings();
        else if (page == PAGE_ABOUT) drawAbout();
        else if (page == PAGE_SAVEDIR) drawSaveDir();
        else if (page == PAGE_DEVICES) drawDevices();
        else if (page == PAGE_DEVICE_DETAIL) drawDeviceDetail();
        else if (page == PAGE_WAN) drawWan();

        c.restore();

        if (progress >= 0) drawProgress(navTop);
        drawNav(navTop);
    }

    private void drawHeader(float navTop, float headerBottom) {
        p.setColor(Skin.CARD);
        canvas.drawRect(0, 0, W, headerBottom, p);
        p.setColor(Skin.LINE);
        canvas.drawRect(0, headerBottom - dp(1), W, headerBottom, p);

        float base = statusInset + dp(34);
        if (page == PAGE_ABOUT || page == PAGE_SAVEDIR || page == PAGE_DEVICES
                || page == PAGE_DEVICE_DETAIL) {
            // back chevron
            p.setColor(Skin.ACCENT);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(2.1f));
            p.setStrokeCap(Paint.Cap.ROUND);
            float ax = dp(24), ay = statusInset + dp(28);
            canvas.drawLine(ax + dp(5), ay - dp(6), ax, ay, p);
            canvas.drawLine(ax, ay, ax + dp(5), ay + dp(6), p);
            p.setStrokeCap(Paint.Cap.BUTT);
            p.setStyle(Paint.Style.FILL);
            String t = page == PAGE_ABOUT ? "关于"
                    : (page == PAGE_SAVEDIR ? "接收目录"
                    : (page == PAGE_DEVICES ? "设备管理" : "设备详情"));
            text(t, dp(46), base, 19, Skin.TEXT, Skin.BOLD);
        } else {
            text("AirLink", dp(20), base, 20, Skin.TEXT, Skin.BOLD);
            if (!selfAddr.isEmpty()) {
                String a = selfAddr;
                float w = width(a, 11, Skin.MONO);
                round(W - dp(20) - w - dp(20), statusInset + dp(16), W - dp(20), statusInset + dp(38), 11, Skin.ACCENT_SOFT);
                textRight(a, W - dp(30), statusInset + dp(33), 11, Skin.ACCENT, Skin.MONO);
            }
        }
    }

    private void drawNav(float top) {
        p.setColor(Skin.CARD);
        canvas.drawRect(0, top, W, H, p);
        p.setColor(Skin.LINE);
        canvas.drawRect(0, top, W, top + dp(1), p);

        String[] labels = {"快传", "电脑文件", "WAN", "记录", "设置"};
        int[] pages = {PAGE_SEND, PAGE_FILES, PAGE_WAN, PAGE_HISTORY, PAGE_SET};
        float seg = W / 5f;
        int currentTab = (page == PAGE_ABOUT || page == PAGE_SAVEDIR || page == PAGE_DEVICES
                || page == PAGE_DEVICE_DETAIL) ? PAGE_SET : page;
        for (int i = 0; i < 5; i++) {
            float cx = seg * i + seg / 2f;
            boolean active = currentTab == pages[i];
            int col = active ? Skin.ACCENT : Skin.TEXT3;
            navIcon(i == 2 ? 4 : (i > 2 ? i - 1 : i), cx, top + dp(22), col);
            textCenter(labels[i], cx, top + dp(46), 10.5f, col, active ? Skin.MED : Skin.REG);
            if (active) {
                round(cx - dp(9), top + dp(52), cx + dp(9), top + dp(54.5f), 1.25f, Skin.ACCENT);
            }
        }
    }

    private void navIcon(int idx, float cx, float cy, int color) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(1.8f));
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeJoin(Paint.Join.ROUND);
        p.setColor(color);
        float r = dp(8.5f);
        if (idx == 0) {
            path.reset();
            path.moveTo(cx - r, cy + r * 0.3f);
            path.lineTo(cx + r, cy - r * 0.75f);
            path.lineTo(cx + r * 0.2f, cy + r * 0.9f);
            path.lineTo(cx - r * 0.15f, cy + r * 0.1f);
            path.close();
            canvas.drawPath(path, p);
        } else if (idx == 1) {
            rect.set(cx - r, cy - r * 0.7f, cx + r, cy + r * 0.42f);
            canvas.drawRoundRect(rect, dp(2.5f), dp(2.5f), p);
            canvas.drawLine(cx - r * 0.45f, cy + r * 0.85f, cx + r * 0.45f, cy + r * 0.85f, p);
            canvas.drawLine(cx, cy + r * 0.42f, cx, cy + r * 0.85f, p);
        } else if (idx == 2) {
            canvas.drawCircle(cx, cy, r, p);
            canvas.drawLine(cx, cy, cx, cy - r * 0.55f, p);
            canvas.drawLine(cx, cy, cx + r * 0.5f, cy + r * 0.22f, p);
        } else if (idx == 4) {
            // mirror: a screen with a play/shift arrow
            rect.set(cx - r, cy - r * 0.72f, cx + r, cy + r * 0.5f);
            canvas.drawRoundRect(rect, dp(2.5f), dp(2.5f), p);
            canvas.drawLine(cx, cy + r * 0.5f, cx, cy + r * 0.85f, p);
            canvas.drawLine(cx - r * 0.45f, cy + r * 0.85f, cx + r * 0.45f, cy + r * 0.85f, p);
            canvas.drawLine(cx - r * 0.35f, cy - r * 0.11f, cx + r * 0.3f, cy - r * 0.11f, p);
        } else {
            for (int i = -1; i <= 1; i++) {
                canvas.drawLine(cx - r, cy + i * dp(5), cx + r, cy + i * dp(5), p);
            }
            p.setStyle(Paint.Style.FILL);
            canvas.drawCircle(cx - r * 0.35f, cy - dp(5), dp(2.3f), p);
            canvas.drawCircle(cx + r * 0.35f, cy, dp(2.3f), p);
            canvas.drawCircle(cx - r * 0.2f, cy + dp(5), dp(2.3f), p);
            p.setStyle(Paint.Style.STROKE);
        }
        p.setStrokeCap(Paint.Cap.BUTT);
        p.setStyle(Paint.Style.FILL);
    }

    private void drawProgress(float navTop) {
        float h = dp(36), top = navTop - h - dp(8);
        card(dp(12), top, W - dp(12), top + h, 12);
        float w = (W - dp(24) - dp(16)) * Math.max(0, Math.min(1, progress));
        round(dp(20), top + h - dp(9), dp(20) + Math.max(dp(3), w), top + h - dp(5), 2, Skin.ACCENT);
        text(ellipsize(progressLabel, W - dp(60), 10.5f), dp(20), top + dp(16), 10.5f, Skin.TEXT2, Skin.REG);
    }

    // ------------------------------------------------------------------ send page
    private void drawSend() {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);

        // this device
        float h = dp(96);
        card(l, pos, r, pos + h, 16);
        round(l + dp(16), pos + dp(20), l + dp(60), pos + dp(64), 12, Skin.ACCENT_SOFT);
        textCenter("A", l + dp(38), pos + dp(50), 20, Skin.ACCENT, Skin.BOLD);
        text(ellipsize(selfName, W - dp(150), 15), l + dp(72), pos + dp(38), 15, Skin.TEXT, Skin.BOLD);
        text("本机 · Android", l + dp(72), pos + dp(57), 10.5f, Skin.TEXT3, Skin.REG);
        boolean online = !selfAddr.isEmpty();
        String addr = online ? selfAddr : "未连接网络";
        text(addr, l + dp(16), pos + dp(86), 11.5f, online ? Skin.ACCENT : Skin.RED, Skin.MONO);
        p.setColor(online ? Skin.GREEN : Skin.RED);
        canvas.drawCircle(r - dp(20), pos + dp(24), dp(4.5f), p);
        pos += h + dp(14);

        // primary actions
        float bw = (r - l - dp(10)) / 2f;
        primaryButton(l, pos, l + bw, pos + dp(50), "选择文件发送");
        ghostButton(l + bw + dp(10), pos, r, pos + dp(50), "扫二维码连接");
        pos += dp(50) + dp(12);

        // manual-IP hint, promoted to a card so it is easy to hit
        {
            float hh = dp(46);
            round(l, pos, r, pos + hh, 13, Skin.CARD);
            border(l, pos, r, pos + hh, 13, Skin.LINE, 1f);
            round(l + dp(13), pos + dp(11), l + dp(37), pos + dp(35), 8, Skin.ACCENT_SOFT);
            p.setColor(Skin.ACCENT);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1.7f));
            canvas.drawLine(l + dp(20), pos + dp(23), l + dp(30), pos + dp(23), p);
            canvas.drawLine(l + dp(25), pos + dp(18), l + dp(25), pos + dp(28), p);
            p.setStyle(Paint.Style.FILL);
            text("找不到设备？手动输入电脑 IP", l + dp(46), pos + dp(24), 12.2f, Skin.ACCENT, Skin.MED);
            text("在电脑的 AirLink 首页可以看到", l + dp(46), pos + dp(38), 9.8f, Skin.TEXT3, Skin.REG);
            p.setColor(Skin.TEXT3);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1.7f));
            canvas.drawLine(r - dp(26), pos + dp(20), r - dp(21), pos + dp(23), p);
            canvas.drawLine(r - dp(21), pos + dp(23), r - dp(26), pos + dp(26), p);
            p.setStyle(Paint.Style.FILL);
            pos += hh + dp(16);
        }

        // peers
        sectionTitle("附近设备");
        if (peers.isEmpty()) {
            float eh = dp(96);
            card(l, pos, r, pos + eh, 16);
            textCenter("正在搜索…", W / 2f, pos + dp(42), 13, Skin.TEXT2, Skin.MED);
            textCenter("让电脑打开 AirLink，连同一个 Wi-Fi", W / 2f, pos + dp(64), 10.5f, Skin.TEXT3, Skin.REG);
            pos += eh + dp(16);
        } else {
            for (int i = 0; i < peers.size(); i++) {
                Row row = peers.get(i);
                float rh = dp(70);
                card(l, pos, r, pos + rh, 14);
                if (row.selected) border(l, pos, r, pos + rh, 14, Skin.ACCENT, 1.5f);

                round(l + dp(14), pos + dp(17), l + dp(50), pos + dp(53), 11,
                        row.off ? Skin.CARD2 : Skin.ACCENT_SOFT);
                textCenter(row.title == null || row.title.isEmpty() ? "?" : row.title.substring(0, 1).toUpperCase(),
                        l + dp(32), pos + dp(41), 15, row.off ? Skin.TEXT3 : Skin.ACCENT, Skin.BOLD);

                float nameW = r - l - dp(160);
                text(ellipsize(row.title, nameW, 13.5f), l + dp(62), pos + dp(30), 13.5f,
                        row.off ? Skin.TEXT3 : Skin.TEXT, Skin.MED);
                text(row.sub, l + dp(62), pos + dp(49), 10.5f, Skin.TEXT3, Skin.MONO);

                if (row.selected) {
                    // connected: show the live state and the disconnect action
                    float cw = dp(64);
                    round(r - dp(14) - cw, pos + (rh - dp(30)) / 2f, r - dp(14), pos + (rh + dp(30)) / 2f, 15, Skin.GREEN_SOFT);
                    textCenter("已连接", r - dp(14) - cw / 2f, pos + rh / 2f + dp(4), 10.8f, Skin.GREEN, Skin.MED);
                    float dw = dp(54);
                    round(r - dp(14) - cw - dp(8) - dw, pos + (rh - dp(30)) / 2f,
                            r - dp(14) - cw - dp(8), pos + (rh + dp(30)) / 2f, 10, Skin.RED_SOFT);
                    textCenter("断开", r - dp(14) - cw - dp(8) - dw / 2f, pos + rh / 2f + dp(4), 10.8f, Skin.RED, Skin.MED);
                } else {
                    // not connected: one button only, and it is "连接"
                    String label = row.lastUsed ? "继续连接" : "连接";
                    float cw = dp(row.lastUsed ? 78 : 62);
                    round(r - dp(14) - cw, pos + (rh - dp(30)) / 2f, r - dp(14), pos + (rh + dp(30)) / 2f, 10,
                            row.lastUsed ? Skin.ORANGE_SOFT : Skin.ACCENT_SOFT);
                    textCenter(label, r - dp(14) - cw / 2f, pos + rh / 2f + dp(4), 10.8f,
                            row.lastUsed ? Skin.ORANGE : Skin.ACCENT, Skin.MED);
                }
                pos += rh + dp(10);
            }
            pos += dp(6);
        }

        sectionTitle("传输任务");
        float cardH = dp(104);
        card(l, pos, r, pos + cardH, 16);
        if (statusLine.isEmpty()) {
            textCenter("暂无任务", W / 2f, pos + cardH / 2 - dp(6), 12.5f, Skin.TEXT3, Skin.REG);
            textCenter("选择文件后点上方按钮发送", W / 2f, pos + cardH / 2 + dp(14), 10.5f, Skin.TEXT3, Skin.REG);
        } else {
            text(ellipsize(statusLine, W - dp(60), 12), l + dp(18), pos + cardH / 2 - dp(2), 12, Skin.TEXT2, Skin.REG);
            text("可在「记录」里查看和删除", l + dp(18), pos + cardH / 2 + dp(18), 10.2f, Skin.TEXT3, Skin.REG);
        }
        pos += cardH + dp(16);
        maxScroll = Math.max(0, pos - navTopOf());
    }

    private float navTopOf() { return H - dp(60); }

    private void primaryButton(float l, float t, float r, float b, String label) {
        round(l, t, r, b, 13, Skin.ACCENT);
        textCenter(label, (l + r) / 2f, t + (b - t) / 2f + dp(4.5f), 12.8f, 0xFFFFFFFF, Skin.MED);
    }

    private void ghostButton(float l, float t, float r, float b, String label) {
        round(l, t, r, b, 13, Skin.CARD);
        border(l, t, r, b, 13, Skin.LINE, 1.2f);
        textCenter(label, (l + r) / 2f, t + (b - t) / 2f + dp(4.5f), 12.8f, Skin.TEXT2, Skin.MED);
    }

    // ------------------------------------------------------------------ files page
    private void drawFiles(float headerBottom, float navTop) {
        float l = dp(16), r = W - dp(16);
        float barH = dp(44);

        // path bar
        card(l, pos, r, pos + barH, 12);
        p.setColor(Skin.ACCENT);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(2.0f));
        p.setStrokeCap(Paint.Cap.ROUND);
        float ax = l + dp(18), ay = pos + barH / 2f;
        canvas.drawLine(ax + dp(5), ay - dp(6), ax, ay, p);
        canvas.drawLine(ax, ay, ax + dp(5), ay + dp(6), p);
        p.setStrokeCap(Paint.Cap.BUTT);
        p.setStyle(Paint.Style.FILL);
        String shown = Paths.displayPath(filePath);
        float chipW = 0;
        if (!filePeer.isEmpty()) {
            chipW = Math.min(dp(96), width(filePeer, 9.8f, Skin.REG) + dp(20));
            round(r - dp(34) - chipW, pos + dp(10), r - dp(34), pos + barH - dp(10), 9, Skin.ACCENT_SOFT);
            textCenter(ellipsize(filePeer, chipW - dp(12), 9.8f), r - dp(34) - chipW / 2f,
                    pos + barH / 2f + dp(3.5f), 9.8f, Skin.ACCENT, Skin.REG);
        }
        text(ellipsize(shown, r - l - dp(120) - chipW, 12), l + dp(38), pos + barH / 2f + dp(4),
                12, Skin.TEXT, Skin.MONO);
        // refresh sits right of the chip so the two never collide
        p.setColor(Skin.TEXT3);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(1.8f));
        p.setStrokeCap(Paint.Cap.ROUND);
        float rx = r - dp(18), ry2 = pos + barH / 2f - dp(1);
        canvas.drawArc(new RectF(rx - dp(7), ry2 - dp(7), rx + dp(1), ry2 + dp(1)), 40, 280, false, p);
        p.setStrokeCap(Paint.Cap.BUTT);
        p.setStyle(Paint.Style.FILL);
        pos += barH + dp(10);

        float listTop = pos;
        // leave room for the footer hint so it never covers the last row
        float listH = navTop - dp(10) - listTop - (hint.isEmpty() ? 0 : dp(34));

        if (fileState == 1) {
            textCenter("正在读取目录…", W / 2f, listTop + listH / 2, 12.5f, Skin.TEXT3, Skin.REG);
        } else if (fileState == 2) {
            textCenter("无法打开该目录", W / 2f, listTop + listH / 2 - dp(10), 13, Skin.TEXT2, Skin.MED);
            textCenter(ellipsize(fileErr, r - l, 10.5f), W / 2f, listTop + listH / 2 + dp(12), 10.5f, Skin.TEXT3, Skin.REG);
        } else if (files.isEmpty()) {
            textCenter("这个文件夹是空的", W / 2f, listTop + listH / 2, 12.5f, Skin.TEXT3, Skin.REG);
        } else {
            float rowH = dp(58);
            float content = files.size() * rowH;
            maxScroll = Math.max(0, content - listH);
            if (scrollY > maxScroll) scrollY = maxScroll;
            if (scrollY < 0) scrollY = 0;

            canvas.save();
            canvas.clipRect(0, listTop, W, listTop + listH);
            for (int i = 0; i < files.size(); i++) {
                float ry = listTop + i * rowH - scrollY;
                if (ry + rowH < listTop - dp(4)) continue;
                if (ry > listTop + listH) break;
                Row row = files.get(i);

                round(l, ry + dp(3), r, ry + rowH - dp(3), 12, Skin.CARD);
                border(l, ry + dp(3), r, ry + rowH - dp(3), 12, Skin.LINE_SOFT, 1f);

                if (row.dir) {
                    round(l + dp(13), ry + dp(16), l + dp(47), ry + dp(44), 9, Skin.DIR_SOFT);
                    p.setColor(Skin.DIR);
                    rect.set(l + dp(22), ry + dp(26), l + dp(38), ry + dp(37));
                    canvas.drawRoundRect(rect, dp(2.5f), dp(2.5f), p);
                    rect.set(l + dp(24), ry + dp(23), l + dp(31), ry + dp(28));
                    canvas.drawRoundRect(rect, dp(2), dp(2), p);
                } else {
                    round(l + dp(13), ry + dp(16), l + dp(47), ry + dp(44), 9, Skin.CARD2);
                    p.setColor(Skin.TEXT3);
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(dp(1.6f));
                    rect.set(l + dp(23), ry + dp(23), l + dp(37), ry + dp(38));
                    canvas.drawRoundRect(rect, dp(2.5f), dp(2.5f), p);
                    p.setStyle(Paint.Style.FILL);
                }

                float nameW = r - l - dp(140);
                text(ellipsize(row.title, nameW, 12.8f), l + dp(58), ry + dp(25), 12.8f,
                        row.dir ? Skin.TEXT : Skin.TEXT2, row.dir ? Skin.MED : Skin.REG);
                text(row.sub, l + dp(58), ry + dp(43), 10, Skin.TEXT3, Skin.REG);

                if (row.dir) {
                    p.setColor(Skin.TEXT3);
                    p.setStyle(Paint.Style.STROKE);
                    p.setStrokeWidth(dp(1.8f));
                    p.setStrokeCap(Paint.Cap.ROUND);
                    float cx2 = r - dp(22), cy2 = ry + rowH / 2f;
                    canvas.drawLine(cx2 - dp(3), cy2 - dp(5), cx2 + dp(2), cy2, p);
                    canvas.drawLine(cx2 + dp(2), cy2, cx2 - dp(3), cy2 + dp(5), p);
                    p.setStrokeCap(Paint.Cap.BUTT);
                    p.setStyle(Paint.Style.FILL);
                } else {
                    float bw = dp(56), bh = dp(28);
                    round(r - dp(12) - bw, ry + (rowH - bh) / 2f, r - dp(12), ry + (rowH + bh) / 2f, 9, Skin.ACCENT_SOFT);
                    textCenter("复制", r - dp(12) - bw / 2f, ry + rowH / 2f + dp(4), 10.8f, Skin.ACCENT, Skin.MED);
                }
            }
            canvas.restore();

            if (maxScroll > 0) {
                float sbarH = Math.max(dp(30), listH * listH / content);
                float sbarY = listTop + (listH - sbarH) * (scrollY / maxScroll);
                round(r - dp(4), sbarY, r - dp(1), sbarY + sbarH, 1.5f, Skin.alpha(Skin.TEXT3, 0.55f));
            }
        }

        if (!hint.isEmpty()) {
            float hy = navTop - dp(34);
            round(l, hy, r, hy + dp(26), 9, Skin.CARD);
            border(l, hy, r, hy + dp(26), 9, Skin.LINE, 1f);
            text(ellipsize(hint, r - l - dp(24), 10), l + dp(12), hy + dp(17), 10,
                    hint.startsWith("已") ? Skin.GREEN : Skin.TEXT2, Skin.REG);
        }
    }

    // ------------------------------------------------------------------ history
    private void drawHistory(float navTop) {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);
        if (history.isEmpty()) {
            textCenter("还没有传输记录", W / 2f, navTop / 2, 13, Skin.TEXT3, Skin.REG);
            maxScroll = 0;
            return;
        }
        for (int i = 0; i < history.size(); i++) {
            Row row = history.get(i);
            float h = dp(78);
            card(l, pos, r, pos + h, 14);

            int col = row.dir ? Skin.ACCENT : Skin.ACCENT2;
            round(l + dp(14), pos + dp(18), l + dp(50), pos + dp(56), 11,
                    row.badge == 1 ? Skin.GREEN_SOFT : (row.badge == 3 ? Skin.RED_SOFT : Skin.ORANGE_SOFT));
            textCenter(row.dir ? "↓" : "↑", l + dp(32), pos + dp(45), 17,
                    row.badge == 1 ? Skin.GREEN : (row.badge == 3 ? Skin.RED : Skin.ORANGE), Skin.BOLD);

            float nameW = r - l - dp(130);
            text(ellipsize(row.title, nameW, 12.6f), l + dp(62), pos + dp(30), 12.6f, Skin.TEXT, Skin.MED);
            text(ellipsize(row.sub, nameW, 10), l + dp(62), pos + dp(48), 10, Skin.TEXT3, Skin.REG);
            text(row.right == null ? "" : row.right, l + dp(62), pos + dp(65), 9.5f, Skin.TEXT3, Skin.MONO);

            if (row.badge == 1) {
                round(r - dp(14) - dp(60), pos + dp(24), r - dp(14), pos + dp(54), 10, Skin.RED_SOFT);
                textCenter("删除", r - dp(14) - dp(30), pos + dp(44), 10.8f, Skin.RED, Skin.MED);
            } else {
                textCenter("—", r - dp(14) - dp(30), pos + dp(44), 12, Skin.TEXT3, Skin.REG);
            }
            pos += h + dp(10);
        }
        pos += dp(6);
        maxScroll = Math.max(0, pos - (navTop - dp(10)));
    }

    // ------------------------------------------------------------------ settings
    private void settingsRow(int id, String title, String sub, String right, int iconTint) {
        float l = dp(16), r = W - dp(16);
        float h = dp(64);
        card(l, pos, r, pos + h, 14);
        round(l + dp(14), pos + dp(16), l + dp(46), pos + dp(48), 10, Skin.mix(iconTint, 0xFFFFFFFF, 0.86f));
        p.setColor(iconTint);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(1.7f));
        canvas.drawCircle(l + dp(30), pos + dp(32), dp(6), p);
        p.setStyle(Paint.Style.FILL);
        text(title, l + dp(58), pos + dp(28), 12.8f, Skin.TEXT, Skin.MED);
        if (sub != null && !sub.isEmpty()) text(ellipsize(sub, r - l - dp(90), 10), l + dp(58), pos + dp(46), 10, Skin.TEXT3, Skin.REG);
        if (right != null) textRight(right, r - dp(34), pos + dp(36), 11, Skin.ACCENT, Skin.MED);
        p.setColor(Skin.TEXT3);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(1.7f));
        p.setStrokeCap(Paint.Cap.ROUND);
        float cx = r - dp(22), cy = pos + h / 2f;
        canvas.drawLine(cx - dp(3), cy - dp(5), cx + dp(2), cy, p);
        canvas.drawLine(cx + dp(2), cy, cx - dp(3), cy + dp(5), p);
        p.setStrokeCap(Paint.Cap.BUTT);
        p.setStyle(Paint.Style.FILL);
        pos += h + dp(10);
    }

    private void switchRow(String title, String sub, boolean on, int iconTint) {
        float l = dp(16), r = W - dp(16);
        float h = dp(64);
        card(l, pos, r, pos + h, 14);
        round(l + dp(14), pos + dp(16), l + dp(46), pos + dp(48), 10, on ? Skin.GREEN_SOFT : Skin.CARD2);
        p.setColor(on ? Skin.GREEN : iconTint);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(dp(1.8f));
        canvas.drawCircle(l + dp(30), pos + dp(32), dp(6), p);
        p.setStyle(Paint.Style.FILL);
        text(title, l + dp(58), pos + dp(28), 12.8f, Skin.TEXT, Skin.MED);
        text(ellipsize(sub, r - l - dp(110), 10), l + dp(58), pos + dp(46), 10, Skin.TEXT3, Skin.REG);

        float tw = dp(46), th = dp(26);
        float tl = r - dp(16) - tw, tt = pos + (h - th) / 2f;
        round(tl, tt, tl + tw, tt + th, th / 2f, on ? Skin.GREEN : Skin.LINE);
        p.setColor(0xFFFFFFFF);
        canvas.drawCircle(on ? tl + tw - th / 2f : tl + th / 2f, tt + th / 2f, th / 2f - dp(2.5f), p);
        pos += h + dp(10);
    }

    private void drawSettings() {
        pos -= (int) scrollY;
        sectionTitle("设备与连接");
        settingsRow(0, "设备名称", selfName, null, Skin.ACCENT);

        boolean allow = Store.allowBrowse(getContext());
        switchRow("允许对方浏览本机文件", "开关立即生效，无需重启", allow, Skin.ACCENT);
        int allowTop = (int) (pos - dp(64) - dp(10));

        boolean awake = Store.keepAwake(getContext());
        switchRow("保持这台手机唤醒", "开启后屏幕不会自动熄灭", awake, Skin.ORANGE);
        int awakeTop = (int) (pos - dp(64) - dp(10));

        pos += dp(6);
        sectionTitle("文件");
        settingsRow(1, "接收目录", Paths.incomingDir().getAbsolutePath(), "更改", Skin.ACCENT);
        settingsRow(2, "全部文件访问权限", "Android 11+ 需要授权才能浏览整个存储",
                Paths.hasAllFilesAccess(getContext()) ? "已授权" : "去授权", Skin.ORANGE);

        pos += dp(6);
        sectionTitle("其他");
        settingsRow(3, "关于 AirLink", "版本、协议与使用说明", null, Skin.ACCENT);
        settingsRow(4, "设备管理", "连接过的设备与每次连接的时间", null, Skin.ORANGE);
        String warmSub;
        {
            String at = Store.warmAt(getContext());
            boolean ok = Store.warmOk(getContext());
            warmSub = at.isEmpty() ? "系统限制后台时会断；加入「不优化」名单可避免"
                    : ("最近保活 " + at + (ok ? " 成功" : " 失败"));
        }

        settingsRow(5, "锁屏后保持连接", warmSub,
                batteryOk ? "已豁免" : "去设置", batteryOk ? Skin.GREEN : Skin.RED);

        settingsHit = new int[]{(int) (pos - dp(64) - dp(10)), allowTop, awakeTop};
        pos += dp(44);   // leave room so the last row can scroll clear of the nav
        maxScroll = Math.max(0, pos - (navTopOf() - dp(10)));
    }

    private int[] settingsHit = new int[]{0, 0, 0};
    private boolean batteryOk = true;
    public void setBatteryOk(boolean v) { batteryOk = v; invalidate(); }

    private void drawAbout() {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);
        // logo + version
        float h = dp(120);
        card(l, pos, r, pos + h, 16);
        round(W / 2f - dp(28), pos + dp(18), W / 2f + dp(28), pos + dp(74), 18, Skin.ACCENT);
        textCenter("A", W / 2f, pos + dp(56), 24, 0xFFFFFFFF, Skin.BOLD);
        textCenter("AirLink 隔空快传", W / 2f, pos + dp(94), 14, Skin.TEXT, Skin.BOLD);
        textCenter("安卓版 1.0 · 297 KB · 单 APK", W / 2f, pos + dp(110), 10.2f, Skin.TEXT3, Skin.REG);
        pos += h + dp(14);

        if (!aboutText.isEmpty()) {
            String[] lines = aboutText.split("\n");
            float bh = dp(14) + lines.length * dp(22) + dp(10);
            card(l, pos, r, pos + bh, 16);
            float y = pos + dp(26);
            for (String line : lines) {
                int sep = line.indexOf('|');
                if (sep > 0) {
                    text(line.substring(0, sep), l + dp(18), y, 11.2f, Skin.TEXT3, Skin.REG);
                    text(ellipsize(line.substring(sep + 1), r - l - dp(130), 11.2f),
                            l + dp(104), y, 11.2f, Skin.TEXT2, Skin.REG);
                } else {
                    text(ellipsize(line, r - l - dp(36), 11.2f), l + dp(18), y, 11.2f, Skin.TEXT2, Skin.REG);
                }
                y += dp(22);
            }
            pos += bh + dp(14);
        }
        pos += dp(8);
        maxScroll = Math.max(0, pos - (navTopOf() - dp(10)));
    }

    private void drawSaveDir() {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);
        text("选择一个文件夹作为默认保存位置", l + dp(4), pos + dp(10), 11.5f, Skin.TEXT3, Skin.REG);
        pos += dp(26);
        for (int i = 0; i < saveDirs.size(); i++) {
            Row row = saveDirs.get(i);
            float h = dp(62);
            card(l, pos, r, pos + h, 14);
            if (row.selected) border(l, pos, r, pos + h, 14, Skin.ACCENT, 1.6f);
            round(l + dp(14), pos + dp(16), l + dp(46), pos + dp(48), 10, Skin.DIR_SOFT);
            p.setColor(Skin.DIR);
            rect.set(l + dp(23), pos + dp(26), l + dp(37), pos + dp(38));
            canvas.drawRoundRect(rect, dp(2.5f), dp(2.5f), p);
            text(row.title, l + dp(58), pos + dp(28), 12.6f, Skin.TEXT, Skin.MED);
            text(ellipsize(row.sub, r - l - dp(90), 9.8f), l + dp(58), pos + dp(45), 9.8f, Skin.TEXT3, Skin.MONO);
            if (row.selected) {
                p.setColor(Skin.ACCENT);
                canvas.drawCircle(r - dp(26), pos + h / 2f, dp(7), p);
                p.setColor(0xFFFFFFFF);
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(dp(1.8f));
                p.setStrokeCap(Paint.Cap.ROUND);
                canvas.drawLine(r - dp(29), pos + h / 2f, r - dp(26), pos + h / 2f + dp(3), p);
                canvas.drawLine(r - dp(26), pos + h / 2f + dp(3), r - dp(22), pos + h / 2f - dp(3), p);
                p.setStrokeCap(Paint.Cap.BUTT);
                p.setStyle(Paint.Style.FILL);
            }
            pos += h + dp(10);
        }
        pos += dp(10);
        card(l, pos, r, pos + dp(46), 13);
        border(l, pos, r, pos + dp(46), 13, Skin.ACCENT, 1.2f);
        textCenter("手动输入完整路径…", W / 2f, pos + dp(28), 11.8f, Skin.ACCENT, Skin.MED);
        pos += dp(52);

        card(l, pos, r, pos + dp(76), 14);
        text("提示", l + dp(18), pos + dp(24), 11.5f, Skin.TEXT, Skin.MED);
        text("安卓 11 起，保存到公共目录需要「所有文件访问」权限。", l + dp(18), pos + dp(44), 10.2f, Skin.TEXT3, Skin.REG);
        text("没有授权时会自动存到应用私有目录。", l + dp(18), pos + dp(62), 10.2f, Skin.TEXT3, Skin.REG);
        pos += dp(90);
        maxScroll = Math.max(0, pos - (navTopOf() - dp(10)));
    }

    private void drawDevices() {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);
        text("连接过的设备", l + dp(4), pos + dp(12), 11.5f, Skin.TEXT3, Skin.MED);
        pos += dp(26);
        if (devices.isEmpty()) {
            card(l, pos, r, pos + dp(96), 16);
            textCenter("还没有连接过设备", W / 2f, pos + dp(44), 12.5f, Skin.TEXT3, Skin.REG);
            textCenter("连上任意一台设备后，这里会留下记录", W / 2f, pos + dp(64), 10.5f, Skin.TEXT3, Skin.REG);
            pos += dp(110);
            maxScroll = 0;
            return;
        }
        for (int i = 0; i < devices.size(); i++) {
            Row row = devices.get(i);
            float h = dp(74);
            card(l, pos, r, pos + h, 14);
            round(l + dp(14), pos + dp(18), l + dp(50), pos + dp(54), 11, Skin.ACCENT_SOFT);
            textCenter(row.title.isEmpty() ? "?" : row.title.substring(0, 1).toUpperCase(),
                    l + dp(32), pos + dp(42), 15, Skin.ACCENT, Skin.BOLD);
            float nameW = r - l - dp(120);
            text(ellipsize(row.title, nameW, 12.8f), l + dp(62), pos + dp(30), 12.8f, Skin.TEXT, Skin.MED);
            text(ellipsize(row.sub, nameW, 9.8f), l + dp(62), pos + dp(48), 9.8f, Skin.TEXT3, Skin.REG);

            float dw = dp(54);
            round(r - dp(14) - dw, pos + (h - dp(28)) / 2f, r - dp(14), pos + (h + dp(28)) / 2f, 9, Skin.RED_SOFT);
            textCenter("删除", r - dp(14) - dw / 2f, pos + h / 2f + dp(4), 10.6f, Skin.RED, Skin.MED);
            p.setColor(Skin.TEXT3);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(dp(1.7f));
            p.setStrokeCap(Paint.Cap.ROUND);
            float cx2 = r - dp(14) - dw - dp(16), cy2 = pos + h / 2f;
            canvas.drawLine(cx2 - dp(3), cy2 - dp(5), cx2 + dp(2), cy2, p);
            canvas.drawLine(cx2 + dp(2), cy2, cx2 - dp(3), cy2 + dp(5), p);
            p.setStrokeCap(Paint.Cap.BUTT);
            p.setStyle(Paint.Style.FILL);
            pos += h + dp(10);
        }
        pos += dp(6);
        maxScroll = Math.max(0, pos - (navTopOf() - dp(10)));
    }

    private void drawDeviceDetail() {
        pos -= (int) scrollY;
        float l = dp(16), r = W - dp(16);
        float h = dp(84);
        card(l, pos, r, pos + h, 16);
        text(ellipsize(deviceName, r - l - dp(140), 15), l + dp(18), pos + dp(30), 15, Skin.TEXT, Skin.BOLD);
        text(deviceIp + "  ·  共 " + deviceStamps.size() + " 次连接", l + dp(18), pos + dp(50),
                10.5f, Skin.TEXT3, Skin.REG);
        float dw = dp(88);
        round(r - dp(16) - dw, pos + dp(24), r - dp(16), pos + dp(56), 10, Skin.RED_SOFT);
        textCenter("删除此设备", r - dp(16) - dw / 2f, pos + dp(44), 10.6f, Skin.RED, Skin.MED);
        pos += h + dp(14);

        text("连接记录", l + dp(4), pos + dp(10), 11.5f, Skin.TEXT3, Skin.MED);
        pos += dp(24);
        if (deviceStamps.isEmpty()) {
            textCenter("没有记录", W / 2f, pos + dp(30), 12, Skin.TEXT3, Skin.REG);
        }
        for (int i = 0; i < deviceStamps.size(); i++) {
            float rh = dp(40);
            card(l, pos, r, pos + rh, 11);
            p.setColor(Skin.ACCENT);
            canvas.drawCircle(l + dp(20), pos + rh / 2f, dp(3f), p);
            text(deviceStamps.get(i).title, l + dp(36), pos + rh / 2f + dp(4), 11.6f, Skin.TEXT2, Skin.REG);
            pos += rh + dp(8);
        }
        pos += dp(10);
        maxScroll = Math.max(0, pos - (navTopOf() - dp(10)));
    }

    // ------------------------------------------------------------------ mirror
    private Bitmap mirrorBmp;
    private String mirrorMsg = "先在「快传」里连接电脑";
    private final RectF mirrorDst = new RectF();
    private boolean mirrorHasFrame = false;
    private boolean mirrorFull = false;
    private float mScale = 1f, mPanX = 0f, mPanY = 0f;   // pinch zoom + pan
    private float mBaseW = 0, mBaseH = 0, mBaseX = 0, mBaseY = 0;
    private float pDownDist = 0f, pDownScale = 1f, pDownX = 0f, pDownY = 0f;
    private float pDownPanX = 0f, pDownPanY = 0f;
    private boolean pMulti = false;
    public void setMirrorFullscreen(boolean v) {
        mirrorFull = v;
        mScale = 1f; mPanX = 0; mPanY = 0;
        invalidate();
    }

    private Bitmap mirrorPending;

    public void setMirrorFrame(Bitmap b) {
        mirrorBmp = b;
        mirrorHasFrame = b != null;
        invalidate();
    }

    /** Called from the stream thread: keep only the newest frame, no queue. */
    public void offerMirrorFrame(Bitmap b) {
        mirrorPending = b;
        postInvalidateOnAnimation();
    }
    public void setMirrorMsg(String s) { mirrorMsg = s == null ? "" : s; invalidate(); }

    private void drawMirror() {
        float l = dp(8), r = W - dp(8);
        float top = statusInset + dp(56) + dp(8);
        float bottom = navTopOf() - dp(8);
        canvas.drawRect(l, top, r, bottom, p0(Skin.CARD));
        if (mirrorBmp == null) {
            textCenter(mirrorMsg, W / 2f, (top + bottom) / 2f, 13f, Skin.TEXT3, Skin.REG);
            mirrorHasFrame = false;
            return;
        }
        float ia = (float) mirrorBmp.getWidth() / mirrorBmp.getHeight();
        float ba = (r - l) / (bottom - top);
        float w, h;
        if (ba > ia) { h = bottom - top; w = h * ia; }
        else { w = r - l; h = w / ia; }
        float x = l + ((r - l) - w) / 2f;
        float y = top + ((bottom - top) - h) / 2f;
        mirrorDst.set(x, y, x + w, y + h);
        canvas.drawBitmap(mirrorBmp, null, mirrorDst, null);
        mirrorHasFrame = true;
    }

    /** Fullscreen variant: the frame covers the display edge to edge. */
    private void drawMirrorFull() {
        if (mirrorBmp == null) {
            textCenter(mirrorMsg, W / 2f, H / 2f, 14f, 0xFFB9C3D2, Skin.REG);
            mirrorHasFrame = false;
            return;
        }
        float ia = (float) mirrorBmp.getWidth() / mirrorBmp.getHeight();
        float ba = W / H;
        float w, h;
        // COVER: fill the display edge to edge. The desktop is 16:10 and the
        // phone is wider, so a sliver of the top and bottom sits outside the
        // screen; pinch out below 1x to bring it back into view.
        if (ba < ia) { h = H; w = h * ia; } else { w = W; h = w / ia; }
        mBaseW = w; mBaseH = h;
        mBaseX = (W - w) / 2f;
        mBaseY = (H - h) / 2f;

        float z = Math.max(0.5f, mScale);
        float dw = w * z, dh = h * z;
        float dx = mBaseX - (dw - w) / 2f + mPanX;
        float dy = mBaseY - (dh - h) / 2f + mPanY;
        // keep the picture from leaving the screen empty
        float maxX = Math.max(0, (dw - W) / 2f), maxY = Math.max(0, (dh - H) / 2f);
        if (mPanX > maxX) mPanX = maxX;
        if (mPanX < -maxX) mPanX = -maxX;
        if (mPanY > maxY) mPanY = maxY;
        if (mPanY < -maxY) mPanY = -maxY;
        dx = mBaseX - (dw - w) / 2f + mPanX;
        dy = mBaseY - (dh - h) / 2f + mPanY;

        mirrorDst.set(dx, dy, dx + dw, dy + dh);
        canvas.drawBitmap(mirrorBmp, null, mirrorDst, null);
        mirrorHasFrame = true;
    }

    /** Tap position -> 0..65535 in the peer's screen space, through zoom/pan. */
    private void mirrorTap(float x, float y) {
        if (!mirrorHasFrame || mirrorDst.width() <= 0) return;
        float nx = (x - mirrorDst.left) / mirrorDst.width();
        float ny = (y - mirrorDst.top) / mirrorDst.height();
        nx = Math.max(0, Math.min(1, nx));
        ny = Math.max(0, Math.min(1, ny));
        cb.onMirrorTap((int) (nx * 65535f), (int) (ny * 65535f));
    }

    private float dist(float x1, float y1, float x2, float y2) {
        float dx = x1 - x2, dy = y1 - y2;
        return (float) Math.sqrt(dx * dx + dy * dy);
    }

    private Paint p0(int color) {
        p.setStyle(Paint.Style.FILL);
        p.setColor(color);
        return p;
    }
    // ------------------------------------------------------------------ WAN
    private String wanAddr = "", wanCode = "";

    public void setWanFields(String addr, String code) {
        wanAddr = addr == null ? "" : addr;
        wanCode = code == null ? "" : code;
        invalidate();
    }

    private void drawWan() {
        float l = dp(16), r = W - dp(16);
        float y = statusInset + dp(56) + dp(16);
        canvas.drawRoundRect(new RectF(l, y, r, y + dp(112)), dp(14), dp(14), p0(Skin.CARD));
        text("广域网连接", l + dp(18), y + dp(34), 15f, Skin.TEXT, Skin.BOLD);
        text("两台设备不在同一个 Wi-Fi 时用这里", l + dp(18), y + dp(58), 11.5f, Skin.TEXT3, Skin.REG);
        text("在电脑的「WAN连接」页可以看到公网地址和配对码", l + dp(18), y + dp(82), 11f, Skin.TEXT3, Skin.REG);
        y += dp(128);

        canvas.drawRoundRect(new RectF(l, y, r, y + dp(72)), dp(14), dp(14), p0(Skin.CARD));
        text("电脑公网地址", l + dp(18), y + dp(28), 11.5f, Skin.TEXT3, Skin.REG);
        text(wanAddr.isEmpty() ? "例如 1.2.3.4:45678" : wanAddr, l + dp(18), y + dp(52), 13.5f,
                wanAddr.isEmpty() ? Skin.TEXT3 : Skin.TEXT, Skin.REG);
        y += dp(84);

        canvas.drawRoundRect(new RectF(l, y, r, y + dp(72)), dp(14), dp(14), p0(Skin.CARD));
        text("配对码", l + dp(18), y + dp(28), 11.5f, Skin.TEXT3, Skin.REG);
        text(wanCode.isEmpty() ? "电脑上显示的 6 位数字" : wanCode, l + dp(18), y + dp(52), 13.5f,
                wanCode.isEmpty() ? Skin.TEXT3 : Skin.TEXT, Skin.REG);
        y += dp(84);

        RectF btn = new RectF(l, y, r, y + dp(50));
        canvas.drawRoundRect(btn, dp(12), dp(12), p0(Skin.ACCENT));
        textCenter("连接", W / 2f, y + dp(31), 14f, 0xFFFFFFFF, Skin.BOLD);
    }
    // ------------------------------------------------------------------ touch
    /** Mirror page owns its gestures: tap = click, pinch = zoom, drag = pan. */
    private boolean mirrorTouch(MotionEvent e) {
        float x = e.getX(), y = e.getY();
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                pMulti = false;
                downY = y;
                downScroll = 0;
                dragging = false;
                pDownX = x; pDownY = y;
                pDownPanX = mPanX; pDownPanY = mPanY;
                return true;
            case MotionEvent.ACTION_POINTER_DOWN:
                if (e.getPointerCount() >= 2) {
                    pMulti = true;
                    pDownDist = dist(e.getX(0), e.getY(0), e.getX(1), e.getY(1));
                    pDownScale = mScale;
                }
                return true;
            case MotionEvent.ACTION_MOVE:
                if (e.getPointerCount() >= 2) {
                    float d = dist(e.getX(0), e.getY(0), e.getX(1), e.getY(1));
                    if (pDownDist > 10f) {
                        mScale = Math.max(0.5f, Math.min(5f, pDownScale * d / pDownDist));
                        invalidate();
                    }
                    pMulti = true;
                } else if (mScale > 1.01f) {
                    float dx = x - pDownX, dy = y - pDownY;
                    if (Math.abs(dx) > dp(6) || Math.abs(dy) > dp(6)) dragging = true;
                    if (dragging) {
                        mPanX = pDownPanX + dx;
                        mPanY = pDownPanY + dy;
                        invalidate();
                    }
                }
                return true;
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP:
                if (!pMulti && !dragging) mirrorTap(x, y);
                if (e.getActionMasked() == MotionEvent.ACTION_UP) {
                    // snap back only when the pinch ends right at 1x
                    if (Math.abs(mScale - 1f) < 0.06f) { mScale = 1f; mPanX = 0; mPanY = 0; invalidate(); }
                }
                pMulti = false;
                dragging = false;
                return true;
        }
        return true;
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        float x = e.getX(), y = e.getY();

        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downY = y;
                downScroll = scrollY;
                dragging = false;
                if (getParent() != null) getParent().requestDisallowInterceptTouchEvent(true);
                return true;
            case MotionEvent.ACTION_MOVE:
                if (Math.abs(y - downY) > dp(6)) dragging = true;
                if (dragging) {
                    scrollY = downScroll - (y - downY);
                    scrollY = Math.max(0, Math.min(scrollY, maxScroll));
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!dragging) tap(x, y);
                dragging = false;
                return true;
        }
        return true;
    }

    private void tap(float x, float y) {
        if (cb == null) return;
        float navTop = navTopOf();
        if (y >= navTop) {
            int idx = (int) (x / (W / 5f));
            idx = Math.max(0, Math.min(4, idx));
            int[] pages = {PAGE_SEND, PAGE_FILES, PAGE_WAN, PAGE_HISTORY, PAGE_SET};
            goPage(pages[idx]);
            return;
        }
        float headerBottom = statusInset + dp(56);

        if (page == PAGE_ABOUT || page == PAGE_SAVEDIR || page == PAGE_DEVICES
                || page == PAGE_DEVICE_DETAIL) {
            if (y < headerBottom) { cb.onBack(); return; }
        }

        float l = dp(16), r = W - dp(16);
        float base = headerBottom + dp(10);
        // every scrolling page draws from base - scrollY, so hit testing must use
        // the same origin or taps land one row off once the list is scrolled
        float scrolled = base - scrollY;

        if (page == PAGE_SEND) {
            float yy = scrolled + dp(96) + dp(14);
            float bw = (r - l - dp(10)) / 2f;
            if (y >= yy && y <= yy + dp(50)) {
                if (x < l + bw) cb.onPickFiles(); else cb.onScanQr();
                return;
            }
            yy += dp(50) + dp(12);
            if (y >= yy && y <= yy + dp(46)) { cb.onManualIp(); return; }
            yy += dp(46) + dp(16);
            yy += dp(24);       // section title
            if (peers.isEmpty()) return;
            for (int i = 0; i < peers.size(); i++) {
                float rh = dp(70);
                if (y >= yy && y <= yy + rh) {
                    Row row = peers.get(i);
                    if (row.off) { cb.onPeer(i); return; }
                    float cw = dp(row.lastUsed && !row.selected ? 78 : 62);                    if (x >= r - dp(14) - cw) { cb.onPeer(i); return; }
                    float dw = dp(54);
                    if (x >= r - dp(14) - cw - dp(8) - dw && x < r - dp(14) - cw - dp(8)) {
                        cb.onPeerDisconnect(i);
                        return;
                    }
                    cb.onPeer(i);
                    return;
                }
                yy += rh + dp(10);
            }
        } else if (page == PAGE_FILES) {
            float barH = dp(44);
            if (y >= base && y <= base + barH) {
                if (x < l + dp(40)) { cb.onFileUp(); return; }
                cb.onFileRefresh();
                return;
            }
            float listTop = base + barH + dp(10);
            if (y < listTop) return;
            float rowH = dp(58);
            int idx = (int) ((y - listTop + scrollY) / rowH);
            if (idx < 0 || idx >= files.size()) return;
            Row row = files.get(idx);
            if (row.dir) { cb.onFileEnter(idx); return; }
            if (x >= r - dp(72)) { cb.onFileCopy(idx); return; }
            cb.onFileEnter(idx);
        } else if (page == PAGE_HISTORY) {
            float rowH = dp(78) + dp(10);
            for (int i = 0; i < history.size(); i++) {
                float ry = scrolled + i * rowH;
                if (y >= ry && y <= ry + dp(78)) {
                    Row row = history.get(i);
                    if (row.badge == 1 && x >= r - dp(14) - dp(60)) { cb.onHistoryDelete(i); return; }
                    cb.onHistoryOpen(i);
                    return;
                }
            }
        } else if (page == PAGE_SET) {
            float rowH = dp(64) + dp(10);
            float b = scrolled + dp(24);               // first row (device name)
            if (y >= b && y <= b + dp(64)) { cb.onEditName(); return; }
            float allowTop = b + rowH;
            if (y >= allowTop && y <= allowTop + dp(64)) { cb.onToggleAllowBrowse(); return; }
            float awakeTop = b + rowH * 2;
            if (y >= awakeTop && y <= awakeTop + dp(64)) { cb.onKeepAwake(); return; }
            float afterSwitch = b + rowH * 3 + dp(6) + dp(24);
            if (y >= afterSwitch && y <= afterSwitch + dp(64)) { cb.onOpenSaveDirSettings(); return; }
            if (y >= afterSwitch + rowH && y <= afterSwitch + rowH + dp(64)) { cb.onRequestStoragePerm(); return; }
            if (y >= afterSwitch + rowH * 2 && y <= afterSwitch + rowH * 2 + dp(64)) { cb.onOpenAbout(); return; }
            if (y >= afterSwitch + rowH * 3 && y <= afterSwitch + rowH * 3 + dp(64)) { cb.onOpenDevices(); return; }
            if (y >= afterSwitch + rowH * 4 && y <= afterSwitch + rowH * 4 + dp(64)) { cb.onBatterySetting(); return; }


        } else if (page == PAGE_WAN) {
            float wy = statusInset + dp(56) + dp(16) + dp(128);
            if (y >= wy && y < wy + dp(72)) { cb.onWanAddr(); return; }
            wy += dp(84);
            if (y >= wy && y < wy + dp(72)) { cb.onWanCode(); return; }
            wy += dp(84);
            if (y >= wy && y <= wy + dp(50)) { cb.onWanConnect(); return; }
        } else if (page == PAGE_DEVICES) {
            float yy = scrolled + dp(26);
            float h = dp(74) + dp(10);
            for (int i = 0; i < devices.size(); i++) {
                if (y >= yy && y <= yy + dp(74)) {
                    if (x >= r - dp(14) - dp(54)) { cb.onDeviceDelete(i); return; }
                    cb.onDeviceOpen(i);
                    return;
                }
                yy += h;
            }
        } else if (page == PAGE_DEVICE_DETAIL) {
            float b = scrolled;
            if (y >= b && y <= b + dp(84)) {
                if (x >= r - dp(16) - dp(88)) { cb.onDeviceDeleteAll(); return; }
            }
        } else if (page == PAGE_SAVEDIR) {
            float yy = scrolled + dp(26);
            for (int i = 0; i < saveDirs.size(); i++) {
                if (y >= yy && y <= yy + dp(62)) { cb.onSaveDirChosen(i); return; }
                yy += dp(62) + dp(10);
            }
            if (y >= yy && y <= yy + dp(46)) { cb.onSaveDirManual(); return; }
        }
    }
}
