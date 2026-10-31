package com.airlink;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.ImageFormat;
import android.graphics.Paint;
import android.graphics.RectF;
import android.hardware.Camera;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import com.google.zxing.BinaryBitmap;
import com.google.zxing.DecodeHintType;
import com.google.zxing.NotFoundException;
import com.google.zxing.PlanarYUVLuminanceSource;
import com.google.zxing.Result;
import com.google.zxing.common.HybridBinarizer;
import com.google.zxing.qrcode.QRCodeReader;

import java.util.EnumMap;
import java.util.List;
import java.util.Map;

/**
 * Camera preview + QR decode (classic Camera API + ZXing).
 *
 * The sensor delivers frames rotated relative to what the operator sees, so the
 * NV21 buffer is rotated by the same angle the preview is rotated by before it
 * reaches ZXing; without this the decoder is fed a sideways image.
 */
public class ScanActivity extends Activity implements SurfaceHolder.Callback, Camera.PreviewCallback {

    private static final String TAG = "AirLinkScan";

    private SurfaceView surface;
    private Camera camera;
    private int cameraId = -1;
    private final QRCodeReader reader = new QRCodeReader();
    private final Handler main = new Handler(Looper.getMainLooper());
    private volatile boolean decoding = false;
    private boolean done = false;
    private volatile boolean toldWrong = false;
    private volatile int framesSeen = 0;
    private TextView hint;
    private TorchView torchBtn;
    private boolean torchOn = false;

    /** Degrees the frame must be rotated to match the upright preview. */
    private int frameRotation = 90;

    @Override
    protected void onCreate(Bundle b) {
        startHints();
        super.onCreate(b);

        float d = getResources().getDisplayMetrics().density;
        int statusInset = 0;
        int id = getResources().getIdentifier("status_bar_height", "dimen", "android");
        if (id > 0) statusInset = getResources().getDimensionPixelSize(id);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(0xFF0C0E12);

        surface = new SurfaceView(this);
        root.addView(surface, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        surface.getHolder().addCallback(this);

        FinderView finder = new FinderView(this);
        root.addView(finder, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        TextView title = new TextView(this);
        title.setText("扫描二维码连接");
        title.setTextColor(0xFFFFFFFF);
        title.setTextSize(17);
        title.setGravity(Gravity.CENTER);
        title.setShadowLayer(8f, 0f, 1f, 0xFF000000);
        FrameLayout.LayoutParams tp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        tp.gravity = Gravity.TOP;
        tp.topMargin = statusInset + (int) (22 * d);
        root.addView(title, tp);

        hint = new TextView(this);
        hint.setText("把电脑上的二维码放进取景框");
        hint.setTextColor(0xFFE8EDF6);
        hint.setTextSize(14);
        hint.setGravity(Gravity.CENTER);
        hint.setShadowLayer(6f, 0f, 1f, 0xFF000000);

        // torch switch: an icon, sitting right under the viewfinder square
        torchBtn = new TorchView(this);
        torchBtn.setOnClickListener(v -> toggleTorch());

        // place both relative to the finder square so they land under it on any screen
        int sw = getResources().getDisplayMetrics().widthPixels;
        int sh = getResources().getDisplayMetrics().heightPixels;
        float side = Math.min(sw, sh) * 0.62f;
        int boxBottom = (int) ((sh - side) / 2f + side);
        int torchSize = (int) (54 * d);
        FrameLayout.LayoutParams fp = new FrameLayout.LayoutParams(torchSize, torchSize);
        fp.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        fp.topMargin = boxBottom + (int) (16 * d);
        root.addView(torchBtn, fp);

        FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.gravity = Gravity.TOP;
        lp.topMargin = boxBottom + (int) (82 * d);
        root.addView(hint, lp);

        setContentView(root);
    }

    /** Flashlight glyph in a round button; no text needed. */
    private static final class TorchView extends View {
        private boolean on;
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);

        TorchView(android.content.Context c) {
            super(c);
        }

        void setTorchOn(boolean v) {
            on = v;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas c) {
            float w = getWidth(), h = getHeight();
            float d = getResources().getDisplayMetrics().density;
            float cx = w / 2f, cy = h / 2f;

            p.setStyle(Paint.Style.FILL);
            p.setColor(on ? 0xE6FFC53D : 0x66000000);
            c.drawCircle(cx, cy, Math.min(w, h) / 2f - d, p);

            int fg = on ? 0xFF241A00 : 0xFFFFFFFF;
            p.setColor(fg);
            RectF head = new RectF(cx - 7 * d, cy - 8 * d, cx + 7 * d, cy - 2.5f * d);
            c.drawRoundRect(head, 2 * d, 2 * d, p);
            RectF body = new RectF(cx - 4.6f * d, cy - 2.5f * d, cx + 4.6f * d, cy + 8.5f * d);
            c.drawRoundRect(body, 2 * d, 2 * d, p);

            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(1.7f * d);
            p.setStrokeCap(Paint.Cap.ROUND);
            c.drawLine(cx - 11 * d, cy - 9.5f * d, cx - 8 * d, cy - 6.5f * d, p);
            c.drawLine(cx + 11 * d, cy - 9.5f * d, cx + 8 * d, cy - 6.5f * d, p);
            c.drawLine(cx, cy - 13 * d, cx, cy - 10.5f * d, p);
            p.setStrokeCap(Paint.Cap.BUTT);
            p.setStyle(Paint.Style.FILL);
        }
    }

    /** Dims everything except a centred square so aiming is obvious. */
    private static final class FinderView extends View {
        private final Paint dim = new Paint();
        private final Paint edge = new Paint();
        private final RectF box = new RectF();

        FinderView(android.content.Context c) {
            super(c);
            dim.setColor(0x9E000000);
            edge.setColor(0xFF3E86FF);
            edge.setStyle(Paint.Style.STROKE);
            edge.setStrokeWidth(3f * getResources().getDisplayMetrics().density);
            edge.setAntiAlias(true);
        }

        @Override
        protected void onDraw(Canvas c) {
            int w = getWidth(), h = getHeight();
            float side = Math.min(w, h) * 0.62f;
            float l = (w - side) / 2f, t = (h - side) / 2f;
            box.set(l, t, l + side, t + side);
            c.drawRect(0, 0, w, t, dim);
            c.drawRect(0, box.bottom, w, h, dim);
            c.drawRect(0, t, l, box.bottom, dim);
            c.drawRect(box.right, t, w, box.bottom, dim);
            float r = 18f * getResources().getDisplayMetrics().density;
            c.drawRoundRect(box, r, r, edge);
        }
    }

    // ------------------------------------------------------------------ camera
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        try {
            Camera.CameraInfo info = new Camera.CameraInfo();
            for (int i = 0; i < Camera.getNumberOfCameras(); i++) {
                Camera.getCameraInfo(i, info);
                if (info.facing == Camera.CameraInfo.CAMERA_FACING_BACK) { cameraId = i; break; }
            }
            if (cameraId < 0) {
                Camera.getCameraInfo(0, info);
                cameraId = 0;
            }
            camera = Camera.open(cameraId);
            if (camera == null) { fail("没有可用的相机"); return; }

            Camera.getCameraInfo(cameraId, info);
            android.view.Display display = getWindowManager().getDefaultDisplay();
            int degrees;
            switch (display.getRotation()) {
                case android.view.Surface.ROTATION_90: degrees = 90; break;
                case android.view.Surface.ROTATION_180: degrees = 180; break;
                case android.view.Surface.ROTATION_270: degrees = 270; break;
                default: degrees = 0; break;
            }
            int result;
            if (info.facing == Camera.CameraInfo.CAMERA_FACING_FRONT) {
                result = (info.orientation + degrees) % 360;
                result = (360 - result) % 360;
            } else {
                result = (info.orientation - degrees + 360) % 360;
            }
            // This is exactly the rotation applied to the preview, so the same
            // angle turns the raw frame upright for the decoder.
            frameRotation = result;
            camera.setDisplayOrientation(result);

            Camera.Parameters params = camera.getParameters();
            params.setPreviewFormat(ImageFormat.NV21);
            List<Camera.Size> sizes = params.getSupportedPreviewSizes();
            if (sizes != null && !sizes.isEmpty()) {
                // Aim near 1280x720: a dense QR needs enough pixels to resolve
                // its modules, while anything larger just slows the decode loop.
                Camera.Size best = sizes.get(0);
                int target = 1280 * 720;
                int bestDelta = Integer.MAX_VALUE;
                for (Camera.Size s : sizes) {
                    int px = s.width * s.height;
                    if (px < 640 * 480) continue;
                    int delta = Math.abs(px - target);
                    if (delta < bestDelta) { bestDelta = delta; best = s; }
                }
                params.setPreviewSize(best.width, best.height);
            }
            // Close-up QR scanning wants macro focus; fall back to continuous,
            // then plain auto. Continuous alone often settles at the wrong
            // distance on a static scene and never hunts again.
            boolean focusSet = false;
            for (String mode : new String[] {
                    Camera.Parameters.FOCUS_MODE_MACRO,
                    Camera.Parameters.FOCUS_MODE_CONTINUOUS_PICTURE,
                    Camera.Parameters.FOCUS_MODE_AUTO }) {
                try { params.setFocusMode(mode); focusSet = true; break; }
                catch (Exception ignored) {}
            }
            camera.setParameters(params);
            camera.setPreviewDisplay(holder);
            camera.setPreviewCallback(this);
            camera.startPreview();
            startFocusLoop();

            // The preview used to be stretched over the whole view, which
            // squashed it. Size the surface to the camera's own aspect ratio.
            Camera.Size shown = camera.getParameters().getPreviewSize();
            fitPreview(shown.width, shown.height);

            if (!hasFlash(camera)) {
                main.post(() -> { if (torchBtn != null) torchBtn.setVisibility(View.GONE); });
            }
        } catch (Exception e) {
            Log.w(TAG, "camera open failed", e);
            fail("无法打开相机：" + e.getMessage());
        }
    }

    /**
     * Centre-crops the preview surface to the camera's aspect ratio so the
     * picture is not stretched to the screen shape.
     */
    private void fitPreview(final int pw, final int ph) {
        if (pw <= 0 || ph <= 0) return;
        // after the preview rotation, width and height swap for 90/270
        final float aspect = (frameRotation % 180 == 0) ? (float) pw / ph : (float) ph / pw;
        main.post(() -> {
            if (isFinishing()) return;
            ViewGroup parent = (ViewGroup) surface.getParent();
            if (parent == null) return;
            int vw = parent.getWidth(), vh = parent.getHeight();
            if (vw == 0 || vh == 0) return;
            int w, h;
            if ((float) vw / vh > aspect) {
                h = vh;
                w = Math.round(vh * aspect);
            } else {
                w = vw;
                h = Math.round(vw / aspect);
            }
            FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(w, h);
            lp.gravity = Gravity.CENTER;
            surface.setLayoutParams(lp);
        });
    }

    private static boolean hasFlash(Camera cam) {        try {
            List<String> modes = cam.getParameters().getSupportedFlashModes();
            return modes != null && modes.contains(Camera.Parameters.FLASH_MODE_TORCH);
        } catch (Exception e) {
            return false;
        }
    }

    private void toggleTorch() {
        if (camera == null) return;
        try {
            Camera.Parameters p = camera.getParameters();
            p.setFlashMode(torchOn ? Camera.Parameters.FLASH_MODE_OFF
                                   : Camera.Parameters.FLASH_MODE_TORCH);
            camera.setParameters(p);
            torchOn = !torchOn;
            torchBtn.setTorchOn(torchOn);
        } catch (Exception e) {
            fail("这个摄像头不支持手电筒");
        }
    }

    @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int hh) {}
    @Override public void surfaceDestroyed(SurfaceHolder h) { releaseCamera(); }

    private Thread focusThread;

    /** Nudge the lens towards the code a few times per second. */
    private void startFocusLoop() {
        if (focusThread != null) return;
        focusThread = new Thread(() -> {
            while (!done) {
                try {
                    Camera c = camera;
                    if (c != null) c.autoFocus(null);
                } catch (Exception ignored) {}
                try { Thread.sleep(900); } catch (InterruptedException e) { return; }
            }
        }, "airlink-focus");
        focusThread.setDaemon(true);
        focusThread.start();
    }

    // ------------------------------------------------------------------ decode
    @Override
    public void onPreviewFrame(byte[] data, Camera cam) {
        if (decoding || done || data == null || cam == null) return;
        framesSeen++;
        if (framesSeen % 15 == 0) {
            final int n = framesSeen;
            main.post(() -> { if (!done) hint.setText("正在识别… 已处理 " + n + " 帧"); });
        }
        decoding = true;
        Camera.Size size;
        try {
            size = cam.getParameters().getPreviewSize();
        } catch (Exception e) {
            decoding = false;
            return;
        }
        final int w = size.width, h = size.height;
        final byte[] frame = data.clone();
        final int rot = frameRotation;
        new Thread(() -> {
            try {
                byte[] upright = rotateYuv(frame, w, h, rot);
                int dw = (rot == 90 || rot == 270) ? h : w;
                int dh = (rot == 90 || rot == 270) ? w : h;
                PlanarYUVLuminanceSource src =
                        new PlanarYUVLuminanceSource(upright, dw, dh, 0, 0, dw, dh, false);
                BinaryBitmap bmp = new BinaryBitmap(new HybridBinarizer(src));
                Map<DecodeHintType, Object> hints = new EnumMap<>(DecodeHintType.class);
                hints.put(DecodeHintType.TRY_HARDER, Boolean.TRUE);
                Result r = null;
                try {
                    r = reader.decode(bmp, hints);
                } catch (NotFoundException nf) {
                    // A QR drawn small on a monitor occupies only a few hundred
                    // pixels; cropping the middle and doubling it gives the
                    // detector more pixels per module.
                    r = decodeMagnified(upright, dw, dh, hints);
                }
                if (r == null) return;
                String text = r.getText();
                if (text != null && text.trim().contains("airlink://")) {
                    finishWith(text);
                } else if (text != null && text.length() > 0 && !toldWrong) {
                    toldWrong = true;          // not our code: say so once
                    main.post(() -> hint.setText("这不是 AirLink 的码"));
                }
            } catch (Exception ignored) {
            } finally {
                decoding = false;
            }
        }, "airlink-qr").start();
    }

    /**
     * Rotates an NV21 buffer clockwise by 0/90/180/270 degrees.
     * The Y plane is full resolution; the interleaved VU plane is half in both.
     */
    private Result decodeMagnified(byte[] yuv, int w, int h,
                                   Map<DecodeHintType, Object> hints) {
        try {
            int cw = w / 2, ch = h / 2;
            int ox = (w - cw) / 2, oy = (h - ch) / 2;
            int zw = cw * 2, zh = ch * 2;
            byte[] zoom = new byte[zw * zh * 3 / 2];
            for (int y = 0; y < zh; y++) {
                int srow = (oy + y / 2) * w + ox;
                int drow = y * zw;
                for (int x = 0; x < zw; x++) zoom[drow + x] = yuv[srow + x / 2];
            }
            for (int i = zw * zh; i < zoom.length; i++) zoom[i] = (byte) 128;
            PlanarYUVLuminanceSource src =
                    new PlanarYUVLuminanceSource(zoom, zw, zh, 0, 0, zw, zh, false);
            return reader.decode(new BinaryBitmap(new HybridBinarizer(src)), hints);
        } catch (Exception e) {
            return null;
        }
    }

    static byte[] rotateYuv(byte[] src, int w, int h, int deg) {
        if (deg == 0) return src;
        byte[] out = new byte[src.length];
        if (deg == 180) {
            for (int i = 0; i < w * h; i++) out[i] = src[w * h - 1 - i];
            int cw = w / 2, ch = h / 2;
            int off = w * h;
            for (int i = 0; i < cw * ch; i++) {
                int s = off + i * 2;
                int t = off + (cw * ch - 1 - i) * 2;
                out[t] = src[s];
                out[t + 1] = src[s + 1];
            }
            return out;
        }
        // 90 or 270 (clockwise)
        int nw = h, nh = w;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int dx, dy;
                if (deg == 90) { dx = h - 1 - y; dy = x; }     // clockwise
                else { dx = y; dy = w - 1 - x; }               // counter-clockwise
                out[dy * nw + dx] = src[y * w + x];
            }
        }
        int cw = w / 2, ch = h / 2;                            // chroma dims
        int nwc = nh / 2, nhc = nw / 2;
        int off = w * h;
        for (int y = 0; y < ch; y++) {
            for (int x = 0; x < cw; x++) {
                int dx, dy;
                if (deg == 90) { dx = ch - 1 - y; dy = x; }
                else { dx = y; dy = cw - 1 - x; }
                int s = off + (y * cw + x) * 2;
                int t = off + (dy * nwc + dx) * 2;
                out[t] = src[s];
                out[t + 1] = src[s + 1];
            }
        }
        return out;
    }

    private void startHints() {
        new Thread(() -> {
            try { Thread.sleep(18000); } catch (InterruptedException e) { return; }
            if (!done && !toldWrong && framesSeen == 0) {
                main.post(() -> hint.setText("相机没有画面，请检查相机权限"));
            } else if (!done && !toldWrong) {
                main.post(() -> hint.setText("识别中，靠近一点或调亮电脑屏幕"));
            }
        }, "airlink-scanhint").start();
    }

    private void finishWith(String uri) {
        if (done) return;
        done = true;
        // airlink://IP:PORT?n=NAME
        try {
            String rest = uri.substring(uri.indexOf("airlink://") + "airlink://".length()).trim();
            String name = null;
            String code = null;
            int q = rest.indexOf('?');
            if (q >= 0) {
                String query = rest.substring(q + 1);
                rest = rest.substring(0, q);
                for (String kv : query.split("&")) {
                    int eq = kv.indexOf('=');
                    if (eq <= 0) continue;
                    String k = kv.substring(0, eq);
                    if (k.equals("n")) name = java.net.URLDecoder.decode(kv.substring(eq + 1), "UTF-8");
                    if (k.equals("code")) code = java.net.URLDecoder.decode(kv.substring(eq + 1), "UTF-8");
                }
            }
            String host;
            int port = Net.TCP_PORT;
            if (rest.startsWith("[")) {                 // [v6]:port
                int rb = rest.indexOf(']');
                host = rest.substring(1, rb);
                if (rb + 1 < rest.length() && rest.charAt(rb + 1) == ':') {
                    port = Integer.parseInt(rest.substring(rb + 2));
                }
            } else if (rest.indexOf(':') != rest.lastIndexOf(':')) {
                host = rest;                            // bare IPv6 literal
            } else {
                int colon = rest.lastIndexOf(':');
                if (colon > 0) {
                    host = rest.substring(0, colon);
                    port = Integer.parseInt(rest.substring(colon + 1));
                } else {
                    host = rest;
                }
            }
            final String fHost = host;
            final int fPort = port;
            final String fName = name;
            final String fCode = code;
            main.post(() -> {
                Intent out = new Intent();
                out.putExtra("host", fHost);
                out.putExtra("port", fPort);
                out.putExtra("name", fName);
                out.putExtra("code", fCode);
                setResult(RESULT_OK, out);
                finish();
            });
        } catch (Exception e) {
            main.post(() -> { hint.setText("二维码格式不对，请扫描 AirLink 的码"); });
            done = false;
        }
    }

    private void fail(String msg) {
        main.post(() -> { if (hint != null) hint.setText(msg); });
    }

    private void releaseCamera() {
        if (camera != null) {
            try { camera.setPreviewCallback(null); } catch (Exception ignored) {}
            try {
                Camera.Parameters p = camera.getParameters();
                p.setFlashMode(Camera.Parameters.FLASH_MODE_OFF);
                camera.setParameters(p);
            } catch (Exception ignored) {}
            try { camera.stopPreview(); } catch (Exception ignored) {}
            try { camera.release(); } catch (Exception ignored) {}
            camera = null;
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        releaseCamera();
    }
}
