package com.airlink;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.EditText;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class MainActivity extends Activity implements Ui.OnAction {

    private Ui ui;
    private Net.Peer selectedPeer;
    private final List<Net.Peer> discovered = new ArrayList<>();
    private Net.Peer manualPeer;              // last manual/QR connection
    private final List<Net.Peer> peerList = new ArrayList<>();
    private final List<Net.RemoteEntry> remoteEntries = new ArrayList<>();
    private final List<Hist> history = new ArrayList<>();
    private String remotePath = "/";
    private final SimpleDateFormat clock = new SimpleDateFormat("HH:mm", Locale.US);
    private Thread heartbeat;

    private static final int REQ_PICK = 1001;
    private static final int REQ_QR = 1002;
    private static final int REQ_PERM = 1003;
    private static final int REQ_MANAGE = 1004;
    private static final int REQ_DIR = 1005;
    private static final int REQ_NOTIF = 1006;

    /** One history entry: keeps enough context to delete the real file later. */
    private static final class Hist {
        String id = "", name, realName, dir, state, time, localPath, peerIp;
        int peerPort;
        boolean incoming;
        long size;
    }

    private static String newId() {
        return Long.toHexString(System.currentTimeMillis()) + "-" + Integer.toHexString((int) (Math.random() * 0xFFFFFF));
    }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setStatusBarColor(0xFFF2F5FA);
        getWindow().setNavigationBarColor(0xFFFFFFFF);
        // light canvas means the system icons have to turn dark, otherwise the
        // clock and battery are white-on-white
        if (Build.VERSION.SDK_INT >= 23) {
            View dv = getWindow().getDecorView();
            dv.setSystemUiVisibility(dv.getSystemUiVisibility() | View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR);
        }
        if (Build.VERSION.SDK_INT >= 26) {
            View dv = getWindow().getDecorView();
            dv.setSystemUiVisibility(dv.getSystemUiVisibility() | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        }

        Paths.init(this);
        Net.init(this);
        Net.setNotifier(new Sync());
        Net.setIdentity(Store.selfId(this), Store.deviceName(this));

        ui = new Ui(this);
        ui.setCallback(this);
        setContentView(ui);

        // keep our header clear of the system status bar
        final View root = ui;
        root.post(() -> {
            int inset = 0;
            if (Build.VERSION.SDK_INT >= 23) {
                android.view.WindowInsets wi = root.getRootWindowInsets();
                if (wi != null) inset = wi.getSystemWindowInsetTop();
            }
            if (inset <= 0) {
                int id = getResources().getIdentifier("status_bar_height", "dimen", "android");
                if (id > 0) inset = getResources().getDimensionPixelSize(id);
            }
            ui.setStatusInset(inset);
        });

        restoreLastPeer();

        Net.setDiscoverListener(list -> ui.post(() -> {
            discovered.clear();
            discovered.addAll(list);
            rebuildPeers();
        }));

        Net.setIncomingListener(new Net.IncomingListener() {
            @Override public boolean onOffer(String ip, String[] names, long total) {
                return true;
            }
            @Override public void onProgress(String name, long done, long total) {
                ui.post(() -> ui.setProgress("接收中 " + name,
                        total <= 0 ? 0 : (float) ((double) done / total)));
            }
            @Override public void onFinished(String path) {
                File f = new File(path);
                ui.post(() -> {
                    ui.setProgress(null, -1);
                    ui.setStatus("已接收：" + f.getName());
                    addHistory(f.getName(), true, "完成", "接收 · " + Net.fmtSize(f.length()),
                            f.getAbsolutePath(), null, 0, f.length());
                });
            }
            @Override public void onFailed(String reason) {
                ui.post(() -> {
                    ui.setProgress(null, -1);
                    ui.setStatus("接收失败：" + reason);
                });
            }
        });

        loadHistoryFromDisk();
        Net.start();
        updateSelf();
        applyKeepAwake();
        startHeartbeat();
        startKeepAlive();
        handleShare(getIntent());
    }

    /**
     * Battery optimisation is what actually freezes the app when the screen
     * goes off: it stops the broadcast and the listener, so the PC loses the
     * device. Ask once for an exemption.
     */
    private boolean forceBatteryDialog = false;

    boolean batteryOptimized() {
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            return pm == null || !pm.isIgnoringBatteryOptimizations(getPackageName());
        } catch (Exception e) {
            return false;
        }
    }

    @Override public void onBatterySetting() {
        forceBatteryDialog = true;
        requestBatteryExemption();
        forceBatteryDialog = false;
    }

    private void requestBatteryExemption() {
        if (Build.VERSION.SDK_INT < 23) return;
        try {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            if (pm == null || pm.isIgnoringBatteryOptimizations(getPackageName())) return;
            if (Store.askedBattery(this) && !forceBatteryDialog) return;
            Store.setAskedBattery(this);
            Dlg.card(this, "让连接在锁屏后保持",
                    "系统会在锁屏后限制后台程序，导致电脑找不到这台手机。\n"
                            + "把 AirLink 加入「不优化」名单后，锁屏也能继续接收和浏览。",
                    null, "去设置", () -> {
                        try {
                            Intent i = new Intent(android.provider.Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS);
                            i.setData(Uri.parse("package:" + getPackageName()));
                            startActivity(i);
                        } catch (Exception e) {
                            try {
                                startActivity(new Intent(android.provider.Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
                            } catch (Exception ignored) {}
                        }
                    }, "以后再说", null).show();
        } catch (Exception ignored) {}
    }

    /** Foreground service + wake locks so the link survives screen-off. */
    private void startKeepAlive() {
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"}, REQ_NOTIF);
        }
        try { KeepAliveService.start(this); } catch (Exception ignored) {}
        requestBatteryExemption();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleShare(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateSelf();
        applyKeepAwake();
        if (ui != null && ui.page() == Ui.PAGE_FILES && selectedPeer != null && remoteEntries.isEmpty()) {
            loadDir(remotePath);
        }
        ui.invalidate();
    }

    /** Bring the previous device back as a "继续连接" row after a restart. */
    private void restoreLastPeer() {
        String[] parts = Store.lastPeerParts(this);
        if (parts == null) return;
        try {
            Net.Peer p = new Net.Peer();
            p.id = "last-" + parts[0] + ":" + parts[1];
            String nm = Store.lastPeerName(this);
            p.name = nm.isEmpty() ? parts[0] : nm;
            p.ip = parts[0];
            p.port = Integer.parseInt(parts[1]);
            p.lastSeen = System.currentTimeMillis();
            p.linkDown = true;
            manualPeer = p;
            rebuildPeers();
        } catch (Exception ignored) {}
    }

    private void updateSelf() {
        List<String> addrs = Net.localAddresses();
        String addr = addrs.isEmpty() ? "" : addrs.get(0) + ":" + Net.TCP_PORT;
        ui.setSelf(Store.deviceName(this), addr);
        if (history.isEmpty()) ui.setStatus("");
        ui.setAbout(aboutText());
    }

    private String aboutText() {
        return "程序大小|约 297 KB，单 APK，无第三方运行库\n"
                + "支持系统|Android 5.0 及以上\n"
                + "传输端口|" + Net.TCP_PORT + "（TCP）· 45679（UDP 发现）\n"
                + "传输方式|局域网点对点直连，文件不经过任何服务器\n"
                + "电脑文件|浏览所连电脑的整个磁盘，按需复制\n"
                + "扫码连接|扫描电脑首页的二维码即可连上\n"
                + "接收目录|" + Paths.incomingDir().getAbsolutePath() + "\n"
                + "\n"
                + "协议与电脑版完全一致：UDP 广播发现，TCP 承载推送 / 列目录 / 取文件 / 心跳 / 断开 / 删除。";
    }

    // ------------------------------------------------------------------ peers
    /**
     * One row per device. A manual/QR connection is kept even when the same
     * machine is also discovered by broadcast, otherwise the same PC shows up
     * twice and the extra row does nothing useful.
     */
    private void rebuildPeers() {
        List<Net.Peer> merged = new ArrayList<>(discovered);
        if (manualPeer != null) {
            boolean dup = false;
            for (Net.Peer p : merged) {
                boolean sameAddr = p.ip != null && p.ip.equals(manualPeer.ip) && p.port == manualPeer.port;
                // a device may be seen on a different address than the one it was
                // reached on; the name is the stable identifier the user knows
                boolean sameName = p.name != null && p.name.equals(manualPeer.name)
                        && p.port == manualPeer.port;
                if (sameAddr || sameName) {
                    manualPeer = null;   // the discovered copy wins (live link state)
                    dup = true;
                    break;
                }
            }
            if (!dup) merged.add(0, manualPeer);
        }
        peerList.clear();
        peerList.addAll(merged);
        if (selectedPeer != null) {
            for (Net.Peer p : peerList) {
                if (p.key().equals(selectedPeer.key())) {
                    selectedPeer.linkDown = p.linkDown;
                    break;
                }
            }
        }
        refreshPeerRows();
    }

    private void refreshPeerRows() {
        List<Ui.Row> rows = new ArrayList<>();
        for (Net.Peer p : peerList) {
            Ui.Row r = new Ui.Row();
            r.title = p.name;
            boolean down = p.linkDown;
            // connected = an agreed session, or (fallback) the peer the user
            // picked when no session handshake has completed yet
            boolean here = !down && (Net.isSession(p)
                    || (Net.session() == null && selectedPeer != null
                        && selectedPeer.key().equals(p.key())));
            r.sub = p.ip + ":" + p.port + (p.usb ? " · USB" : "");
            r.selected = here;
            r.off = down;
            // previously used device: offer a one-tap resume, even if it dropped
            r.lastUsed = !here && Store.isLastPeer(this, p.ip, p.port);
            rows.add(r);
        }
        ui.setPeers(rows);
        // link state is already visible on the row itself; only refresh the
        // status line when the *selected* peer dropped, and never from the
        // file browser where it would read as a browse error
        if (selectedPeer != null && selectedPeer.linkDown && ui.page() == Ui.PAGE_SEND) {
            ui.setStatus("与 " + selectedPeer.name + " 的连接已断开");
        }
    }

    @Override public void onPeer(int index) {
        if (index < 0 || index >= peerList.size()) return;
        final Net.Peer p = peerList.get(index);
        if (Net.isSession(p)) {
            // already connected: just open its files
            selectedPeer = p;
            remotePath = "/";
            remoteEntries.clear();
            ui.goPage(Ui.PAGE_FILES);
            loadDir("/");
            return;
        }
        final String myId = Store.selfId(this);
        final String myName = Store.deviceName(this);
        ui.setStatus("正在连接 " + p.name + " …");
        new Thread(() -> {
            boolean ok = Net.sessionConnect(p, myId, myName, Net.TCP_PORT);
            ui.post(() -> {
                if (ok) Net.setSession(p);
                selectedPeer = p;
                p.linkDown = false;
                Store.setLastPeer(this, p.ip, p.port, p.name);
                deviceLogAdd(p.name, p.ip, p.port);
                refreshPeerRows();
                ui.setStatus("已连接 " + p.name);
                syncAfterConnect(p);
                ui.goPage(Ui.PAGE_FILES);
                loadDir("/");
            });
        }, "airlink-conn").start();
    }

    @Override public void onPeerDisconnect(int index) {
        if (index < 0 || index >= peerList.size()) return;
        final Net.Peer p = peerList.get(index);
        // drop the session first: browsing stops immediately on both ends
        Net.clearSession();
        new Thread(() -> Net.sayBye(p)).start();
        p.linkDown = true;
        if (selectedPeer != null && selectedPeer.key().equals(p.key())) selectedPeer = null;
        refreshPeerRows();
        ui.setStatus("已断开与 " + p.name + " 的连接");
    }

    /**
     * Two jobs in one loop:
     *  - keep the Wi-Fi radio awake so the PC can still reach us with the
     *    screen off (an idle radio stops answering incoming connections)
     *  - notice when the peer really went away
     */
    private void startHeartbeat() {
        if (heartbeat != null) return;
        heartbeat = new Thread(() -> {
            while (true) {
                Net.sleep(8000);
                Net.Peer target = Net.session();
                if (target == null) target = selectedPeer;
                if (target == null) {
                    String[] last = Store.lastPeerParts(this);
                    if (last != null) {
                        Net.Peer p = new Net.Peer();
                        p.ip = last[0];
                        try { p.port = Integer.parseInt(last[1]); } catch (Exception ignored) {}
                        target = p;
                    }
                }
                if (target == null) continue;
                Net.keepWarm(target);
            }
        }, "airlink-hb");
        heartbeat.setDaemon(true);
        heartbeat.start();

        // the visual "disconnected" check stays on its own faster loop
        new Thread(() -> {
            while (true) {
                Net.sleep(2500);
                Net.Peer probe = selectedPeer;
                if (probe == null) continue;
                boolean was = probe.linkDown;
                Net.ping(probe);
                if (was != probe.linkDown) ui.post(this::refreshPeerRows);
            }
        }, "airlink-hb2").start();
    }

    // ------------------------------------------------------------------ files
    @Override public void onNav(int page) {

        if (page == Ui.PAGE_FILES) {
            if (selectedPeer == null) {
                ui.setFileState(2, "/", "", "还没有连接电脑。先在「快传」里选一台电脑，或者扫码连接。");
                ui.setFiles(new ArrayList<>());
                return;
            }
            loadDir(remotePath);
        } else if (page == Ui.PAGE_SET) {
            refreshSettings();
        } else if (page == Ui.PAGE_HISTORY) {
            renderHistory();
        }
    }

    @Override public void onFileEnter(int index) {
        if (index < 0 || index >= remoteEntries.size()) return;
        Net.RemoteEntry e = remoteEntries.get(index);
        if (!e.isDir) { showFileInfo(e); return; }
        loadDir(e.path);
    }

    @Override public void onFileUp() {
        if (selectedPeer == null) { ui.goPage(Ui.PAGE_FILES); return; }
        String p = remotePath.replace('\\', '/');
        while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
        if (p.equals("/")) {
            // at the volume list already: bounce to 快传 so the operator is not stuck
            ui.setHint("已经在最顶层了");
            return;
        }
        String parent = Paths.displayPath(p).equals("/") ? "/" : parentOf(p);
        loadDir(parent);
    }

    private static String parentOf(String p) {
        while (p.length() > 1 && p.endsWith("/")) p = p.substring(0, p.length() - 1);
        int slash = p.lastIndexOf('/');
        if (slash <= 0) return "/";
        return p.substring(0, slash);
    }

    @Override public void onFileRefresh() {
        if (selectedPeer != null) loadDir(remotePath);
    }

    @Override public void onFileCopy(int index) {
        if (selectedPeer == null) { toast("请先在「快传」里选择一台电脑"); return; }
        if (index < 0 || index >= remoteEntries.size()) return;
        final Net.RemoteEntry e = remoteEntries.get(index);
        if (e.isDir) return;

        // Files are meant to land in a shallow, easy-to-find folder. On Android 11+
        // that needs "all files access"; ask once, then fall back quietly.
        if (Build.VERSION.SDK_INT >= 30 && !Paths.hasAllFilesAccess(this) && !Store.askedStorage(this)) {
            Store.setAskedStorage(this);
            new AlertDialog.Builder(this)
                    .setTitle("保存到哪里？")
                    .setMessage("为了把文件放到手机根目录下的 AirLink 文件夹（而不是一层层藏进去），"
                            + "需要授予「所有文件访问」权限。\n\n不授权也能用，文件会存到应用私有目录。")
                    .setPositiveButton("去授权", (d, w) -> onRequestStoragePerm())
                    .setNegativeButton("就这样保存", (d, w) -> doCopy(e))
                    .show();
            return;
        }
        doCopy(e);
    }

    private void doCopy(final Net.RemoteEntry e) {
        final File dir = Paths.incomingDir();
        if (!dir.exists()) dir.mkdirs();
        final File dst = Paths.uniqueFile(dir, e.name);
        ui.setProgress("复制 " + e.name, 0);
        ui.setHint("正在复制 " + e.name + "…");
        new Thread(() -> {
            final long[] progress = {0};
            final boolean[] stop = {false};
            Thread reporter = new Thread(() -> {
                while (!stop[0]) {
                    final long got = progress[0];
                    ui.post(() -> ui.setProgress("复制 " + e.name + "  " + Net.fmtSize(got),
                            e.size <= 0 ? 0 : (float) ((double) got / e.size)));
                    Net.sleep(120);
                }
            });
            reporter.setDaemon(true);
            reporter.start();

            String[] err = new String[1];
            boolean ok = Net.remoteGet(selectedPeer, e.path, dst, progress, err);
            stop[0] = true;
            ui.post(() -> {
                ui.setProgress(null, -1);
                if (ok) {
                    ui.setStatus("已复制：" + dst.getName());
                    ui.setHint("已保存到 " + dst.getAbsolutePath());
                    addHistory(dst.getName(), true, "完成", "从电脑复制 · " + Net.fmtSize(dst.length()),
                            dst.getAbsolutePath(), null, 0, dst.length());
                } else {
                    String m = err[0] == null ? "未知错误" : err[0];
                    ui.setStatus("复制失败：" + m);
                    ui.setHint("复制失败：" + m);
                }
            });
        }, "airlink-get").start();
    }

    private void loadDir(final String path) {
        if (selectedPeer == null) {
            ui.setFileState(2, path, "", "还没有连接电脑");
            ui.setFiles(new ArrayList<>());
            return;
        }
        ui.setFileState(1, path, selectedPeer.name, "");
        new Thread(() -> {
            String[] err = new String[1];
            List<Net.RemoteEntry> list = Net.remoteList(selectedPeer, path, err);
            ui.post(() -> {
                remotePath = path;
                remoteEntries.clear();
                if (list == null) {
                    ui.setFileState(2, path, selectedPeer.name, err[0]);
                    ui.setFiles(new ArrayList<>());
                    return;
                }
                remoteEntries.addAll(list);
                List<Ui.Row> rows = new ArrayList<>();
                for (Net.RemoteEntry e : list) {
                    Ui.Row r = new Ui.Row();
                    r.title = e.name;
                    r.dir = e.isDir;
                    r.sub = e.isDir ? "文件夹" : Net.fmtSize(e.size);
                    rows.add(r);
                }
                ui.setFiles(rows);
                ui.setFileState(3, path, selectedPeer.name, "");
            });
        }, "airlink-list").start();
    }

    private void showFileInfo(Net.RemoteEntry e) {
        String msg = "名称：" + e.name + "\n大小：" + Net.fmtSize(e.size)
                + "\n位置：" + e.path + "\n修改时间：" + clock.format(new Date(e.mtime));
        new AlertDialog.Builder(this)
                .setTitle("文件信息")
                .setMessage(msg)
                .setPositiveButton("复制到手机", (d, w) -> onFileCopy(remoteEntries.indexOf(e)))
                .setNegativeButton("关闭", null)
                .show();
    }

    // ------------------------------------------------------------------ mirror
    private Thread mirrorThread;
    private volatile boolean mirrorOn = false;

    private void startMirror() {
        if (mirrorOn) return;
        mirrorOn = true;
        mirrorThread = new Thread(() -> {
            final android.graphics.BitmapFactory.Options opt = new android.graphics.BitmapFactory.Options();
            opt.inPreferredConfig = android.graphics.Bitmap.Config.RGB_565;   // half the work to draw
            while (mirrorOn) {
                Net.Peer p = Net.session() != null ? Net.session() : selectedPeer;
                if (p == null) {
                    ui.post(() -> ui.setMirrorMsg("先在「快传」里连接电脑"));
                    Net.sleep(700);
                    continue;
                }
                final boolean[] gotOne = {false};
                Net.streamFrames(p, Store.mirrorWidth(this), (jpeg, w, h) -> {
                    if (!mirrorOn) return false;
                    Bitmap bmp = BitmapFactory.decodeByteArray(jpeg, 0, jpeg.length, opt);
                    if (bmp == null) return true;
                    gotOne[0] = true;
                    // Hand over the newest frame only: posting every frame made
                    // the UI queue grow whenever drawing was slower than the
                    // stream, which reads as lag that gets worse over time.
                    ui.offerMirrorFrame(bmp);
                    return mirrorOn;
                });
                if (!mirrorOn) break;
                ui.post(() -> ui.setMirrorMsg(gotOne[0] ? "画面已断开，正在重连…" : "读取画面失败，正在重试…"));
                Net.sleep(gotOne[0] ? 300 : 800);
            }
        }, "airlink-mirror");
        mirrorThread.setDaemon(true);
        mirrorThread.start();
    }

    /** Immersive fullscreen while mirroring. */
    private void setMirrorImmersive(boolean on) {
        try {
            android.view.View dv = getWindow().getDecorView();
            if (on) {
                getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                dv.setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
                        | android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
                ui.setMirrorFullscreen(true);
            } else {
                getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                dv.setSystemUiVisibility(android.view.View.SYSTEM_UI_FLAG_VISIBLE);
                ui.setMirrorFullscreen(false);
            }
        } catch (Exception ignored) {}
    }

    private void stopMirror() {
        mirrorOn = false;
        mirrorThread = null;
    }

    @Override public void onMirrorTap(int nx, int ny) {
        final Net.Peer p = Net.session() != null ? Net.session() : selectedPeer;
        if (p == null) return;
        new Thread(() -> {
            Net.sendInput(p, 1, nx, ny, 0);   // button down
            Net.sleep(40);
            Net.sendInput(p, 2, nx, ny, 0);   // button up
        }, "airlink-input").start();
    }

    // ------------------------------------------------------------------ sync
    /** Bridges Net's wire format to the local record store. */
    private final class Sync implements Net.Notify {
        @Override public List<Net.HistRec> historySnapshot() {
            List<Net.HistRec> out = new ArrayList<>();
            for (Hist h : history) out.add(toRec(h));
            return out;
        }

        @Override public void mergeHistory(List<Net.HistRec> recs) {
            ui.post(() -> {
                for (Net.HistRec r : recs) {
                    boolean replaced = false;
                    for (Hist h : history) {
                        if (h.id.equals(r.id)) {
                            fromRec(h, r);
                            replaced = true;
                            break;
                        }
                    }
                    if (!replaced) history.add(fromRec(new Hist(), r));
                }
                while (history.size() > 200) history.remove(history.size() - 1);
                saveHistoryToDisk();
                renderHistory();
                ui.invalidate();
            });
        }

        @Override public void deleteHistory(String id) {
            ui.post(() -> {
                for (int i = history.size() - 1; i >= 0; i--) {
                    if (history.get(i).id.equals(id)) history.remove(i);
                }
                saveHistoryToDisk();
                renderHistory();
                ui.invalidate();
            });
        }

        @Override public void sessionChanged() {
            ui.post(() -> {
                refreshPeerRows();
                ui.invalidate();
            });
        }
    }

    private Net.HistRec toRec(Hist h) {
        Net.HistRec r = new Net.HistRec();
        r.id = h.id;
        r.name = nz(h.name);
        r.size = Net.fmtSize(h.size);
        r.dir = h.incoming ? "in" : "out";
        r.state = nz(h.state);
        r.time = nz(h.time);
        r.realName = nz(h.realName == null ? h.name : h.realName);
        r.peerIp = nz(h.peerIp);
        r.port = h.peerPort;
        return r;
    }

    private Hist fromRec(Hist h, Net.HistRec r) {
        h.id = r.id == null || r.id.isEmpty() ? newId() : r.id;
        h.name = r.name;
        h.realName = r.realName == null || r.realName.isEmpty() ? r.name : r.realName;
        h.incoming = "in".equals(r.dir);
        h.state = r.state;
        h.time = r.time;
        h.peerIp = r.peerIp;
        h.peerPort = r.port;
        try { h.size = Long.parseLong(r.size.replaceAll("[^0-9]", "")); } catch (Exception e) { h.size = 0; }
        if (h.localPath == null) {
            h.localPath = h.incoming ? new File(Paths.incomingDir(), h.name).getAbsolutePath() : "";
        }
        return h;
    }

    private void syncAfterConnect(final Net.Peer p) {
        new Thread(() -> {
            List<Net.HistRec> mine = new Sync().historySnapshot();
            Net.histPush(p, mine);
            List<Net.HistRec> theirs = Net.histPull(p);
            if (!theirs.isEmpty()) new Sync().mergeHistory(theirs);
        }, "airlink-sync").start();
    }

    // ------------------------------------------------------------------ devices
    private File deviceFile() { return new File(getFilesDir(), "devices.txt"); }

    /** name \t ip \t port \t timestamp, one line per successful connection. */
    private void deviceLogAdd(String name, String ip, int port) {
        try (OutputStreamWriter w = new OutputStreamWriter(
                new FileOutputStream(deviceFile(), true), "UTF-8")) {
            w.write(nz(name) + "\t" + nz(ip) + "\t" + port + "\t" + deviceStamp() + "\n");
        } catch (Exception ignored) {}
    }

    private static String deviceStamp() {
        SimpleDateFormat f = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);
        return f.format(new Date());
    }

    /** Group the log by device: name + ip -> list of timestamps. */
    static final class DeviceGroup {
        String name, ip;
        final List<String> stamps = new ArrayList<>();
    }

    private List<DeviceGroup> loadDeviceGroups() {
        List<DeviceGroup> out = new ArrayList<>();
        File f = deviceFile();
        if (!f.isFile()) return out;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(
                new java.io.FileInputStream(f), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                if (line.length() > 0 && line.charAt(0) == '\uFEFF') line = line.substring(1);
                String[] p = line.split("\t", -1);
                if (p.length < 4) continue;
                DeviceGroup g = null;
                for (DeviceGroup d : out) {
                    if (d.name.equals(p[0]) && d.ip.equals(p[1])) { g = d; break; }
                }
                if (g == null) {
                    g = new DeviceGroup();
                    g.name = p[0];
                    g.ip = p[1];
                    out.add(g);
                }
                g.stamps.add(p[3]);
            }
        } catch (Exception ignored) {}
        return out;
    }

    private void deviceLogRemove(String name, String ip) {
        List<DeviceGroup> all = loadDeviceGroups();
        try (OutputStreamWriter w = new OutputStreamWriter(
                new FileOutputStream(deviceFile(), false), "UTF-8")) {
            for (DeviceGroup d : all) {
                if (d.name.equals(name) && d.ip.equals(ip)) continue;
                for (String s : d.stamps) {
                    w.write(nz(d.name) + "\t" + nz(d.ip) + "\t0\t" + s + "\n");
                }
            }
        } catch (Exception ignored) {}
    }

    private void renderDevices() {
        List<Ui.Row> rows = new ArrayList<>();
        for (DeviceGroup g : loadDeviceGroups()) {
            Ui.Row r = new Ui.Row();
            r.title = g.name;
            String last = g.stamps.isEmpty() ? "" : g.stamps.get(g.stamps.size() - 1);
            r.sub = g.ip + " · 连接过 " + g.stamps.size() + " 次"
                    + (last.isEmpty() ? "" : " · 最近 " + last);
            r.right = g.ip;
            rows.add(r);
        }
        ui.setDevices(rows);
    }

    private void renderDeviceDetail(String key) {
        List<Ui.Row> rows = new ArrayList<>();
        for (DeviceGroup g : loadDeviceGroups()) {
            if (!g.ip.equals(key) && !g.name.equals(key)) continue;
            for (int i = g.stamps.size() - 1; i >= 0; i--) {
                Ui.Row r = new Ui.Row();
                r.title = g.stamps.get(i);
                rows.add(r);
            }
            break;
        }
        ui.setDeviceStamps(rows);
    }

    // ------------------------------------------------------------------ history
    private File historyFile() { return new File(getFilesDir(), "history.txt"); }

    private void loadHistoryFromDisk() {
        history.clear();
        File f = historyFile();
        if (!f.isFile()) { renderHistory(); return; }
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new java.io.FileInputStream(f), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) {
                String[] p = line.split("\t", -1);
                if (p.length < 9) continue;
                Hist h = new Hist();
                h.id = p[0];
                h.name = p[1];
                h.incoming = "in".equals(p[2]);
                h.state = p[3];
                h.time = p[4];
                h.localPath = p[5];
                h.realName = p[6];
                h.peerIp = p[7];
                try { h.peerPort = Integer.parseInt(p[8]); } catch (Exception ignored) {}
                try { h.size = Long.parseLong(p.length > 9 ? p[9] : "0"); } catch (Exception ignored) {}
                history.add(h);
            }
        } catch (Exception ignored) {}
        renderHistory();
    }

    private void saveHistoryToDisk() {
        try (OutputStreamWriter w = new OutputStreamWriter(new FileOutputStream(historyFile()), "UTF-8")) {
            for (Hist h : history) {
                w.write(h.id + "\t" + nz(h.name) + "\t" + (h.incoming ? "in" : "out") + "\t" + h.state
                        + "\t" + h.time + "\t" + nz(h.localPath) + "\t" + nz(h.realName)
                        + "\t" + nz(h.peerIp) + "\t" + h.peerPort + "\t" + h.size + "\n");
            }
        } catch (Exception ignored) {}
    }

    private static String nz(String s) { return s == null ? "" : s.replace('\t', ' ').replace('\n', ' '); }

    private void addHistory(String name, boolean incoming, String state, String sub,
                            String localPath, String peerIp, int peerPort, long size) {
        Hist h = new Hist();
        h.id = newId();
        h.name = name;
        h.realName = name;
        h.incoming = incoming;
        h.state = state;
        h.time = clock.format(new Date());
        h.localPath = localPath;
        h.peerIp = peerIp;
        h.peerPort = peerPort;
        h.size = size;
        history.add(0, h);
        if (history.size() > 60) history.remove(history.size() - 1);
        saveHistoryToDisk();
        renderHistory();
        // live sync: the other device sees this record at once
        final Net.Peer sp = Net.session();
        if (sp != null) {
            final Net.HistRec r = toRec(h);
            new Thread(() -> Net.histPush(sp, java.util.Collections.singletonList(r))).start();
        }
    }

    private void renderHistory() {
        List<Ui.Row> rows = new ArrayList<>();
        for (Hist h : history) {
            Ui.Row r = new Ui.Row();
            r.title = h.name;
            r.sub = (h.incoming ? "接收" : "发送") + " · " + h.time + (h.peerIp == null || h.peerIp.isEmpty() ? "" : " · " + h.peerIp);
            r.right = Net.fmtSize(h.size);
            r.dir = h.incoming;
            r.badge = "完成".equals(h.state) ? 1 : ("已取消".equals(h.state) ? 2 : 3);
            rows.add(r);
        }
        ui.setHistory(rows);
    }

    @Override public void onHistoryDelete(int index) {
        if (index < 0 || index >= history.size()) return;
        final Hist h = history.get(index);
        Dlg.card(this, "删除这条记录？",
                "是否删除？这将会删除本条记录和本地文件。\n\n" + h.name,
                null, "删除", () -> doDeleteHistory(h), "取消", null).show();
    }

    private void doDeleteHistory(final Hist h) {
        new Thread(() -> {
            boolean ok = true;
            String msg = null;
            if (h.incoming) {
                // we received it, so the copy is here
                if (h.localPath != null && !h.localPath.isEmpty()) {
                    File f = new File(h.localPath);
                    if (f.exists() && !f.delete()) { ok = false; msg = "文件删除失败，可能没有权限"; }
                }
            } else {
                // we sent it: the delivered copy sits on the PC
                Net.Peer p = findPeer(h.peerIp, h.peerPort);
                if (p == null) {
                    ok = false;
                    msg = "找不到那台电脑，无法删除对方文件";
                } else {
                    String[] err = new String[1];
                    ok = Net.remoteDelete(p, h.realName, err);
                    if (!ok) msg = err[0];
                }
            }
            final boolean fok = ok;
            final String fmsg = msg;
            ui.post(() -> {
                if (fok) {
                    history.remove(h);
                    if (history.isEmpty()) ui.setStatus("");
                    saveHistoryToDisk();
                    renderHistory();
                    final Net.Peer sp = Net.session();
                    if (sp != null && h.id != null && !h.id.isEmpty()) {
                        new Thread(() -> Net.histDel(sp, h.id)).start();
                    }
                    toast("已删除记录和对应的文件");
                } else {
                    toast("删除失败：" + (fmsg == null ? "未知错误" : fmsg));
                }
            });
        }, "airlink-del").start();
    }

    private Net.Peer findPeer(String ip, int port) {
        if (ip == null) return null;
        for (Net.Peer p : peerList) if (ip.equals(p.ip)) return p;
        if (selectedPeer != null && ip.equals(selectedPeer.ip)) return selectedPeer;
        return null;
    }

    @Override public void onHistoryOpen(int index) {
        if (index < 0 || index >= history.size()) return;
        Hist h = history.get(index);
        final String where;
        if (h.incoming) {
            where = h.localPath == null || h.localPath.isEmpty()
                    ? Paths.incomingDir().getAbsolutePath() : h.localPath;
        } else {
            where = "已发送到 " + (h.peerIp == null || h.peerIp.isEmpty() ? "对方电脑" : h.peerIp)
                    + " 的接收目录";
        }
        showDetailDialog(h, where);
    }

    /** Detail sheet: thumbnail (when we still have the file) plus where it landed. */
    private void showDetailDialog(Hist h, String where) {
        float dd = getResources().getDisplayMetrics().density;
        android.widget.LinearLayout box = new android.widget.LinearLayout(this);
        box.setOrientation(android.widget.LinearLayout.VERTICAL);

        Bitmap bmp = loadThumb(h.localPath);
        if (bmp != null) {
            android.widget.ImageView iv = new android.widget.ImageView(this);
            iv.setImageBitmap(bmp);
            iv.setScaleType(android.widget.ImageView.ScaleType.CENTER_CROP);
            iv.setAdjustViewBounds(true);
            android.widget.LinearLayout.LayoutParams il = new android.widget.LinearLayout.LayoutParams(
                    android.widget.LinearLayout.LayoutParams.MATCH_PARENT, (int) (150 * dd));
            box.addView(iv, il);
        }

        android.widget.TextView tv = new android.widget.TextView(this);
        tv.setText("类型：" + (h.incoming ? "接收" : "发送")
                + "\n大小：" + Net.fmtSize(h.size)
                + "\n状态：" + h.state
                + "\n时间：" + h.time
                + "\n\n" + (h.incoming ? "保存在：" : "传送到：") + "\n" + where);
        tv.setTextSize(13.5f);
        tv.setTextColor(Skin.TEXT);           // dark on the white card, readable
        tv.setLineSpacing(0, 1.3f);
        box.addView(tv, new android.widget.LinearLayout.LayoutParams(
                android.widget.LinearLayout.LayoutParams.MATCH_PARENT,
                android.widget.LinearLayout.LayoutParams.WRAP_CONTENT));

        Dlg.card(this, h.name, null, box, "知道了", null, null, null).show();
    }

    private Bitmap loadThumb(String path) {
        if (path == null || path.isEmpty()) return null;
        try {
            BitmapFactory.Options o = new BitmapFactory.Options();
            o.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(path, o);
            if (o.outWidth <= 0) return null;
            int scale = 1;
            while (o.outWidth / scale > 320) scale *= 2;
            BitmapFactory.Options o2 = new BitmapFactory.Options();
            o2.inSampleSize = scale;
            return BitmapFactory.decodeFile(path, o2);
        } catch (Exception e) {
            return null;
        }
    }

    // ------------------------------------------------------------------ settings
    private void refreshSettings() {
        ui.setBatteryOk(!batteryOptimized());
        List<Ui.Row> dirs = new ArrayList<>();
        String cur = Paths.incomingDir().getAbsolutePath();
        for (String s : Paths.suggestedDirs()) {
            Ui.Row r = new Ui.Row();
            r.title = s.equals(cur) ? "当前目录" : s.substring(s.lastIndexOf('/') + 1);
            if (r.title.isEmpty()) r.title = s;
            r.sub = s;
            r.selected = s.equals(cur);
            dirs.add(r);
        }
        ui.setSaveDirs(dirs);
        ui.setSelf(Store.deviceName(this), ui.selfAddr());
    }

    @Override public void onEditName() {
        Dlg.input(this, "设备名称", "给这台手机起个名字", Store.deviceName(this),
                "对方在设备列表里看到的就是这个名字", "保存", v -> {
                    if (v.isEmpty()) return;
                    Store.setDeviceName(this, v);
                    Net.setIdentity(Store.selfId(this), v);
                    updateSelf();
                    refreshSettings();
                });
    }

    @Override public void onToggleAllowBrowse() {
        Store.setAllowBrowse(this, !Store.allowBrowse(this));
        // Net re-reads the flag on every request, so this is effective at once
        toast(Store.allowBrowse(this) ? "电脑现在可以浏览本机文件" : "电脑已无法浏览本机文件");
        ui.invalidate();
    }

    @Override public void onKeepAwake() {
        Store.setKeepAwake(this, !Store.keepAwake(this));
        applyKeepAwake();
        toast(Store.keepAwake(this) ? "已保持唤醒，屏幕不会自动熄灭" : "已恢复系统的休眠策略");
        ui.invalidate();
    }

    private void applyKeepAwake() {
        if (Store.keepAwake(this)) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
    }

    @Override public void onOpenAbout() {
        ui.goPage(Ui.PAGE_ABOUT);
    }

    // ------------------------------------------------------------------ WAN
    private String wanAddr = "", wanCode = "";

    @Override public void onWanAddr() {
        Dlg.input(this, "电脑公网地址", "例如 1.2.3.4:45678", wanAddr, null, "确定", v -> {
            wanAddr = v.trim();
            ui.setWanFields(wanAddr, wanCode);
        });
    }

    @Override public void onWanCode() {
        Dlg.input(this, "配对码", "电脑 WAN 连接页显示的 6 位数字", wanCode, null, "确定", v -> {
            wanCode = v.trim();
            ui.setWanFields(wanAddr, wanCode);
        });
    }

    @Override public void onWanConnect() {
        if (wanAddr.isEmpty()) { toast("先填电脑的公网地址"); return; }
        String host = wanAddr;
        int port = 45678;
        if (host.startsWith("[")) {                 // [v6]:port
            int rb = host.indexOf(']');
            if (rb > 0) {
                String after = host.substring(rb + 1);
                host = host.substring(1, rb);
                if (after.startsWith(":")) {
                    try { port = Integer.parseInt(after.substring(1)); } catch (Exception ignored) {}
                }
            }
        } else if (host.indexOf(':') != host.lastIndexOf(':')) {
            // a bare IPv6 literal has several colons: keep it whole
        } else {
            int c = host.lastIndexOf(':');
            if (c > 0) {
                try { port = Integer.parseInt(host.substring(c + 1)); } catch (Exception ignored) {}
                host = host.substring(0, c);
            }
        }
        connectWan(host, port, wanCode);
    }

    private void connectWan(final String host, int port, final String code) {
        final String h = host;
        final int p = port;
        toast("正在连接 " + h + ":" + p + " …");
        new Thread(() -> {
            Net.Peer peer = new Net.Peer();
            peer.ip = h;
            peer.port = p;
            peer.name = h;
            if (!Net.auth(peer, code)) {
                ui.post(() -> toast("配对码不对，或电脑没有开端口映射"));
                return;
            }
            String myId = Store.selfId(this);
            boolean ok = Net.sessionConnect(peer, myId, Store.deviceName(this), Net.TCP_PORT);
            ui.post(() -> {
                if (ok) Net.setSession(peer);
                selectedPeer = peer;
                peer.linkDown = false;
                Store.setLastPeer(this, h, p, h);
                deviceLogAdd(h, h, p);
                refreshPeerRows();
                ui.setStatus("已连接 " + h);
                syncAfterConnect(peer);
                ui.goPage(Ui.PAGE_FILES);
                loadDir("/");
            });
        }, "airlink-wan").start();
    }

    @Override public void onOpenDevices() {
        renderDevices();
        ui.goPage(Ui.PAGE_DEVICES);
    }

    @Override public void onDeviceOpen(int index) {
        List<DeviceGroup> gs = loadDeviceGroups();
        if (index < 0 || index >= gs.size()) return;
        DeviceGroup g = gs.get(index);
        ui.setDeviceKey(g.name, g.ip);
        renderDeviceDetail(g.ip);
        ui.goPage(Ui.PAGE_DEVICE_DETAIL);
    }

    @Override public void onDeviceDelete(int index) {
        List<DeviceGroup> gs = loadDeviceGroups();
        if (index < 0 || index >= gs.size()) return;
        final DeviceGroup g = gs.get(index);
        Dlg.card(this, "删除这个设备？",
                "会删除「" + g.name + "」的全部 " + g.stamps.size() + " 条连接记录。",
                null, "删除", () -> {
                    deviceLogRemove(g.name, g.ip);
                    renderDevices();
                    toast("已删除设备记录");
                }, "取消", null).show();
    }

    @Override public void onDeviceDeleteAll() {
        List<DeviceGroup> gs = loadDeviceGroups();
        for (DeviceGroup g : gs) {
            String key = ui.deviceKey();
            if (key.contains(g.ip) || key.contains(g.name)) {
                final DeviceGroup hit = g;
                Dlg.card(this, "删除这个设备？",
                        "会删除「" + g.name + "」的全部 " + g.stamps.size() + " 条连接记录。",
                        null, "删除", () -> {
                            deviceLogRemove(hit.name, hit.ip);
                            renderDevices();
                            ui.goPage(Ui.PAGE_DEVICES);
                            toast("已删除设备记录");
                        }, "取消", null).show();
                return;
            }
        }
    }

    @Override public void onOpenSaveDirSettings() {
        refreshSettings();
        ui.goPage(Ui.PAGE_SAVEDIR);
    }

    @Override public void onSaveDirChosen(int index) {
        List<String> dirs = Paths.suggestedDirs();
        if (index < 0 || index >= dirs.size()) return;
        String s = dirs.get(index);
        File f = new File(s);
        if (!f.isDirectory() && !f.mkdirs()) { toast("无法创建这个目录"); return; }
        Store.setSaveDir(this, s);
        toast("已设为 " + s);
        refreshSettings();
        ui.invalidate();
    }

    @Override public void onSaveDirManual() {
        Dlg.input(this, "接收目录", "/storage/emulated/0/AirLink",
                Paths.incomingDir().getAbsolutePath(),
                "输入一个完整路径，文件会存到这里", "保存", v -> {
                    if (v.isEmpty()) return;
                    File f = new File(v);
                    if (!f.isDirectory() && !f.mkdirs()) { toast("无法创建这个目录"); return; }
                    Store.setSaveDir(this, v);
                    toast("已设为 " + v);
                    refreshSettings();
                    ui.invalidate();
                });
    }

    /** The hardware/gesture back key mirrors the in-app back arrow. */
    @Override
    public void onBackPressed() {
        if (ui == null) { super.onBackPressed(); return; }
        int p = ui.page();
        if (p == Ui.PAGE_ABOUT || p == Ui.PAGE_SAVEDIR) { ui.goPage(Ui.PAGE_SET); return; }
        if (p == Ui.PAGE_DEVICE_DETAIL) { ui.goPage(Ui.PAGE_DEVICES); return; }
        if (p == Ui.PAGE_DEVICES) { ui.goPage(Ui.PAGE_SET); return; }
        if (p == Ui.PAGE_FILES && selectedPeer != null && !"/".equals(remotePath)) {
            onFileUp();
            return;
        }
        if (p != Ui.PAGE_SEND) { ui.goPage(Ui.PAGE_SEND); return; }
        super.onBackPressed();
    }

    @Override public void onRequestStoragePerm() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                i.setData(Uri.parse("package:" + getPackageName()));
                startActivityForResult(i, REQ_MANAGE);
                return;
            } catch (Exception ignored) {}
            try {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQ_MANAGE);
                return;
            } catch (Exception ignored) {}
        }
        requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,
                Manifest.permission.WRITE_EXTERNAL_STORAGE}, REQ_PERM);
    }

    @Override public void onBack() {
        int p = ui.page();
        if (p == Ui.PAGE_DEVICE_DETAIL) { ui.goPage(Ui.PAGE_DEVICES); return; }
        ui.goPage(Ui.PAGE_SET);
    }

    // ------------------------------------------------------------------ actions
    @Override public void onScanQr() {
        if (!ensurePermission(Manifest.permission.CAMERA, REQ_PERM)) {
            toast("需要相机权限才能扫码");
            return;
        }
        try {
            startActivityForResult(new Intent(this, ScanActivity.class), REQ_QR);
        } catch (ActivityNotFoundException e) {
            toast("无法启动相机");
        }
    }

    @Override public void onManualIp() {
        List<String> addrs = Net.localAddresses();
        String prefill = "";
        if (!addrs.isEmpty()) {
            String a = addrs.get(0);
            prefill = a.substring(0, a.lastIndexOf('.') + 1);
        }
        String last = Store.lastPeerAddr(this);
        if (!last.isEmpty()) prefill = last;
        Dlg.input(this, "连接电脑", "192.168.1.7:45678", prefill,
                "在电脑的 AirLink 首页可以看到这个地址",
                "连接", s -> {
                    if (s.isEmpty()) return;
                    String host = s;
                    int port = Net.TCP_PORT;
                    int c = s.lastIndexOf(':');
                    if (c > 0) {
                        host = s.substring(0, c);
                        try { port = Integer.parseInt(s.substring(c + 1)); } catch (Exception ignored) {}
                    }
                    connectManual(host, port, null);
                });
    }

    @Override public void onPickFiles() {
        if (selectedPeer == null) { toast("请先选择一台电脑"); return; }
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        try {
            startActivityForResult(i, REQ_PICK);
        } catch (ActivityNotFoundException e) {
            toast("没有可用的文件选择器");
        }
    }

    // ------------------------------------------------------------------ share intent
    private void handleShare(Intent intent) {
        if (intent == null) return;
        String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action) && !Intent.ACTION_SEND_MULTIPLE.equals(action)) return;
        if (selectedPeer == null && !peerList.isEmpty()) selectedPeer = peerList.get(0);
        if (selectedPeer == null) { toast("请先选择一台电脑再分享文件"); return; }

        List<Uri> uris = new ArrayList<>();
        if (Intent.ACTION_SEND.equals(action)) {
            Uri u = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u != null) uris.add(u);
        } else {
            ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
            if (list != null) uris.addAll(list);
        }
        if (uris.isEmpty()) return;
        sendUris(uris);
    }

    private void sendUris(final List<Uri> uris) {
        ui.setProgress("准备发送…", 0);
        new Thread(() -> {
            List<File> files = new ArrayList<>();
            for (Uri u : uris) {
                File f = copyToCache(u);
                if (f != null) files.add(f);
            }
            if (files.isEmpty()) {
                ui.post(() -> { ui.setProgress(null, -1); toast("无法读取所选文件"); });
                return;
            }
            ui.post(() -> ui.setProgress("发送中…", 0));
            Net.sendFiles(selectedPeer, files, new Net.SendListener() {
                @Override public void onProgress(String name, long done, long total) {
                    ui.post(() -> ui.setProgress("发送 " + name + "  " + Net.fmtSize(done),
                            total <= 0 ? 0 : (float) ((double) done / total)));
                }
                @Override public void onFinished() {
                    ui.post(() -> {
                        ui.setProgress(null, -1);
                        ui.setStatus("已发送 " + files.size() + " 个文件");
                        for (File f : files) {
                            addHistory(f.getName(), false, "完成", "发送 · " + Net.fmtSize(f.length()),
                                    f.getAbsolutePath(),
                                    selectedPeer == null ? "" : selectedPeer.ip,
                                    selectedPeer == null ? 0 : selectedPeer.port, f.length());
                        }
                    });
                }
                @Override public void onFailed(String reason) {
                    ui.post(() -> {
                        ui.setProgress(null, -1);
                        ui.setStatus("发送失败：" + reason);
                        for (File f : files) {
                            addHistory(f.getName(), false, "失败", "发送 · " + reason,
                                    f.getAbsolutePath(),
                                    selectedPeer == null ? "" : selectedPeer.ip,
                                    selectedPeer == null ? 0 : selectedPeer.port, f.length());
                        }
                    });
                }
            });
        }, "airlink-prep").start();
    }

    private File copyToCache(Uri uri) {
        try {
            File dir = new File(getCacheDir(), "out");
            if (!dir.exists()) dir.mkdirs();
            String name = queryName(uri);
            File dst = new File(dir, name);
            InputStream in = getContentResolver().openInputStream(uri);
            if (in == null) return null;
            FileOutputStream out = new FileOutputStream(dst);
            try {
                byte[] buf = new byte[1 << 16];
                int r;
                while ((r = in.read(buf)) > 0) out.write(buf, 0, r);
                out.flush();
            } finally { in.close(); out.close(); }
            return dst;
        } catch (Exception e) {
            return null;
        }
    }

    private String queryName(Uri uri) {
        String n = null;
        try (android.database.Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) n = c.getString(idx);
            }
        } catch (Exception ignored) {}
        if (n == null || n.isEmpty()) n = "file_" + System.currentTimeMillis();
        return n.replace('/', '_');
    }

    // ------------------------------------------------------------------ results
    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_QR && res == RESULT_OK && data != null) {
            String host = data.getStringExtra("host");
            int port = data.getIntExtra("port", Net.TCP_PORT);
            String name = data.getStringExtra("name");
            String code = data.getStringExtra("code");
            if (host != null && code != null && !code.isEmpty()) {
                // WAN code: remember both fields and connect through the auth path
                wanAddr = host.indexOf(':') >= 0 ? ("[" + host + "]:" + port) : (host + ":" + port);
                wanCode = code;
                ui.setWanFields(wanAddr, wanCode);
                connectWan(host, port, code);
            } else if (host != null) {
                connectManual(host, port, name);
            }
        } else if (req == REQ_PICK && res == RESULT_OK && data != null) {
            List<Uri> uris = new ArrayList<>();
            if (data.getClipData() != null) {
                for (int i = 0; i < data.getClipData().getItemCount(); i++) {
                    uris.add(data.getClipData().getItemAt(i).getUri());
                }
            } else if (data.getData() != null) {
                uris.add(data.getData());
            }
            if (!uris.isEmpty()) sendUris(uris);
        } else if (req == REQ_MANAGE) {
            refreshSettings();
            ui.invalidate();
        }
    }

    private void connectManual(String host, int port, String name) {
        Net.Peer p = new Net.Peer();
        p.id = "manual-" + host + ":" + port;
        p.name = (name == null || name.isEmpty()) ? host : name;
        p.ip = host;
        p.port = port;
        p.lastSeen = System.currentTimeMillis();
        manualPeer = p;
        selectedPeer = p;
        Store.setLastPeer(this, host, port, p.name);
        remotePath = "/";
        remoteEntries.clear();
        rebuildPeers();
        ui.setStatus("正在连接 " + p.name + " …");
        final String myId = Store.selfId(this);
        final String myName = Store.deviceName(this);
        new Thread(() -> {
            // The session handshake also tells the other side to flip its list;
            // if it does not get through we still keep the local link so the
            // app stays usable.
            boolean ok = Net.sessionConnect(p, myId, myName, Net.TCP_PORT);
            ui.post(() -> {
                if (ok) Net.setSession(p);
                deviceLogAdd(p.name, host, port);
                refreshPeerRows();
                ui.setStatus("已连接到 " + p.name);
                syncAfterConnect(p);
                ui.goPage(Ui.PAGE_FILES);
                loadDir("/");
            });
        }, "airlink-conn").start();
    }

    // ------------------------------------------------------------------ permissions
    private boolean ensurePermission(String perm, int req) {
        if (checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED) return true;
        requestPermissions(new String[]{perm}, req);
        return false;
    }

    private void toast(String s) { Toast.makeText(this, s, Toast.LENGTH_SHORT).show(); }
}
