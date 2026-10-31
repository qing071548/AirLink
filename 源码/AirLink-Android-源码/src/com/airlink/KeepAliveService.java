package com.airlink;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/**
 * Keeps the sockets usable while the screen is off.
 *
 * Without this the system freezes the app once the phone locks, so the PC stops
 * seeing the device and an open transfer dies. A foreground service plus a
 * partial wake lock and a high-performance Wi-Fi lock is the standard fix.
 */
public class KeepAliveService extends Service {

    private static final String CHANNEL = "airlink_alive";
    private static final int NOTIF_ID = 1001;

    private PowerManager.WakeLock wake;
    private WifiManager.WifiLock wifi;

    public static void start(Context ctx) {
        try {
            Intent i = new Intent(ctx, KeepAliveService.class);
            if (Build.VERSION.SDK_INT >= 26) ctx.startForegroundService(i);
            else ctx.startService(i);
        } catch (Exception ignored) {
        }
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createChannel();
        Notification n = buildNotification();
        try {
            startForeground(NOTIF_ID, n);
        } catch (Exception ignored) {
        }

        registerScreenReceiver();
        try {
            PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
            if (pm != null) {
                wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AirLink:transfer");
                wake.setReferenceCounted(false);
                wake.acquire();
            }
        } catch (Exception ignored) {
        }

        try {
            acquireWifiLock();
        } catch (Exception ignored) {
        }
    }

    /** Try the strongest mode the OS will actually grant, then fall back. */
    private void acquireWifiLock() {
        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wm == null) return;
        int[] modes = Build.VERSION.SDK_INT >= 29
                ? new int[]{ WifiManager.WIFI_MODE_FULL_LOW_LATENCY,
                             WifiManager.WIFI_MODE_FULL_HIGH_PERF,
                             WifiManager.WIFI_MODE_FULL }
                : new int[]{ WifiManager.WIFI_MODE_FULL_HIGH_PERF, WifiManager.WIFI_MODE_FULL };
        for (int m : modes) {
            try {
                WifiManager.WifiLock l = wm.createWifiLock(m, "AirLink:wifi");
                l.setReferenceCounted(false);
                l.acquire();
                wifi = l;
                return;
            } catch (Exception ignored) {}
        }
    }

    /** Re-arm on screen changes: this is exactly when the system trims us. */
    public static class ScreenReceiver extends android.content.BroadcastReceiver {
        @Override public void onReceive(Context ctx, Intent intent) {
            try { start(ctx); } catch (Exception ignored) {}
        }
    }

    private ScreenReceiver screenReceiver;

    private void registerScreenReceiver() {
        try {
            screenReceiver = new ScreenReceiver();
            IntentFilter f = new IntentFilter();
            f.addAction(Intent.ACTION_SCREEN_OFF);
            f.addAction(Intent.ACTION_SCREEN_ON);
            f.addAction(Intent.ACTION_USER_PRESENT);
            try { registerReceiver(screenReceiver, f); } catch (Exception ignored) {}
        } catch (Exception ignored) {}
    }

    private void unregisterScreenReceiver() {
        try { if (screenReceiver != null) unregisterReceiver(screenReceiver); } catch (Exception ignored) {}
        screenReceiver = null;
    }
    private void createChannel() {
        if (Build.VERSION.SDK_INT < 26) return;
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (nm == null) return;
        NotificationChannel ch = new NotificationChannel(CHANNEL, "保持连接",
                NotificationManager.IMPORTANCE_MIN);
        ch.setDescription("锁屏后仍然保持 AirLink 的接收服务在线");
        ch.setShowBadge(false);
        ch.enableLights(false);
        ch.enableVibration(false);
        nm.createNotificationChannel(ch);
    }

    private Notification buildNotification() {
        Intent open = new Intent(this, MainActivity.class);
        open.setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP);
        int flags = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        PendingIntent pi = PendingIntent.getActivity(this, 0, open, flags);

        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) b = new Notification.Builder(this, CHANNEL);
        else b = new Notification.Builder(this);

        b.setContentTitle("AirLink 正在后台保持连接")
                .setContentText("锁屏后仍可接收文件")
                .setSmallIcon(R.mipmap.ic_launcher)
                .setOngoing(true)
                .setContentIntent(pi)
                .setPriority(Notification.PRIORITY_MIN);
        return b.build();
    }

    private final android.os.Handler beat = new android.os.Handler(android.os.Looper.getMainLooper());
    private final Runnable reArm = new Runnable() {
        @Override public void run() {
            // some ROMs release the wake lock when the process looks idle
            try {
                if (wake == null || !wake.isHeld()) {
                    PowerManager pm = (PowerManager) getSystemService(POWER_SERVICE);
                    if (pm != null) {
                        if (wake == null) wake = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "AirLink:transfer");
                        wake.setReferenceCounted(false);
                        wake.acquire();
                    }
                }
            } catch (Exception ignored) {}
            try {
                if (wifi == null || !wifi.isHeld()) {
                    if (wifi == null || !wifi.isHeld()) acquireWifiLock();
                }
            } catch (Exception ignored) {}
            beat.postDelayed(this, 60_000);
        }
    };

    private Thread warm;

    /**
     * The service owns the keep-warm ping: it must run even when the activity
     * is gone, and an outgoing packet is what keeps the Wi-Fi radio from
     * dropping into a state where inbound connections get no answer.
     */
    private void startKeepWarm() {
        if (warm != null) return;
        warm = new Thread(() -> {
            while (true) {
                try { Thread.sleep(9000); } catch (InterruptedException e) { return; }
                attemptWarm();
            }
        }, "airlink-warm");
        warm.setDaemon(true);
        warm.start();
    }

    private void attemptWarm() {
        String[] parts = Store.lastPeerParts(this);
        if (parts == null) return;
        Net.Peer p = new Net.Peer();
        p.ip = parts[0];
        try { p.port = Integer.parseInt(parts[1]); } catch (Exception e) { return; }
        Net.Peer sess = Net.session();
        if (sess != null) p = sess;
        boolean ok;
        java.net.Socket s = null;
        try {
            s = new java.net.Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 1500);
            ok = true;
        } catch (Exception e) {
            ok = false;
        } finally {
            try { if (s != null) s.close(); } catch (Exception ignored) {}
        }
        Store.setLastWarm(this, ok);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        beat.removeCallbacks(reArm);
        beat.postDelayed(reArm, 60_000);
        startKeepWarm();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        unregisterScreenReceiver();
        try { if (wake != null && wake.isHeld()) wake.release(); } catch (Exception ignored) {}
        try { if (wifi != null && wifi.isHeld()) wifi.release(); } catch (Exception ignored) {}
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
