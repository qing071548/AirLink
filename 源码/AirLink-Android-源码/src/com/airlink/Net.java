package com.airlink;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * All networking for AirLink. The wire format is byte-compatible with the
 * Windows build so the two talk without any translation layer:
 *
 *   UDP 45679  discovery: "AIRLINK1\t<id>\t<name>\t<port>\n" broadcast per interface
 *   TCP 45678  magic "ALFT" + version(1) + type(1)
 *                1 = push  : u32 count, then per file u32 nameLen+name, u64 size,
 *                            reply 1 byte (1 = accept), raw bytes
 *                2 = list  : u32 pathLen+path  -> status(1) count(u32) entries
 *                3 = get   : u32 pathLen+path  -> status(1) size(u64) raw bytes
 *
 * Everything here runs on worker threads; callbacks are what the UI watches.
 */
public final class Net {
    public static final int TCP_PORT = 45678;
    public static final int UDP_PORT = 45679;
    private static final byte[] MAGIC = {'A', 'L', 'F', 'T'};

    public static final class Peer {
        public String id;
        public String name;
        public String ip;
        public int port;
        public long lastSeen;
        public boolean usb;
        /** The peer said goodbye, or stopped answering the heartbeat. */
        public boolean linkDown;
        @Override public String toString() { return name + " (" + ip + ")"; }
        public String key() { return id != null ? id : ip; }
    }

    // message types, matching the Windows build
    private static final int MSG_PUSH = 1, MSG_LIST = 2, MSG_GET = 3,
                             MSG_PING = 4, MSG_BYE = 5, MSG_DEL = 6,
                             MSG_SESSION = 7, MSG_HIST_GET = 8, MSG_HIST_PUT = 9, MSG_HIST_DEL = 10,
                             MSG_SCREEN = 11, MSG_INPUT = 12, MSG_STREAM = 13, MSG_AUTH = 14;

    /** One transfer record, shared with the desktop so both lists match. */
    public static final class HistRec {
        public String id = "", name = "", size = "", dir = "", state = "",
                      time = "", realName = "", peerIp = "";
        public int port = 0;
    }

    /** The peer this phone is currently connected to (null = none). */
    private static Peer session;

    public static synchronized Peer session() { return session; }
    public static synchronized void setSession(Peer p) { session = p; }
    public static synchronized void clearSession() { session = null; }
    public static synchronized boolean isSession(Peer p) {
        return session != null && p != null && session.ip != null && session.ip.equals(p.ip);
    }

    public static final class RemoteEntry {
        public String name;
        public String path;
        public boolean isDir;
        public long size;
        public long mtime;
    }

    public interface DiscoverListener { void onPeers(List<Peer> peers); }
    public interface IncomingListener {
        /** A peer wants to send us `names`; return true to accept. */
        boolean onOffer(String peerIp, String[] names, long total);
        void onProgress(String name, long done, long total);
        void onFinished(String savedPath);
        void onFailed(String reason);
    }

    // ------------------------------------------------------------------ state
    private static String selfId = "";
    private static String selfName = "Android";
    private static int listenPort = TCP_PORT;

    private static final Map<String, Peer> peers = Collections.synchronizedMap(new HashMap<String, Peer>());
    private static volatile DiscoverListener discoverListener;
    private static volatile IncomingListener incomingListener;

    private static Thread discoverThread, listenThread, broadcastThread;
    private static ServerSocket serverSocket;
    private static volatile boolean running = false;

    private Net() {}

    public static void setIdentity(String id, String name) {
        selfId = id;
        selfName = name;
    }
    public static String selfName() { return selfName; }

    public static void setDiscoverListener(DiscoverListener l) { discoverListener = l; }

    private static android.content.Context appCtx;
    public static void init(android.content.Context c) { appCtx = c.getApplicationContext(); }

    /**
     * Checked on every incoming browse request, so flipping the switch in
     * settings takes effect for the next click without restarting anything.
     */
    private static boolean browsingAllowed() {
        return appCtx == null || Store.allowBrowse(appCtx);
    }
    public static void setIncomingListener(IncomingListener l) { incomingListener = l; }

    public static List<Peer> peers() {
        synchronized (peers) { return new ArrayList<>(peers.values()); }
    }

    // ------------------------------------------------------------------ start/stop
    public static synchronized void start() {
        if (running) return;
        running = true;
        discoverThread = new Thread(Net::discoveryLoop, "airlink-disc");
        discoverThread.setDaemon(true);
        discoverThread.start();
        listenThread = new Thread(Net::listenLoop, "airlink-listen");
        listenThread.setDaemon(true);
        listenThread.start();
        broadcastThread = new Thread(Net::announceLoop, "airlink-ann");
        broadcastThread.setDaemon(true);
        broadcastThread.start();
    }

    public static synchronized void stop() {
        running = false;
        try { if (serverSocket != null) serverSocket.close(); } catch (IOException ignored) {}
        serverSocket = null;
    }

    // ------------------------------------------------------------------ local addresses
    /** Every non-loopback IPv4 address, best candidate first. */
    public static List<String> localAddresses() {
        List<String> out = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> ifs = NetworkInterface.getNetworkInterfaces();
            while (ifs != null && ifs.hasMoreElements()) {
                NetworkInterface ni = ifs.nextElement();
                if (!ni.isUp() || ni.isLoopback()) continue;
                Enumeration<InetAddress> addrs = ni.getInetAddresses();
                while (addrs.hasMoreElements()) {
                    InetAddress a = addrs.nextElement();
                    if (a.isLoopbackAddress() || a.getAddress().length != 4) continue;
                    String ip = a.getHostAddress();
                    if (ip == null || ip.startsWith("169.254.")) continue;
                    out.add(ip);
                }
            }
        } catch (Exception ignored) {}
        // Wi-Fi / LAN ranges before anything exotic
        Collections.sort(out, (a, b) -> rank(a) - rank(b));
        return out;
    }

    private static int rank(String ip) {
        if (ip.startsWith("192.168.")) return 0;
        if (ip.startsWith("10.")) return 1;
        if (ip.startsWith("172.")) return 2;
        return 3;
    }

    private static InetAddress broadcastFor(NetworkInterface ni) {
        try {
            for (java.net.InterfaceAddress ia : ni.getInterfaceAddresses()) {
                if (ia.getBroadcast() != null) return ia.getBroadcast();
            }
        } catch (Exception ignored) {}
        return null;
    }

    // ------------------------------------------------------------------ broadcast
    private static void announceLoop() {
        while (running) {
            try {
                Enumeration<NetworkInterface> ifs = NetworkInterface.getNetworkInterfaces();
                byte[] msg = ("AIRLINK1\t" + selfId + "\t" + selfName + "\t" + listenPort + "\n")
                        .getBytes(StandardCharsets.UTF_8);
                while (ifs != null && ifs.hasMoreElements()) {
                    NetworkInterface ni = ifs.nextElement();
                    if (!ni.isUp() || ni.isLoopback()) continue;
                    InetAddress bc = broadcastFor(ni);
                    if (bc == null) continue;
                    DatagramSocket ds = null;
                    try {
                        ds = new DatagramSocket();
                        ds.setBroadcast(true);
                        ds.send(new DatagramPacket(msg, msg.length, bc, UDP_PORT));
                    } catch (Exception ignored) {
                    } finally { if (ds != null) ds.close(); }
                }
            } catch (Exception ignored) {}
            sleep(1500);
        }
    }

    // ------------------------------------------------------------------ discovery
    private static void discoveryLoop() {
        DatagramSocket ds = null;
        try {
            ds = new DatagramSocket(null);
            ds.setReuseAddress(true);
            ds.setBroadcast(true);
            ds.bind(new InetSocketAddress(UDP_PORT));
            byte[] buf = new byte[512];
            while (running) {
                DatagramPacket p = new DatagramPacket(buf, buf.length);
                try { ds.receive(p); } catch (IOException e) { if (!running) break; continue; }
                String s = new String(p.getData(), 0, p.getLength(), StandardCharsets.UTF_8).trim();
                if (!s.startsWith("AIRLINK1\t")) continue;
                String[] parts = s.split("\t");
                if (parts.length < 4) continue;
                Peer peer = new Peer();
                peer.id = parts[1];
                peer.name = parts[2];
                try { peer.port = Integer.parseInt(parts[3].trim()); }
                catch (NumberFormatException e) { peer.port = TCP_PORT; }
                if (peer.id.equals(selfId)) continue;      // ignore ourselves
                peer.ip = p.getAddress().getHostAddress();
                peer.lastSeen = System.currentTimeMillis();
                peer.usb = isUsbIp(peer.ip);

                // One device announces on every interface it has (the PC has a
                // WLAN address plus VMware/VirtualBox host-only adapters). Keep a
                // single row per device id and prefer the address that shares a
                // subnet with this phone, so we do not end up dialling a
                // host-only adapter that cannot be reached.
                Peer old = peers.get(peer.key());
                if (old != null) {
                    boolean oldSame = sameSubnet(old.ip);
                    boolean newSame = sameSubnet(peer.ip);
                    if (newSame && !oldSame) { old.ip = peer.ip; old.usb = peer.usb; }
                    old.name = peer.name;
                    old.port = peer.port;
                    old.lastSeen = peer.lastSeen;
                } else {
                    peers.put(peer.key(), peer);
                    rememberPeer(peer);
                }
                DiscoverListener l = discoverListener;
                if (l != null) l.onPeers(peers());
            }
        } catch (Exception ignored) {
        } finally { if (ds != null) ds.close(); }
    }

    private static boolean isUsbIp(String ip) {
        return ip != null && ip.startsWith("192.168.42.");
    }

    // ------------------------------------------------------------------ tcp server
    private static void listenLoop() {
        while (running) {
            try {
                serverSocket = new ServerSocket();
                serverSocket.setReuseAddress(true);
                serverSocket.bind(new InetSocketAddress(listenPort));
                while (running) {
                    Socket s = serverSocket.accept();
                    s.setSoTimeout(60000);
                    Thread t = new Thread(() -> handleClient(s), "airlink-conn");
                    t.setDaemon(true);
                    t.start();
                }
            } catch (Exception e) {
                if (!running) break;
                sleep(700);
            } finally {
                try { if (serverSocket != null) serverSocket.close(); } catch (IOException ignored) {}
            }
        }
    }

    private static void handleClient(Socket s) {
        try {
            InputStream in = new BufferedInputStream(s.getInputStream());
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            byte[] magic = new byte[4];
            if (!readFully(in, magic, 4)) { s.close(); return; }
            for (int i = 0; i < 4; i++) if (magic[i] != MAGIC[i]) { s.close(); return; }
            int ver = in.read();
            int type = in.read();
            if (ver != 1 || type < 0) { s.close(); return; }
            if (type == MSG_LIST) { serveList(s, in, out); return; }
            if (type == MSG_GET) { serveGet(s, in, out); return; }
            if (type == MSG_DEL) { serveDelete(s, in, out); return; }
            if (type == MSG_SESSION) {
                String pid = readString(in);
                String pname = readString(in);
                long pport = readU32(in);
                if (pid == null || pname == null) { s.close(); return; }
                Peer me = new Peer();
                me.id = pid;
                me.name = pname;
                me.ip = s.getInetAddress().getHostAddress();
                me.port = (int) pport;
                me.lastSeen = System.currentTimeMillis();
                setSession(me);
                rememberPeer(me);
                notifySession();
                out.write(1);
                out.flush();
                s.close();
                return;
            }
            if (type == MSG_HIST_GET) {
                List<HistRec> recs = notifier == null ? new ArrayList<HistRec>() : notifier.historySnapshot();
                wr32(out, recs.size());
                for (HistRec r : recs) writeHist(out, r);
                out.flush();
                s.close();
                return;
            }
            if (type == MSG_HIST_PUT) {
                long n = readU32(in);
                if (n < 0 || n > 5000) { s.close(); return; }
                List<HistRec> got = new ArrayList<>();
                for (long i = 0; i < n; i++) {
                    HistRec r = readHist(in);
                    if (r == null) { s.close(); return; }
                    got.add(r);
                }
                if (notifier != null) notifier.mergeHistory(got);
                out.write(1);
                out.flush();
                s.close();
                return;
            }
            if (type == MSG_HIST_DEL) {
                String id = readString(in);
                if (id != null && notifier != null) notifier.deleteHistory(id);
                out.write(1);
                out.flush();
                s.close();
                return;
            }
            if (type == MSG_PING) {
                out.write(1);
                out.flush();
                s.close();
                return;
            }
            if (type == MSG_BYE) {
                markLinkDown(s.getInetAddress().getHostAddress());
                out.write(1);
                out.flush();
                s.close();
                return;
            }
            if (type != MSG_PUSH) { s.close(); return; }

            long count = readU32(in);
            if (count <= 0 || count > 2000) { s.close(); return; }
            List<String> names = new ArrayList<>();
            List<Long> sizes = new ArrayList<>();
            long total = 0;
            for (long i = 0; i < count; i++) {
                String n = readString(in);
                if (n == null) { s.close(); return; }
                long size = readU64(in);
                names.add(n);
                sizes.add(size);
                total += size;
            }

            String peerIp = s.getInetAddress().getHostAddress();
            IncomingListener l = incomingListener;
            boolean accept = l == null || l.onOffer(peerIp, names.toArray(new String[0]), total);
            out.write(accept ? 1 : 0);
            out.flush();
            if (!accept) { s.close(); return; }

            File dir = Paths.incomingDir();
            if (!dir.exists()) dir.mkdirs();
            for (int i = 0; i < names.size(); i++) {
                File dst = Paths.uniqueFile(dir, names.get(i));
                FileOutputStream fos = new FileOutputStream(dst);
                long left = sizes.get(i);
                byte[] buf = new byte[1 << 16];
                long doneInFile = 0;
                try {
                    while (left > 0) {
                        int want = (int) Math.min(buf.length, left);
                        int r = in.read(buf, 0, want);
                        if (r <= 0) throw new IOException("eof");
                        fos.write(buf, 0, r);
                        left -= r;
                        doneInFile += r;
                        if (l != null) l.onProgress(dst.getName(), sizes.get(i) - left, sizes.get(i));
                    }
                    fos.flush();
                } finally { fos.close(); }
                if (l != null) l.onFinished(dst.getAbsolutePath());
            }
            s.close();
        } catch (Exception e) {
            IncomingListener l = incomingListener;
            if (l != null) l.onFailed(friendly(e));
            try { s.close(); } catch (IOException ignored) {}
        }
    }

    // ---- serve directory listing to the PC
    private static void serveList(Socket s, InputStream in, OutputStream out) {
        try {
            String path = readString(in);
            if (path == null) { s.close(); return; }
            if (!browsingAllowed()) {
                out.write(0);
                writeString(out, "对方关闭了「允许浏览本机文件」");
                out.flush();
                s.close();
                return;
            }

            List<RemoteEntry> entries = Paths.list(path);
            if (entries == null) {
                out.write(0);
                writeString(out, "无法打开该目录");
                out.flush();
                s.close();
                return;
            }
            out.write(1);
            wr32(out, entries.size());
            for (RemoteEntry e : entries) {
                writeString(out, e.name);
                writeString(out, e.path);
                out.write(e.isDir ? 1 : 0);
                wr64(out, e.size);
                wr64(out, e.mtime);
            }
            out.flush();
            s.close();
        } catch (Exception e) {
            try { s.close(); } catch (IOException ignored) {}
        }
    }

    // ---- serve one file to the PC
    private static void serveGet(Socket s, InputStream in, OutputStream out) {
        try {
            String path = readString(in);
            if (path == null) { s.close(); return; }
            if (!browsingAllowed()) {
                out.write(0);
                writeString(out, "对方关闭了「允许浏览本机文件」");
                out.flush();
                s.close();
                return;
            }

            File f = Paths.resolve(path);
            if (f == null || !f.isFile() || !f.canRead()) {
                out.write(0);
                writeString(out, "无法读取该文件");
                out.flush();
                s.close();
                return;
            }
            out.write(1);
            wr64(out, f.length());
            out.flush();
            FileInputStream fis = new FileInputStream(f);
            try {
                byte[] buf = new byte[1 << 16];
                int r;
                while ((r = fis.read(buf)) > 0) out.write(buf, 0, r);
                out.flush();
            } finally { fis.close(); }
            s.close();
        } catch (Exception e) {
            try { s.close(); } catch (IOException ignored) {}
        }
    }

    // ---- delete one file the PC previously received from us.
    // The request carries a bare name; it is resolved inside the receive folder.
    private static void serveDelete(Socket s, InputStream in, OutputStream out) {
        try {
            String name = readString(in);
            if (name == null) { s.close(); return; }
            int cut = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
            if (cut >= 0) name = name.substring(cut + 1);
            if (name.isEmpty() || name.equals(".") || name.equals("..")) {
                out.write(0);
                writeString(out, "文件名无效");
                out.flush();
                s.close();
                return;
            }
            File f = new File(Paths.incomingDir(), name);
            if (f.exists() && f.isFile()) {
                if (!f.delete()) {
                    out.write(0);
                    writeString(out, "删除失败，可能没有权限");
                    out.flush();
                    s.close();
                    return;
                }
            }
            out.write(1);
            out.flush();
            s.close();
        } catch (Exception e) {
            try { s.close(); } catch (IOException ignored) {}
        }
    }
    public interface FrameSink {
        /** Called for every frame; return false to stop streaming. */
        boolean onFrame(byte[] jpeg, int w, int h);
    }

    /**
     * One socket, frames back to back. A deliberately small receive buffer makes
     * a slow phone throttle the sender instead of letting stale frames pile up,
     * which is what keeps latency low.
     */
    public static void streamFrames(Peer p, int wantWidth, FrameSink sink) {
        if (p == null || p.ip == null) return;
        Socket s = null;
        try {
            s = new Socket();
            s.setReceiveBufferSize(256 * 1024);
            s.setTcpNoDelay(true);
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 3000);
            s.setSoTimeout(10000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream(), 1 << 16);
            out.write(MAGIC); out.write(1); out.write(MSG_STREAM);
            out.flush();
            int st = in.read();
            if (st != 1) return;
            byte[] hdr = new byte[12];
            while (true) {
                if (!readFully(in, hdr, 12)) return;
                int w = (int) u32(hdr, 0), h = (int) u32(hdr, 4), len = (int) u32(hdr, 8);
                if (len <= 0 || len > (20 << 20)) return;
                byte[] buf = new byte[len];
                if (!readFully(in, buf, len)) return;
                if (!sink.onFrame(buf, w, h)) return;
            }
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    private static long u32(byte[] b, int off) {
        return (b[off] & 0xffL) | ((b[off + 1] & 0xffL) << 8)
                | ((b[off + 2] & 0xffL) << 16) | ((b[off + 3] & 0xffL) << 24);
    }


    /** Pairing handshake; needed before anything else when reaching a PC over WAN. */
    public static boolean auth(Peer p, String code) {
        if (p == null || p.ip == null) return false;
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(5000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC); out.write(1); out.write(MSG_AUTH);
            writeString(out, code == null ? "" : code);
            out.flush();
            int st = in.read();
            if (st == 0) {
                String msg = readString(in);
                if (msg != null) android.util.Log.w("AirLink", "auth refused: " + msg);
            }
            return st == 1;
        } catch (Exception e) {
            return false;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }
    // ---- screen mirror -----------------------------------------------------------
    public static final class Frame {
        public int w, h;
        public byte[] jpeg;
    }

    /** One JPEG frame of the peer's desktop. */
    public static Frame screenFrame(Peer p) {
        if (p == null || p.ip == null) return null;
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 3000);
            s.setSoTimeout(8000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC); out.write(1); out.write(MSG_SCREEN);
            out.flush();
            int st = in.read();
            if (st != 1) { android.util.Log.w("AirLink", "screenFrame status=" + st); return null; }
            long w = readU32(in), h = readU32(in), len = readU32(in);
            if (len <= 0 || len > (20 << 20)) return null;
            byte[] buf = new byte[(int) len];
            int got = 0;
            while (got < len) {
                int r = in.read(buf, got, (int) len - got);
                if (r <= 0) return null;
                got += r;
            }
            Frame f = new Frame();
            f.w = (int) w; f.h = (int) h; f.jpeg = buf;
            return f;
        } catch (Exception e) {
            android.util.Log.w("AirLink", "screenFrame failed: " + e);
            return null;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    /**
     * Forward one touch to the peer. Coordinates are 0..65535 so the phone does
     * not need to know the desktop resolution.
     */
    public static void sendInput(Peer p, int type, int nx, int ny, int key) {
        if (p == null || p.ip == null) return;
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 2500);
            s.setSoTimeout(2500);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            out.write(MAGIC); out.write(1); out.write(MSG_INPUT);
            wr32(out, type); wr32(out, nx); wr32(out, ny); wr32(out, key);
            out.flush();
            s.getInputStream().read();
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    // ---- record wire format ------------------------------------------------------
    static void writeHist(OutputStream out, HistRec r) throws IOException {
        writeString(out, r.id);
        writeString(out, r.name);
        writeString(out, r.size);
        writeString(out, r.dir);
        writeString(out, r.state);
        writeString(out, r.time);
        writeString(out, r.realName);
        writeString(out, r.peerIp);
        wr32(out, r.port);
    }

    static HistRec readHist(InputStream in) throws IOException {
        HistRec r = new HistRec();
        r.id = readString(in);
        r.name = readString(in);
        r.size = readString(in);
        r.dir = readString(in);
        r.state = readString(in);
        r.time = readString(in);
        r.realName = readString(in);
        r.peerIp = readString(in);
        r.port = (int) readU32(in);
        return r.id == null ? null : r;
    }

    /**
     * Callbacks the UI implements: the network layer owns the wire format, the
     * activity owns where records are stored.
     */
    public interface Notify {
        List<HistRec> historySnapshot();
        void mergeHistory(List<HistRec> recs);
        void deleteHistory(String id);
        void sessionChanged();
    }

    private static volatile Notify notifier;
    public static void setNotifier(Notify n) { notifier = n; }
    private static void notifySession() {
        Notify n = notifier;
        if (n != null) n.sessionChanged();
    }

    // ---- session + record sync (client side) --------------------------------------
    public static boolean sessionConnect(Peer p, String myId, String myName, int myPort) {
        // the link occasionally drops the tail of a short request; retry quietly
        for (int attempt = 0; attempt < 3; attempt++) {
            if (sessionConnectOnce(p, myId, myName, myPort)) return true;
            sleep(200);
        }
        return false;
    }

    private static boolean sessionConnectOnce(Peer p, String myId, String myName, int myPort) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(6000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC);
            out.write(1);
            out.write(MSG_SESSION);
            writeString(out, myId);
            writeString(out, myName);
            wr32(out, myPort);
            out.flush();
            int r = in.read();
            android.util.Log.i("AirLink", "sessionConnect reply=" + r + " to " + p.ip + ":" + p.port);
            return r == 1;
        } catch (Exception e) {
            android.util.Log.w("AirLink", "sessionConnect failed: " + e);
            return false;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    public static List<HistRec> histPull(Peer p) {
        List<HistRec> out = new ArrayList<>();
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(6000);
            OutputStream o = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            o.write(MAGIC); o.write(1); o.write(MSG_HIST_GET);
            o.flush();
            long n = readU32(in);
            if (n < 0 || n > 5000) return out;
            for (long i = 0; i < n; i++) {
                HistRec r = readHist(in);
                if (r == null) break;
                out.add(r);
            }
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
        return out;
    }

    public static void histPush(Peer p, List<HistRec> recs) {
        if (recs == null || recs.isEmpty()) return;
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(6000);
            OutputStream o = new BufferedOutputStream(s.getOutputStream());
            o.write(MAGIC); o.write(1); o.write(MSG_HIST_PUT);
            wr32(o, recs.size());
            for (HistRec r : recs) writeHist(o, r);
            o.flush();
            s.getInputStream().read();
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    public static void histDel(Peer p, String id) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(6000);
            OutputStream o = new BufferedOutputStream(s.getOutputStream());
            o.write(MAGIC); o.write(1); o.write(MSG_HIST_DEL);
            writeString(o, id);
            o.flush();
            s.getInputStream().read();
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    /**
     * Touch the peer without changing any state. Keeps the Wi-Fi radio out of
     * deep power save so the other side can still open a connection while the
     * screen is off.
     */
    public static void keepWarm(Peer p) {
        if (p == null || p.ip == null) return;
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 1500);
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    // ---- liveness / link control -------------------------------------------------
    private static final List<Peer> knownPeers = new ArrayList<>();

    /** Track every peer we have heard from so the heartbeat can probe them. */
    static void rememberPeer(Peer p) {
        synchronized (knownPeers) {
            for (Peer k : knownPeers) {
                if (k.key().equals(p.key())) {
                    k.ip = p.ip;
                    k.port = p.port;
                    k.name = p.name;
                    return;
                }
            }
            knownPeers.add(p);
        }
    }

    /** True when ip shares a /24 with one of this device's own addresses. */
    static boolean sameSubnet(String ip) {
        if (ip == null) return false;
        String mine = ip.substring(0, ip.lastIndexOf('.') + 1);
        for (String a : localAddresses()) {
            if (a.startsWith(mine)) return true;
        }
        return false;
    }

    private static void markLinkDown(String ip) {        synchronized (knownPeers) {
            for (Peer k : knownPeers) if (k.ip != null && k.ip.equals(ip)) k.linkDown = true;
        }
        DiscoverListener l = discoverListener;
        if (l != null) {
            List<Peer> snap;
            synchronized (knownPeers) { snap = new ArrayList<>(knownPeers); }
            l.onPeers(snap);
        }
    }

    /**
     * True when the peer's listener still accepts a connection.
     *
     * A pure connect is enough to tell "the other program is gone" from "still
     * running", and it avoids depending on a one-byte reply arriving intact.
     */
    public static boolean ping(Peer p) {
        Socket s = null;
        boolean ok = false;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 1500);
            ok = true;
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
        p.linkDown = !ok;
        return ok;
    }

    /** Tell the PC we are walking away so its list flips to disconnected now. */
    public static void sayBye(Peer p) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 1200);
            s.setSoTimeout(1200);
            OutputStream out = s.getOutputStream();
            out.write(MAGIC);
            out.write(1);
            out.write(MSG_BYE);
            out.flush();
            s.getInputStream().read();
        } catch (Exception ignored) {
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    /** Ask the PC to delete a delivered copy, by file name, from its receive folder. */
    public static boolean remoteDelete(Peer p, String fileName, String[] err) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new java.net.InetSocketAddress(p.ip, p.port), 4000);
            s.setSoTimeout(6000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC);
            out.write(1);
            out.write(MSG_DEL);
            writeString(out, fileName);
            out.flush();
            int st = in.read();
            if (st == 1) return true;
            if (st == 0) {
                String m = readString(in);
                if (err != null && err.length > 0) err[0] = m == null ? "对方拒绝了删除请求" : m;
            } else if (err != null && err.length > 0) {
                err[0] = "设备没有响应";
            }
            return false;
        } catch (Exception e) {
            if (err != null && err.length > 0) err[0] = friendly(e);
            return false;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    // ------------------------------------------------------------------ client: push
    public interface SendListener {
        void onProgress(String name, long done, long total);
        void onFinished();
        void onFailed(String reason);
    }

    public static void sendFiles(final Peer peer, final List<File> files, final SendListener l) {
        new Thread(() -> {
            Socket s = null;
            try {
                s = new Socket();
                s.connect(new InetSocketAddress(peer.ip, peer.port), 6000);
                s.setSoTimeout(60000);
                OutputStream out = new BufferedOutputStream(s.getOutputStream());
                InputStream in = new BufferedInputStream(s.getInputStream());

                out.write(MAGIC);
                out.write(1);
                out.write(1);
                wr32(out, files.size());
                long total = 0;
                for (File f : files) {
                    writeString(out, f.getName());
                    wr64(out, f.length());
                    total += f.length();
                }
                out.flush();

                int reply = in.read();
                if (reply != 1) { l.onFailed("对方拒绝接收"); s.close(); return; }

                long done = 0;
                for (File f : files) {
                    FileInputStream fis = new FileInputStream(f);
                    try {
                        byte[] buf = new byte[1 << 16];
                        int r;
                        while ((r = fis.read(buf)) > 0) {
                            out.write(buf, 0, r);
                            done += r;
                            l.onProgress(f.getName(), done, total);
                        }
                    } finally { fis.close(); }
                    out.flush();
                }
                out.flush();
                l.onFinished();
                s.close();
            } catch (Exception e) {
                l.onFailed(friendly(e));
                try { if (s != null) s.close(); } catch (IOException ignored) {}
            }
        }, "airlink-send").start();
    }

    // ------------------------------------------------------------------ client: browse
    /** Blocking directory listing against a peer. Returns null on failure. */
    public static List<RemoteEntry> remoteList(Peer peer, String path, String[] errOut) {
        // Wi-Fi drops short connections now and then; one quiet retry turns a
        // transient failure into a success without bothering the operator.
        for (int attempt = 0; attempt < 2; attempt++) {
            String[] e = new String[1];
            List<RemoteEntry> r = remoteListOnce(peer, path, e);
            if (r != null) return r;
            if (errOut != null) errOut[0] = e[0];
            Net.sleep(220);
        }
        return null;
    }

    private static List<RemoteEntry> remoteListOnce(Peer peer, String path, String[] errOut) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new InetSocketAddress(peer.ip, peer.port), 6000);
            s.setSoTimeout(20000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC); out.write(1); out.write(2);
            writeString(out, path);
            out.flush();

            int status = in.read();
            if (status != 1) {
                if (errOut != null) errOut[0] = readString(in);
                return null;
            }
            int count = (int) readU32(in);
            List<RemoteEntry> list = new ArrayList<>(Math.max(0, count));
            for (int i = 0; i < count; i++) {
                RemoteEntry e = new RemoteEntry();
                e.name = readString(in);
                e.path = readString(in);
                int dir = in.read();
                e.isDir = dir != 0;
                e.size = readU64(in);
                e.mtime = readU64(in);
                list.add(e);
            }
            return list;
        } catch (Exception e) {
            if (errOut != null) errOut[0] = friendly(e);
            return null;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    /** Blocking single-file download from a peer. */
    public static boolean remoteGet(Peer peer, String remotePath, File localFile,
                                    long[] progress, String[] errOut) {
        Socket s = null;
        try {
            s = new Socket();
            s.connect(new InetSocketAddress(peer.ip, peer.port), 6000);
            s.setSoTimeout(60000);
            OutputStream out = new BufferedOutputStream(s.getOutputStream());
            InputStream in = new BufferedInputStream(s.getInputStream());
            out.write(MAGIC); out.write(1); out.write(3);
            writeString(out, remotePath);
            out.flush();

            int status = in.read();
            if (status != 1) {
                if (errOut != null) errOut[0] = readString(in);
                return false;
            }
            long size = readU64(in);
            FileOutputStream fos = new FileOutputStream(localFile);
            try {
                long left = size, got = 0;
                byte[] buf = new byte[1 << 16];
                while (left > 0) {
                    int want = (int) Math.min(buf.length, left);
                    int r = in.read(buf, 0, want);
                    if (r <= 0) throw new IOException("连接中断");
                    fos.write(buf, 0, r);
                    left -= r;
                    got += r;
                    if (progress != null && progress.length > 0) progress[0] = got;
                }
                fos.flush();
            } finally { fos.close(); }
            return true;
        } catch (Exception e) {
            if (errOut != null) errOut[0] = friendly(e);
            return false;
        } finally {
            try { if (s != null) s.close(); } catch (IOException ignored) {}
        }
    }

    /** Turn raw socket exceptions into something a user can act on. */
    static String friendly(Exception e) {
        String m = e.getMessage();
        if (m == null) return "连接失败";
        String low = m.toLowerCase(Locale.US);
        if (low.contains("eof") || low.contains("reset") || low.contains("closed"))
            return "对方中断了连接";
        if (low.contains("timed out") || low.contains("timeout"))
            return "连接超时，检查两台设备是否在同一个网络";
        if (low.contains("refused")) return "对方没有在监听，请确认电脑上的 AirLink 已打开";
        if (low.contains("network is unreachable") || low.contains("no route"))
            return "网络不可达";
        if (low.contains("permission")) return "没有权限读取该目录";
        return m;
    }

    // ------------------------------------------------------------------ io helpers
    // The wire format is little-endian throughout: the Windows build writes
    // integers with a plain memcpy of the native type. DataOutputStream would
    // be big-endian, so everything is done by hand here.
    private static void wr32(OutputStream out, long v) throws IOException {
        out.write((int) (v & 0xff));
        out.write((int) ((v >> 8) & 0xff));
        out.write((int) ((v >> 16) & 0xff));
        out.write((int) ((v >> 24) & 0xff));
    }

    private static void wr64(OutputStream out, long v) throws IOException {
        wr32(out, v & 0xffffffffL);
        wr32(out, (v >>> 32) & 0xffffffffL);
    }

    private static boolean readFully(InputStream in, byte[] b, int n) throws IOException {
        int off = 0;
        while (off < n) {
            int r = in.read(b, off, n - off);
            if (r <= 0) return false;
            off += r;
        }
        return true;
    }

    private static long readU32(InputStream in) throws IOException {
        byte[] b = new byte[4];
        if (!readFully(in, b, 4)) throw new IOException("eof");
        return ((long) (b[0] & 0xff)) | ((long) (b[1] & 0xff) << 8)
                | ((long) (b[2] & 0xff) << 16) | ((long) (b[3] & 0xff) << 24);
    }

    private static long readU64(InputStream in) throws IOException {
        long lo = readU32(in), hi = readU32(in);
        return (hi << 32) | lo;
    }

    private static String readString(InputStream in) throws IOException {
        long n = readU32(in);
        if (n <= 0 || n > (1 << 20)) return null;
        byte[] b = new byte[(int) n];
        if (!readFully(in, b, (int) n)) return null;
        return new String(b, StandardCharsets.UTF_8);
    }

    private static void writeString(OutputStream out, String s) throws IOException {
        byte[] b = s.getBytes(StandardCharsets.UTF_8);
        wr32(out, b.length);
        out.write(b);
    }

    static void sleep(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException ignored) {}
    }

    public static String fmtSize(long bytes) {
        if (bytes < 1024) return bytes + " B";
        double kb = bytes / 1024.0;
        if (kb < 1024) return String.format(Locale.US, "%.1f KB", kb);
        double mb = kb / 1024.0;
        if (mb < 1024) return String.format(Locale.US, "%.1f MB", mb);
        return String.format(Locale.US, "%.2f GB", mb / 1024.0);
    }
}
