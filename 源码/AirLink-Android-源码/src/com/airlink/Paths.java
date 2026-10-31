package com.airlink;

import android.content.Context;
import android.os.Build;
import android.os.Environment;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * Maps the wire path format ("/storage/emulated/0/DCIM") onto real files.
 *
 * A bare "/" is the virtual root and lists the storage volumes, mirroring what
 * the Windows build does with drive letters.
 */
public final class Paths {

    private static Context appContext;

    public static void init(Context ctx) { appContext = ctx.getApplicationContext(); }

    /** Volumes the phone can expose, most useful first. */
    public static List<File> roots() {
        List<File> out = new ArrayList<>();
        try {
            File primary = Environment.getExternalStorageDirectory();
            if (primary != null) out.add(primary);
            if (Build.VERSION.SDK_INT >= 19) {
                for (File f : appContext.getExternalFilesDirs(null)) {
                    if (f == null) continue;
                    // climb to the volume root: .../Android/data/<pkg>/files
                    File vol = f;
                    while (vol != null && vol.getParentFile() != null
                            && !"/storage".equals(vol.getParentFile().getAbsolutePath())) {
                        vol = vol.getParentFile();
                    }
                    if (vol == null || !vol.isDirectory()) continue;
                    String path = vol.getAbsolutePath();
                    // "/storage/emulated" is a container, not a volume; the real
                    // one is /storage/emulated/0, already added as primary
                    if (path.equals("/storage/emulated") || path.equals("/storage")) continue;
                    boolean dup = false;
                    for (File e : out) if (e.getAbsolutePath().equals(path)) { dup = true; break; }
                    if (!dup) out.add(vol);
                }
            }
        } catch (Exception ignored) {}
        return out;
    }

    /** "/" -> volume list; anything else goes to the matching real directory. */
    public static List<Net.RemoteEntry> list(String wirePath) {
        if (wirePath == null) return null;
        if (wirePath.isEmpty() || wirePath.equals("/")) {
            List<Net.RemoteEntry> out = new ArrayList<>();
            for (File r : roots()) {
                Net.RemoteEntry e = new Net.RemoteEntry();
                e.name = labelFor(r);
                e.path = r.getAbsolutePath();
                e.isDir = true;
                e.size = 0;
                e.mtime = r.lastModified();
                out.add(e);
            }
            return out;
        }
        File dir = resolve(wirePath);
        if (dir == null || !dir.isDirectory() || !dir.canRead()) return null;
        File[] kids = dir.listFiles();
        if (kids == null) return null;
        List<Net.RemoteEntry> out = new ArrayList<>(kids.length);
        List<File> sorted = new ArrayList<>(Arrays.asList(kids));
        java.util.Collections.sort(sorted, (a, b) -> {
            if (a.isDirectory() != b.isDirectory()) return a.isDirectory() ? -1 : 1;
            return a.getName().compareToIgnoreCase(b.getName());
        });
        for (File f : sorted) {
            Net.RemoteEntry e = new Net.RemoteEntry();
            e.name = f.getName();
            e.path = f.getAbsolutePath();
            e.isDir = f.isDirectory();
            e.size = f.isDirectory() ? 0 : f.length();
            e.mtime = f.lastModified();
            out.add(e);
        }
        return out;
    }

    private static String labelFor(File volume) {
        String p = volume.getAbsolutePath();
        if (p.equals("/storage/emulated/0")) return "内部存储";
        String name = volume.getName();
        if (name.matches("[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}")) return "存储卡 " + name;
        return name;
    }

    /** Resolve a wire path to a File, refusing anything that escapes the roots. */
    public static File resolve(String wirePath) {
        if (wirePath == null || wirePath.isEmpty()) return null;
        File f = new File(wirePath);
        if (!f.isAbsolute()) return null;
        try {
            String canon = f.getCanonicalPath();
            for (File r : roots()) {
                String rc = r.getCanonicalPath();
                if (canon.equals(rc) || canon.startsWith(rc + File.separator)) {
                    return new File(canon);
                }
            }
            // app-private dirs are always allowed (incoming files live there)
            String priv = appContext.getExternalFilesDir(null).getCanonicalPath();
            if (canon.startsWith(priv)) return new File(canon);
            String data = appContext.getFilesDir().getCanonicalPath();
            if (canon.startsWith(data)) return new File(canon);
        } catch (Exception ignored) {}
        return null;
    }

    /** Where files pushed from the PC land: a shallow, user-visible folder. */
    public static File incomingDir() {
        String custom = Store.saveDir(appContext);
        if (custom != null && !custom.isEmpty()) {
            File f = new File(custom);
            if (f.isDirectory() || f.mkdirs()) return f;
        }
        // Default: /storage/emulated/0/AirLink — one level, easy to find, no
        // Android/data/<pkg>/files/Download nesting.
        File pub = new File(Environment.getExternalStorageDirectory(), "AirLink");
        try {
            if (pub.isDirectory() || pub.mkdirs()) return pub;
        } catch (Exception ignored) {}
        File base = appContext.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS);
        if (base == null) base = new File(appContext.getFilesDir(), "Download");
        File fallback = new File(base, "AirLink");
        if (!fallback.isDirectory()) fallback.mkdirs();
        return fallback;
    }

    /** A folder the user can actually write to, for the settings picker. */
    public static List<String> suggestedDirs() {
        List<String> out = new ArrayList<>();
        out.add(new File(Environment.getExternalStorageDirectory(), "AirLink").getAbsolutePath());
        File dl = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS);
        out.add(new File(dl, "AirLink").getAbsolutePath());
        out.add(new File(Environment.getExternalStorageDirectory(), "Download").getAbsolutePath());
        for (File r : roots()) out.add(r.getAbsolutePath());
        return out;
    }

    /** True when MANAGE_EXTERNAL_STORAGE is granted (Android 11+). */
    public static boolean hasAllFilesAccess(Context ctx) {
        if (Build.VERSION.SDK_INT < 30) return true;
        try {
            return Environment.isExternalStorageManager();
        } catch (Exception e) {
            return false;
        }
    }

    /** What the breadcrumb shows: a phone volume root reads as "/". */
    public static String displayPath(String wire) {
        if (wire == null || wire.isEmpty()) return "/";
        String s = wire;
        while (s.length() > 1 && s.endsWith("/")) s = s.substring(0, s.length() - 1);
        if (s.equals("/storage/emulated")) return "/";
        int a = s.lastIndexOf('/');
        if (a > 0) {
            String parent = s.substring(0, a);
            if (parent.equals("/storage") || parent.equals("/storage/emulated")) return "/";
        }
        return s;
    }

    /** Never overwrite: "name (1).ext". */
    public static File uniqueFile(File dir, String name) {
        if (name == null || name.isEmpty()) name = "file";
        name = name.replace('/', '_').replace('\\', '_');
        File f = new File(dir, name);
        if (!f.exists()) return f;
        int dot = name.lastIndexOf('.');
        String base = dot < 0 ? name : name.substring(0, dot);
        String ext = dot < 0 ? "" : name.substring(dot);
        for (int i = 1; i < 9999; i++) {
            File c = new File(dir, base + " (" + i + ")" + ext);
            if (!c.exists()) return c;
        }
        return new File(dir, System.currentTimeMillis() + "_" + name);
    }
}
