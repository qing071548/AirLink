package com.airlink;

import android.content.Context;
import android.content.SharedPreferences;

/** Tiny preference wrapper - avoids pulling in AndroidX just for settings. */
public final class Store {
    private static final String FILE = "airlink";
    private static SharedPreferences prefs(Context c) {
        return c.getApplicationContext().getSharedPreferences(FILE, Context.MODE_PRIVATE);
    }

    public static String deviceName(Context c) {
        String def = android.os.Build.MODEL == null ? "Android" : android.os.Build.MODEL;
        return prefs(c).getString("name", def);
    }
    public static void setDeviceName(Context c, String v) {
        prefs(c).edit().putString("name", v).apply();
    }

    public static boolean allowBrowse(Context c) {
        return prefs(c).getBoolean("allowBrowse", true);
    }
    public static void setAllowBrowse(Context c, boolean v) {
        prefs(c).edit().putBoolean("allowBrowse", v).apply();
    }

    public static String selfId(Context c) {
        String id = prefs(c).getString("id", null);
        if (id == null) {
            id = Long.toHexString(System.currentTimeMillis() ^ (long) (Math.random() * 0xFFFFFF));
            prefs(c).edit().putString("id", id).apply();
        }
        return id;
    }

    /** Custom receive folder; empty means "use the default shallow folder". */
    public static String saveDir(Context c) {
        return prefs(c).getString("saveDir", "");
    }
    public static void setSaveDir(Context c, String v) {
        prefs(c).edit().putString("saveDir", v == null ? "" : v).apply();
    }

    public static boolean keepAwake(Context c) {
        return prefs(c).getBoolean("keepAwake", false);
    }
    public static void setKeepAwake(Context c, boolean v) {
        prefs(c).edit().putBoolean("keepAwake", v).apply();
    }

    /** Result of the last keep-warm ping, shown in settings for diagnosis. */
    public static void setLastWarm(Context c, boolean ok) {
        prefs(c).edit()
                .putBoolean("warmOk", ok)
                .putString("warmAt", new java.text.SimpleDateFormat("HH:mm:ss",
                        java.util.Locale.US).format(new java.util.Date()))
                .apply();
    }
    public static boolean warmOk(Context c) { return prefs(c).getBoolean("warmOk", false); }
    public static String warmAt(Context c) { return prefs(c).getString("warmAt", ""); }

    /** Mirror quality: target frame width. 1920 is sharp, 1280 is smoother. */
    public static int mirrorWidth(Context c) {
        return prefs(c).getInt("mirrorWidth", 1920);
    }
    public static void setMirrorWidth(Context c, int w) {
        prefs(c).edit().putInt("mirrorWidth", w).apply();
    }

    public static boolean askedBattery(Context c) {
        return prefs(c).getBoolean("askedBattery", false);
    }
    public static void setAskedBattery(Context c) {
        prefs(c).edit().putBoolean("askedBattery", true).apply();
    }

    /** One-shot flag: only explain the storage permission prompt once. */
    public static boolean askedStorage(Context c) {
        return prefs(c).getBoolean("askedStorage", false);
    }
    public static void setAskedStorage(Context c) {
        prefs(c).edit().putBoolean("askedStorage", true).apply();
    }

    /** Last successfully connected peer, so its row can offer "continue". */
    public static String lastPeerAddr(Context c) {
        return prefs(c).getString("lastPeer", "");
    }
    public static void setLastPeer(Context c, String ip, int port) {
        prefs(c).edit().putString("lastPeer", (ip == null ? "" : ip) + ":" + port).apply();
    }
    public static void setLastPeer(Context c, String ip, int port, String name) {
        setLastPeer(c, ip, port);
        prefs(c).edit().putString("lastPeerName", name == null ? "" : name).apply();
    }
    public static String lastPeerName(Context c) {
        return prefs(c).getString("lastPeerName", "");
    }
    /** Split the saved "ip:port" back into its parts, or null when unset. */
    public static String[] lastPeerParts(Context c) {
        String s = lastPeerAddr(c);
        if (s.isEmpty()) return null;
        int c2 = s.lastIndexOf(':');
        if (c2 <= 0) return null;
        return new String[]{ s.substring(0, c2), s.substring(c2 + 1) };
    }
    public static boolean isLastPeer(Context c, String ip, int port) {
        String s = lastPeerAddr(c);
        return !s.isEmpty() && s.equals((ip == null ? "" : ip) + ":" + port);
    }

    public static int historySeq(Context c) {
        int v = prefs(c).getInt("histSeq", 1);
        prefs(c).edit().putInt("histSeq", v + 1).apply();
        return v;
    }

    private Store() {}
}
