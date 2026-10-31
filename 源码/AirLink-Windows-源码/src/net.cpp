#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include "app.h"
#include <map>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

static const int DISC_PORT = 45679;
static const char* MAGIC = "ALFT";

static SOCKET g_ls = INVALID_SOCKET;
static std::atomic<int>  g_listenPort{ 0 };
static std::atomic<bool> g_listenRestart{ false };
static std::string g_selfId;
std::string net_self_id() { return g_selfId; }
static ULONGLONG g_lastLocalRefresh = 0;

// --------------------------------------------------------------- socket utils
static bool send_all(SOCKET s, const char* p, size_t n) {
    while (n) {
        int chunk = (int)(n > (1 << 20) ? (1 << 20) : n);
        int r = send(s, p, chunk, 0);
        if (r <= 0) return false;
        p += r; n -= (size_t)r;
    }
    return true;
}

static bool recv_all(SOCKET s, char* p, size_t n) {
    while (n) {
        int chunk = (int)(n > (1 << 20) ? (1 << 20) : n);
        int r = recv(s, p, chunk, 0);
        if (r <= 0) return false;
        p += r; n -= (size_t)r;
    }
    return true;
}

// Close after sending: shutting down the send direction first flushes the
// pending bytes and lets the peer finish reading, where a bare closesocket()
// would emit an RST and silently truncate the tail of the stream (seen when
// the Android client received 65534 of 65536 bytes).
static void graceful_close(SOCKET s, bool drain) {
    if (s == INVALID_SOCKET) return;
    shutdown(s, SD_SEND);
    if (drain) {
        // swallow whatever the peer still sends so the close is orderly
        DWORD tv = 2000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
        char tmp[4096];
        for (int i = 0; i < 64; i++) {
            int r = recv(s, tmp, sizeof(tmp), 0);
            if (r <= 0) break;
        }
    }
    closesocket(s);
}

static uint32_t rd32(const char* p) { uint32_t v; memcpy(&v, p, 4); return v; }static uint64_t rd64(const char* p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void wr32(char* p, uint32_t v) { memcpy(p, &v, 4); }
static void wr64(char* p, uint64_t v) { memcpy(p, &v, 8); }

static SOCKET connect_to(const std::string& ip, int port, int timeoutMs) {
    // Whatever family the address belongs to: a colon means IPv6.
    const bool v6 = ip.find(':') != std::string::npos;
    SOCKET s = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
    sockaddr_in6 a6{};
    sockaddr_in a{};
    int alen = 0;
    if (v6) {
        a6.sin6_family = AF_INET6;
        a6.sin6_port = htons((u_short)port);
        if (inet_pton(AF_INET6, ip.c_str(), &a6.sin6_addr) != 1) { closesocket(s); return INVALID_SOCKET; }
        alen = sizeof(a6);
    } else {
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)port);
        inet_pton(AF_INET, ip.c_str(), &a.sin_addr);
        alen = sizeof(a);
    }
    sockaddr* sa = v6 ? (sockaddr*)&a6 : (sockaddr*)&a;

    u_long nb = 1; ioctlsocket(s, FIONBIO, &nb);
    int r = connect(s, sa, alen);
    if (r == SOCKET_ERROR) {
        if (WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); return INVALID_SOCKET; }
        fd_set wf, ef; FD_ZERO(&wf); FD_ZERO(&ef);
        FD_SET(s, &wf); FD_SET(s, &ef);
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        r = select(0, nullptr, &wf, &ef, &tv);
        if (r <= 0) { closesocket(s); return INVALID_SOCKET; }
        int err = 0; int len = sizeof(err);
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&err, &len);
        if (err != 0) { closesocket(s); return INVALID_SOCKET; }
    }
    nb = 0; ioctlsocket(s, FIONBIO, &nb);
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
    int sndbuf = 1 << 20; setsockopt(s, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf, sizeof(sndbuf));
    int rcvbuf = 1 << 20; setsockopt(s, SOL_SOCKET, SO_RCVBUF, (char*)&rcvbuf, sizeof(rcvbuf));
    DWORD tv2 = 60000;  // 60s IO timeout
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv2, sizeof(tv2));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv2, sizeof(tv2));
    return s;
}

// --------------------------------------------------------------- local adapters
static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static bool looks_usb(const std::string& desc, const std::string& ip) {
    std::string d = lower(desc);
    static const char* keys[] = { "remote ndis", "rndis", "android", "usb ethernet",
                                  "apple mobile device", "iphone", "tether", "ncm" };
    for (auto k : keys) if (d.find(k) != std::string::npos) return true;
    if (ip.rfind("192.168.42.", 0) == 0) return true;
    return false;
}

// Which interface does Windows actually route internet traffic through right now?
// Returns the interface index of the best default route (lowest metric), or 0.
static DWORD default_route_ifindex() {
    PMIB_IPFORWARD_TABLE2 table = nullptr;
    if (GetIpForwardTable2(AF_INET, &table) != NO_ERROR || !table) return 0;
    DWORD best = 0;
    ULONG bestMetric = 0xFFFFFFFF;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IPFORWARD_ROW2& r = table->Table[i];
        // default route: destination 0.0.0.0/0
        if (r.DestinationPrefix.PrefixLength != 0) continue;
        if (r.DestinationPrefix.Prefix.si_family != AF_INET) continue;
        if (r.DestinationPrefix.Prefix.Ipv4.sin_addr.s_addr != 0) continue;
        ULONG m = r.Metric;
        if (m < bestMetric) { bestMetric = m; best = r.InterfaceIndex; }
    }
    FreeMibTable(table);
    return best;
}

// "192.168.1." for the adapter we route through, or "" when unknown.
// Callers must NOT hold g_mu.
static std::string primary_prefix() {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& a : g_local) {
        if (!a.primary) continue;
        size_t p = a.ip.find_last_of('.');
        if (p != std::string::npos) return a.ip.substr(0, p + 1);
    }
    return std::string();
}

// The LAN address the router should forward our port to: the one on the
// interface Windows actually routes through.
std::string net_primary_ip() {
    std::string pref = primary_prefix();
    std::vector<LocalAddr> all = local_addrs_snapshot();
    if (!pref.empty()) {
        for (auto& a : all) {
            if (!a.usb && a.ip.rfind(pref, 0) == 0) return a.ip;
        }
    }
    for (auto& a : all) if (!a.usb) return a.ip;
    return all.empty() ? "" : all[0].ip;
}

static bool same_net(const std::string& ip, const std::string& prefix) {
    return !prefix.empty() && ip.rfind(prefix, 0) == 0;
}

// Adapter "type weight": prefer the link the user is actually using.
// wifi/ethernet that carries the default route wins, USB tethering comes after
// real LAN links but before virtual/VPN adapters.
static int adapter_weight(const std::string& desc, bool usb) {
    std::string d = lower(desc);
    if (usb) return 40;
    if (d.find("wi-fi") != std::string::npos || d.find("wifi") != std::string::npos ||
        d.find("wireless") != std::string::npos || d.find("wlan") != std::string::npos) return 10;
    if (d.find("ethernet") != std::string::npos || d.find("gbe") != std::string::npos) return 20;
    return 30;
}

// Host-only / virtual NICs (VMware, VirtualBox, Hyper-V, VPN) can't reach a phone
// in practice, so they rank last — still listed as fallbacks, never dropped.
static bool is_virtual_adapter(const std::string& desc) {
    std::string d = lower(desc);
    static const char* keys[] = { "vmware", "virtualbox", "vethernet", "hyper-v",
                                  "loopback adapter", "npcap", "tap-", "tun ", "wintun",
                                  "zerotier", "tailscale", "wireguard", "openvpn" };
    for (auto k : keys) if (d.find(k) != std::string::npos) return true;
    return false;
}

// Host-only / virtual adapters can never reach a phone, so they are not offered
// as transfer addresses (they would just clutter the list with 192.168.x.1).

static std::vector<LocalAddr> enum_local() {
    std::vector<LocalAddr> out;
    ULONG size = 15000;
    std::vector<char> buf(size);
    PIP_ADAPTER_ADDRESSES addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size) == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        addrs = (PIP_ADAPTER_ADDRESSES)buf.data();
    }
    if (GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, addrs, &size) != NO_ERROR) return out;

    DWORD routeIf = default_route_ifindex();
    if (routeIf == 0) {
        // fall back: which interface would Windows pick for an outside address?
        DWORD idx = 0;
        if (GetBestInterface(inet_addr("8.8.8.8"), &idx) == NO_ERROR) routeIf = idx;
    }

    for (auto a = addrs; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        char descBuf[512] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, a->Description, -1, descBuf, sizeof(descBuf) - 1, nullptr, nullptr);
        bool ifIsRoute = (routeIf != 0 && a->IfIndex == routeIf);
        for (auto u = a->FirstUnicastAddress; u; u = u->Next) {
            const int fam = u->Address.lpSockaddr->sa_family;
            if (fam != AF_INET && fam != AF_INET6) continue;
            char ip[128] = { 0 };
            std::string s;
            if (fam == AF_INET) {
                auto* sin = (sockaddr_in*)u->Address.lpSockaddr;
                inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
                s = ip;
                if (s.rfind("127.", 0) == 0 || s == "0.0.0.0") continue;
                if (s.rfind("169.254.", 0) == 0) continue;
            } else {
                auto* sin6 = (sockaddr_in6*)u->Address.lpSockaddr;
                inet_ntop(AF_INET6, &sin6->sin6_addr, ip, sizeof(ip));
                s = ip;
                // only globally routable v6: link-local and unique-local
                // addresses cannot be reached from another network
                if (s.rfind("fe80", 0) == 0) continue;
                if (s.rfind("fd", 0) == 0 || s.rfind("fc", 0) == 0) continue;
                if (s == "::1" || s == "::") continue;
                if (sin6->sin6_scope_id != 0) continue;
            }
            if (s.rfind("169.254.", 0) == 0) continue;
            LocalAddr la;
            la.ip = s;
            la.adapter = descBuf;
            la.usb = looks_usb(descBuf, s);
            la.primary = ifIsRoute;   // the adapter carrying our default route
            la.virt = is_virtual_adapter(descBuf);
            out.push_back(la);
        }
    }
    // de-dup, keeping the primary flag if any duplicate has it
    std::vector<LocalAddr> uniq;
    for (auto& x : out) {
        bool dup = false;
        for (auto& y : uniq) if (y.ip == x.ip) { dup = true; if (x.primary) y.primary = true; break; }
        if (!dup) uniq.push_back(x);
    }

    // Order: primary first, then by adapter type weight, then IP.
    std::stable_sort(uniq.begin(), uniq.end(), [](const LocalAddr& a, const LocalAddr& b) {
        if (a.primary != b.primary) return a.primary;
        int wa = adapter_weight(a.adapter, a.usb), wb = adapter_weight(b.adapter, b.usb);
        bool va = is_virtual_adapter(a.adapter), vb = is_virtual_adapter(b.adapter);
        if (va != vb) return !va;
        if (wa != wb) return wa < wb;
        return a.ip < b.ip;
    });
    // If the route lookup failed entirely, still promote the best candidate so the
    // UI always has one address to treat as "current".
    if (!uniq.empty()) {
        bool any = false;
        for (auto& x : uniq) if (x.primary) any = true;
        if (!any) uniq[0].primary = true;
    }
    return uniq;
}

void net_refresh_local() {
    auto v = enum_local();
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_local = v;
    }
    post_refresh();
}

std::vector<LocalAddr> local_addrs_snapshot() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_local;
}

bool has_usb_link() {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& a : g_local) if (a.usb) return true;
    return false;
}

// --------------------------------------------------------------- discovery
static void announce(SOCKET s, const std::string& name, int port) {
    char buf[512];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "AIRLINK1\t%s\t%s\t%d\n", g_selfId.c_str(), name.c_str(), port);
    sockaddr_in to{};
    to.sin_family = AF_INET;
    to.sin_port = htons(DISC_PORT);
    to.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(s, buf, (int)strlen(buf), 0, (sockaddr*)&to, sizeof(to));

    // directed broadcast per interface
    auto locals = local_addrs_snapshot();
    for (auto& la : locals) {
        size_t p = la.ip.find_last_of('.');
        if (p == std::string::npos) continue;
        std::string b = la.ip.substr(0, p + 1) + "255";
        sockaddr_in ad{};
        ad.sin_family = AF_INET;
        ad.sin_port = htons(DISC_PORT);
        inet_pton(AF_INET, b.c_str(), &ad.sin_addr);
        sendto(s, buf, (int)strlen(buf), 0, (sockaddr*)&ad, sizeof(ad));
    }
}

static bool peer_is_usb(const std::string& ip) {
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& a : g_local) {
        if (!a.usb) continue;
        size_t p = a.ip.find_last_of('.');
        if (p != std::string::npos && ip.rfind(a.ip.substr(0, p), 0) == 0) return true;
    }
    return false;
}

static void disc_thread() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char*)&on, sizeof(on));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(DISC_PORT);
    a.sin_addr.s_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { closesocket(s); return; }

    ULONGLONG lastAnn = 0;
    char buf[1024];
    while (g_running) {
        fd_set rf; FD_ZERO(&rf); FD_SET(s, &rf);
        timeval tv{ 0, 150000 };
        int r = select(0, &rf, nullptr, nullptr, &tv);

        ULONGLONG now = GetTickCount64();
        if (now - g_lastLocalRefresh > 4000) {
            g_lastLocalRefresh = now;
            auto v = enum_local();
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                if (v.size() != g_local.size()) changed = true;
                else for (size_t i = 0; i < v.size(); i++)
                    if (v[i].ip != g_local[i].ip || v[i].usb != g_local[i].usb) changed = true;
                g_local = v;
            }
            if (changed) post_refresh();
        }
        if (now - lastAnn > 1200) {
            lastAnn = now;
            std::string nm; int port;
            { std::lock_guard<std::mutex> lk(g_mu); nm = w_to_utf8(g_set.deviceName); port = g_set.port; }
            announce(s, nm, port);
            // expire peers
            bool changed = false;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                size_t before = g_peers.size();
                g_peers.erase(std::remove_if(g_peers.begin(), g_peers.end(),
                    [&](const Peer& p) {
                        // A peer we hold a session with must never be dropped for
                        // missing a few announcements: the phone freezes its
                        // broadcast when the screen sleeps, but the TCP link is
                        // still good and browsing has to keep working.
                        if (net_session_is(p.ip)) return false;
                        // a peer that said goodbye lingers so its row can read
                        // "disconnected" instead of silently vanishing
                        ULONGLONG ttl = p.linkDown ? 45000 : 20000;
                        return now - p.lastSeen > ttl;
                    }), g_peers.end());
                changed = before != g_peers.size();
            }
            if (changed) post_refresh();
        }

        if (r > 0) {
            sockaddr_in from{};
            int fl = sizeof(from);
            int n = recvfrom(s, buf, sizeof(buf) - 1, 0, (sockaddr*)&from, &fl);
            if (n > 0) {
                buf[n] = 0;
                std::string line(buf);
                if (line.rfind("AIRLINK1\t", 0) == 0) {
                    std::vector<std::string> parts;
                    size_t pos = 0;
                    while (true) {
                        size_t t = line.find('\t', pos);
                        if (t == std::string::npos) { parts.push_back(line.substr(pos)); break; }
                        parts.push_back(line.substr(pos, t - pos));
                        pos = t + 1;
                    }
                    if (parts.size() >= 4 && parts[1] != g_selfId) {
                        char ipc[64] = { 0 };
                        inet_ntop(AF_INET, &from.sin_addr, ipc, sizeof(ipc));
                        std::string ip = ipc;
                        int port = atoi(parts[3].c_str());
                        bool usb = peer_is_usb(ip);
                        std::string pref = primary_prefix();
                        bool ipOnPrimary = same_net(ip, pref);
                        {
                            std::lock_guard<std::mutex> lk(g_mu);
                            Peer* found = nullptr;
                            for (auto& p : g_peers) if (p.id == parts[1]) { found = &p; break; }
                            if (!found) {
                                Peer np;
                                np.id = parts[1];
                                np.firstSeen = now;
                                g_peers.push_back(np);
                                found = &g_peers.back();
                            }
                            // Prefer the address reachable over the network we are
                            // actually using: don't let a secondary link overwrite
                            // an address that already sits on the primary subnet.
                            bool oldOnPrimary = same_net(found->ip, pref);
                            bool takeIp = !oldOnPrimary || ipOnPrimary;
                            bool diff = takeIp && (found->ip != ip || found->usb != usb);
                            if (takeIp) { found->ip = ip; found->usb = usb; }
                            if (found->port != port || found->name != parts[2]) diff = true;
                            found->port = port;
                            found->name = parts[2];
                            found->lastSeen = now;
                            // linkDown is deliberately sticky: a peer that walked away
                            // keeps announcing (it is still discoverable), so only a
                            // successful ping or new traffic marks it connected again.
                            if (diff) post_refresh();
                        }
                        post_refresh();
                    }
                }
            }
        }
    }
    closesocket(s);
}

// --------------------------------------------------------------- transfers
static std::wstring sanitize_name(const std::wstring& in) {
    std::wstring out;
    for (wchar_t c : in) {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'*' || c == L'?' ||
            c == L'"' || c == L'<' || c == L'>' || c == L'|' || c < 32) out += L'_';
        else out += c;
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    if (out.empty()) out = L"file";
    if (out.size() > 120) {
        size_t dot = out.find_last_of(L'.');
        std::wstring ext = (dot != std::wstring::npos && out.size() - dot <= 12) ? out.substr(dot) : L"";
        out = out.substr(0, 110) + ext;
    }
    return out;
}

static std::wstring unique_path(const std::wstring& dir, const std::wstring& name) {    std::wstring full = dir + L"\\" + name;
    if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return full;
    size_t dot = name.find_last_of(L'.');
    std::wstring base = dot == std::wstring::npos ? name : name.substr(0, dot);
    std::wstring ext = dot == std::wstring::npos ? L"" : name.substr(dot);
    for (int i = 1; i < 9999; i++) {
        full = dir + L"\\" + base + L" (" + std::to_wstring(i) + L")" + ext;
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) return full;
    }
    return dir + L"\\" + std::to_wstring(GetTickCount64()) + name;
}

static void finish_transfer(Transfer& t, TState st, const std::wstring& note) {
    t.state = st;
    t.t1 = GetTickCount64();
    if (!note.empty()) t.note = note;
    if (st == TS_Done) { t.done = t.total; t.speed = 0; }
}

// Forward declarations: the browse protocol lives further down, but the
// accept loop dispatches to it from handle_incoming.
static void serve_list(SOCKET s);
static void serve_get(SOCKET s);
static void serve_delete(SOCKET s);
static bool take_str(SOCKET s, std::string& out);
bool hist_decode(void* sock, HistRec& r);
static void reply_err(SOCKET s, const std::wstring& msg);
enum : unsigned char {
    MSG_PUSH = 1, MSG_LIST = 2, MSG_GET = 3,
    MSG_PING = 4,     // liveness probe: reply a single byte
    MSG_BYE = 5,      // the peer walked away; mark it disconnected
    MSG_DEL = 6,      // remove one file the peer previously received
    MSG_SESSION = 7,  // establish the session: <id> <name> <port>
    MSG_HIST_GET = 8, // ask for the full record list
    MSG_HIST_PUT = 9, // send records (merge by id)
    MSG_HIST_DEL = 10,// delete one record by id
    MSG_SCREEN = 11,  // ask for one screen frame (reply: w,h,len,jpeg)
    MSG_AUTH = 14     // pairing code handshake, required from non-LAN peers
};

// Which addresses have proved they know the pairing code, and until when.
static std::mutex g_authMu;
static std::map<std::string, ULONGLONG> g_authed;

static bool peer_is_lan(const std::string& ip) {
    if (ip.rfind("127.", 0) == 0) return true;
    if (ip.rfind("10.", 0) == 0) return true;
    if (ip.rfind("192.168.", 0) == 0) return true;
    if (ip.rfind("172.16.", 0) == 0 || ip.rfind("172.17.", 0) == 0 ||
        ip.rfind("172.18.", 0) == 0 || ip.rfind("172.19.", 0) == 0 ||
        ip.rfind("172.2", 0) == 0 || ip.rfind("172.30.", 0) == 0 ||
        ip.rfind("172.31.", 0) == 0) return true;
    return false;
}

static bool ip_authed(const std::string& ip) {
    std::lock_guard<std::mutex> lk(g_authMu);
    auto it = g_authed.find(ip);
    if (it == g_authed.end()) return false;
    if (GetTickCount64() - it->second > 10 * 60 * 1000) { g_authed.erase(it); return false; }
    return true;
}

static void mark_authed(const std::string& ip) {
    std::lock_guard<std::mutex> lk(g_authMu);
    g_authed[ip] = GetTickCount64();
}

// Session state: which peer this machine is currently connected to. Both sides
// must agree, so connecting from either end flips both device lists.
static std::string g_sessionId;      // peer id, empty = not connected
static std::string g_sessionIp;
static int         g_sessionPort = 0;
static std::mutex  g_sessMu;

std::string net_session_id() { std::lock_guard<std::mutex> lk(g_sessMu); return g_sessionId; }
void net_session_set(const std::string& id, const std::string& ip, int port) {
    std::lock_guard<std::mutex> lk(g_sessMu);
    g_sessionId = id; g_sessionIp = ip; g_sessionPort = port;
}
void net_session_clear() {
    std::lock_guard<std::mutex> lk(g_sessMu);
    g_sessionId.clear(); g_sessionIp.clear(); g_sessionPort = 0;
}
bool net_session_is(const std::string& ip) {
    std::lock_guard<std::mutex> lk(g_sessMu);
    return !g_sessionId.empty() && !ip.empty() && g_sessionIp == ip;
}

// Remote input from the phone: absolute coordinates arrive as 0..65535 so the
// phone does not need to know the desktop resolution.
static void inject_remote_input(unsigned type, unsigned nx, unsigned ny, unsigned key) {
    INPUT in{};
    if (type <= 2) {
        in.type = INPUT_MOUSE;
        in.mi.dx = (LONG)nx;
        in.mi.dy = (LONG)ny;
        in.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        if (type == 0) in.mi.dwFlags |= MOUSEEVENTF_MOVE;
        if (type == 1) in.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
        if (type == 2) in.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
        SendInput(1, &in, sizeof(INPUT));
        return;
    }
    if (type == 3) {                    // wheel: key carries the notch delta
        int delta = (int)(short)(key & 0xFFFF);
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = (DWORD)delta;
        SendInput(1, &in, sizeof(INPUT));
        return;
    }
    if (type == 4) {                    // virtual key press
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = (WORD)key;
        SendInput(1, &in, sizeof(INPUT));
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
    }
}

// getpeername returns sockaddr_in6 for a v6 caller; reading it as sockaddr_in
// yields a bogus address, which is why a WAN (IPv6) session used to be recorded
// against the wrong host.
static std::string peer_ip(SOCKET s) {
    sockaddr_storage ss{};
    int sl = sizeof(ss);
    if (getpeername(s, (sockaddr*)&ss, &sl) != 0) return "";
    char buf[128] = { 0 };
    if (ss.ss_family == AF_INET6) {
        in6_addr a6 = ((sockaddr_in6*)&ss)->sin6_addr;
        // A dual-stack listener reports IPv4 peers as ::ffff:a.b.c.d. Collapse
        // that back to plain IPv4, otherwise every LAN address test fails and a
        // local phone gets treated as an internet caller.
        if (IN6_IS_ADDR_V4MAPPED(&a6)) {
            inet_ntop(AF_INET, &a6.u.Byte[12], buf, sizeof(buf));
            return buf;
        }
        inet_ntop(AF_INET6, &a6, buf, sizeof(buf));
    } else if (ss.ss_family == AF_INET) {
        inet_ntop(AF_INET, &((sockaddr_in*)&ss)->sin_addr, buf, sizeof(buf));
    } else {
        return "";
    }
    return buf;
}

static std::string ipcOf(SOCKET s) {
    return peer_ip(s);
}

// Short unique id for the history record a transfer writes at its start.
static std::string new_hist_id() {
    static std::atomic<unsigned> seq{ 0 };
    char b[32];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%llx%x",
                (unsigned long long)GetTickCount64(), (unsigned)(++seq));
    return b;
}
static void handle_incoming(SOCKET s) {
    char hdr[4];
    bool okHdr = recv_all(s, hdr, 4);
    if (!okHdr || memcmp(hdr, MAGIC, 4) != 0) {
        FILE* lf = _wfopen(L"C:\\Users\\Administrator\\AppData\\Local\\Temp\\airlink_sess.log", L"ab");
        if (lf) { fprintf(lf, "hdr ok=%d bytes=%02x%02x%02x%02x\n", (int)okHdr,
                           (unsigned char)hdr[0], (unsigned char)hdr[1],
                           (unsigned char)hdr[2], (unsigned char)hdr[3]); fclose(lf); }
        closesocket(s); return;
    }
    char small[4];
    if (!recv_all(s, small, 2)) { closesocket(s); return; }
    unsigned char ver = small[0], type = small[1];
    if (ver != 1) { closesocket(s); return; }
    if (type != MSG_BYE) {
        // real traffic from this address proves the link is alive again
        std::string ipc = peer_ip(s);
        bool cleared = false;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& p : g_peers) {
                if (p.ip != ipc) continue;
                // traffic proves the peer is alive even with no broadcast
                p.lastSeen = GetTickCount64();
                if (p.linkDown) { p.linkDown = false; cleared = true; }
            }
        }
        if (cleared) post_refresh();
    }
    if (type == MSG_AUTH) {
        std::string code;
        if (!take_str(s, code)) { closesocket(s); return; }
        std::string ipc = peer_ip(s);
        if (!g_set.wanCode.empty() && code == g_set.wanCode) {
            mark_authed(ipc);
            char one = 1;
            send_all(s, &one, 1);
        } else {
            reply_err(s, L"配对码不对，请在电脑的「WAN连接」页查看");
        }
        graceful_close(s, false);
        return;
    }

    // Anything below can move files, change records or claim a session, so a
    // caller from outside the LAN must present the pairing code first.
    if (!peer_is_lan(ipcOf(s)) && !ip_authed(ipcOf(s))) {
        reply_err(s, L"需要配对码，请先在手机「WAN连接」里输入");
        graceful_close(s, false);
        return;
    }

    if (type == MSG_SESSION) {
        // the other side picked us: become connected to it right now
        std::string pid, pname; int pport = 0;
        bool okId = take_str(s, pid);
        bool okName = okId && take_str(s, pname);
        if (!okId || !okName) {
            FILE* lf = _wfopen(L"C:\\Users\\Administrator\\AppData\\Local\\Temp\\airlink_sess.log", L"ab");
            if (lf) { fprintf(lf, "session: id=%d name=%d\n", (int)okId, (int)okName); fclose(lf); }
            closesocket(s); return;
        }
        char b4[4];
        if (!recv_all(s, b4, 4)) {
            FILE* lf = _wfopen(L"C:\\Users\\Administrator\\AppData\\Local\\Temp\\airlink_sess.log", L"ab");
            if (lf) { fprintf(lf, "session: port read failed id=%s\n", pid.c_str()); fclose(lf); }
            closesocket(s); return;
        }
        pport = (int)rd32(b4);
        std::string ipc = peer_ip(s);
        ui_set_selected_peer(pid);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            bool known = false;
            for (auto& p : g_peers) {
                if (p.id == pid) {
                    p.linkDown = false; p.ip = ipc; p.port = pport; p.name = pname;
                    p.lastSeen = GetTickCount64();
                    known = true;
                }
            }
            // A WAN caller never shows up in the UDP broadcast. Without adding it
            // here the session points at an id the device list has never heard of
            // and the file browser falls back to local files.
            if (!known) {
                Peer np;
                np.id = pid;
                np.name = pname;
                np.ip = ipc;
                np.port = pport;
                np.lastSeen = GetTickCount64();
                np.linkDown = false;
                g_peers.push_back(np);
            }
        }
        net_session_set(pid, ipc, pport);
        device_log_add(utf8_to_w(pname), ipc, pport);
        post_refresh();
        char one = 1;
        send_all(s, &one, 1);
        graceful_close(s, false);
        return;
    }
    if (type == MSG_HIST_GET) {
        std::vector<HistRec> recs = history_records();
        std::string body;
        char n4[4]; wr32(n4, (uint32_t)recs.size());
        body.append(n4, 4);
        for (auto& r : recs) hist_encode(body, r);
        send_all(s, body.data(), body.size());
        graceful_close(s, false);
        return;
    }
    if (type == MSG_HIST_PUT) {
        char b4[4];
        if (!recv_all(s, b4, 4)) { closesocket(s); return; }
        uint32_t n = rd32(b4);
        if (n > 5000) { closesocket(s); return; }
        for (uint32_t i = 0; i < n; i++) {
            HistRec r;
            if (!hist_decode((void*)(uintptr_t)s, r)) { closesocket(s); return; }
            history_upsert(r);
        }
        ui_reload_history();
        post_refresh();
        char one = 1;
        send_all(s, &one, 1);
        graceful_close(s, false);
        return;
    }
    if (type == MSG_HIST_DEL) {
        std::string id;
        if (!take_str(s, id)) { closesocket(s); return; }
        history_delete_id(id);
        ui_reload_history();
        post_refresh();
        char one = 1;
        send_all(s, &one, 1);
        graceful_close(s, false);
        return;
    }
    if (type == MSG_LIST) { serve_list(s); return; }
    if (type == MSG_GET) { serve_get(s); return; }
    if (type == MSG_DEL) { serve_delete(s); return; }
    if (type == MSG_PING) {
        char one = 1;
        send_all(s, &one, 1);
        graceful_close(s, false);
        return;
    }
    if (type == MSG_BYE) {
        std::string ipc = peer_ip(s);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& p : g_peers) {
                if (p.ip == ipc) { p.linkDown = true; p.lastSeen = GetTickCount64(); }
            }
        }
        if (net_session_is(ipc)) net_session_clear();
        // the other side hung up: its row must stop reading "connected"
        ui_clear_selection_if_ip(ipc);
        post_refresh();
        char one = 1;
        send_all(s, &one, 1);
        graceful_close(s, false);
        return;
    }
    if (type != MSG_PUSH) { closesocket(s); return; }
    char n4[4];
    if (!recv_all(s, n4, 4)) { closesocket(s); return; }
    uint32_t count = rd32(n4);
    if (count == 0 || count > 2000) { closesocket(s); return; }

    std::vector<FileEntry> files;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!recv_all(s, n4, 4)) { closesocket(s); return; }
        uint32_t nl = rd32(n4);
        if (nl == 0 || nl > 8192) { closesocket(s); return; }
        std::string name(nl, '\0');
        if (!recv_all(s, &name[0], nl)) { closesocket(s); return; }
        char s8[8];
        if (!recv_all(s, s8, 8)) { closesocket(s); return; }
        FileEntry fe;
        fe.name = sanitize_name(utf8_to_w(name));
        fe.size = rd64(s8);
        total += fe.size;
        files.push_back(fe);
    }

    std::string ipc = peer_ip(s);




    Transfer t;
    t.dir = TD_In;
    t.files = files;
    t.total = total;
    t.t0 = GetTickCount64();
    t.peerAddr = utf8_to_w(ipc);
    t.peerIp = ipc;
    t.peerPort = (int)g_listenPort;
    t.cancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        t.id = ++g_nextTransferId;
        t.peerName = g_set.deviceName; // replaced below if known
        for (auto& p : g_peers) if (p.ip == ipc) {
            t.peerName = utf8_to_w(p.name);
            device_log_add(utf8_to_w(p.name), p.ip, p.port);
            break;
        }
        bool autoAcc = g_set.autoAccept;
        t.state = autoAcc ? TS_Active : TS_Waiting;
        if (!autoAcc) t.gate = std::make_shared<Gate>();
        g_tx.push_back(t);
        t.id = g_tx.back().id;
    }
    // Show it in the history immediately, then keep updating that same record.
    t.histId = new_hist_id();
    history_begin(t);
    post_refresh();

    int decision = 1;
    if (!g_set.autoAccept) {
        auto gate = t.gate;
        decision = gate->wait(120000);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == t.id) { x.state = decision > 0 ? TS_Active : TS_Rejected; break; }
        }
        post_refresh();
        if (decision == 0) {
            char zero = 0;
            send(s, &zero, 1, 0);
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& x : g_tx) if (x.id == t.id) { if (x.state == TS_Waiting) { finish_transfer(x, TS_Canceled, L"对方未响应"); } break; }
            }
            post_refresh();
            closesocket(s);
            return;
        }
    }

    char one = decision > 0 ? 1 : 0;
    if (!send_all(s, &one, 1)) { closesocket(s); return; }

    std::wstring dir;
    { std::lock_guard<std::mutex> lk(g_mu); dir = g_set.saveDir; }
    CreateDirectoryW(dir.c_str(), nullptr);

    std::vector<char> buf(1 << 20);
    uint64_t moved = 0;
    for (size_t fi = 0; fi < files.size(); fi++) {
        std::wstring path = unique_path(dir, files[fi].name);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == t.id) { x.curFile = fi; x.files[fi].path = path; break; }
        }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == t.id) finish_transfer(x, TS_Failed, L"无法创建文件");
            post_refresh();
            closesocket(s); return;
        }
        uint64_t left = files[fi].size;
        bool ok = true;
        while (left > 0) {
            bool cancelled = false;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& x : g_tx) if (x.id == t.id) { cancelled = x.cancel && x.cancel->load(); break; }
            }
            if (cancelled) { ok = false; break; }
            int want = (int)(left > buf.size() ? buf.size() : left);
            int r = recv(s, buf.data(), want, 0);
            if (r <= 0) { ok = false; break; }
            DWORD wr = 0;
            if (!WriteFile(h, buf.data(), (DWORD)r, &wr, nullptr) || wr != (DWORD)r) { ok = false; break; }
            left -= (uint64_t)r; moved += (uint64_t)r;
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == t.id) { x.files[fi].done += r; x.done += r; break; }
            if ((moved & 0x3FFFF) < (uint64_t)r) post_refresh();
        }
        CloseHandle(h);
        if (!ok) {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == t.id) {
                bool c = x.cancel && x.cancel->load();
                if (x.state != TS_Canceled) finish_transfer(x, c ? TS_Canceled : TS_Failed, c ? L"已取消" : L"连接中断");
                history_append(x);
            }
            post_refresh();
            closesocket(s);
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& x : g_tx) if (x.id == t.id) { finish_transfer(x, TS_Done, L""); history_append(x); break; }
    }
    post_refresh();
    closesocket(s);
}

// --------------------------------------------------------------- file browsing
// Wire types on the same TCP port:
//   1 = push files (existing transfer)
//   2 = list directory   request: path    reply: status(1) count(u32) then entries
//   3 = pull a file      request: path    reply: status(1) size(u64) then raw bytes
// Paths are UTF-8. On Windows they use drive-letter form ("C:\\Users");
// the phone side uses POSIX form ("/sdcard"). A reply of status 0 is followed
// by a length-prefixed UTF-8 error message.

static void put_str(std::string& out, const std::string& s) {
    char b4[4]; wr32(b4, (uint32_t)s.size());
    out.append(b4, 4);
    out.append(s);
}

static bool take_str(SOCKET s, std::string& out) {
    char b4[4];
    if (!recv_all(s, b4, 4)) return false;
    uint32_t n = rd32(b4);
    if (n > (1u << 20)) return false;
    out.assign(n, '\0');
    if (n && !recv_all(s, &out[0], n)) return false;
    return true;
}

static void reply_err(SOCKET s, const std::wstring& msg) {
    char st = 0;
    send_all(s, &st, 1);
    std::string body;
    put_str(body, w_to_utf8(msg));
    send_all(s, body.data(), body.size());
}

// Windows path -> the UTF-8 form that goes on the wire.
static std::string to_wire(const std::wstring& w) {
    std::string s = w_to_utf8(w);
    for (auto& c : s) if (c == '\\') c = '/';
    return s;
}

// Wire path -> a local wide path. Returns false when the input is not a
// well-formed absolute path, which also blocks "..\\" style escapes.
static bool from_wire(const std::string& in, std::wstring& out) {
    std::wstring w = utf8_to_w(in);
    if (w.empty()) return false;
    for (auto& c : w) if (c == L'/') c = L'\\';
    // collapse repeated separators so "C:\\dir" behaves like "C:\dir"
    std::wstring norm;
    norm.reserve(w.size());
    for (wchar_t c : w) {
        if (c == L'\\' && !norm.empty() && norm.back() == L'\\') continue;
        norm += c;
    }
    w.swap(norm);
    if (w.find(L"..") != std::wstring::npos) return false;
    if (!(w.size() >= 2 && w[1] == L':') && !(w.size() >= 2 && w[0] == L'\\' && w[1] == L'\\'))
        return false;
    out = w;
    return true;
}

static void serve_list(SOCKET s) {
    std::string path;
    if (!take_str(s, path)) { closesocket(s); return; }

    if (!g_set.allowBrowse) { reply_err(s, L"对方没有开启文件浏览"); closesocket(s); return; }

    // "/" is the virtual root: on Windows that is the list of drives, which
    // mirrors what a phone reports for its storage roots.
    if (path == "/" || path.empty()) {
        std::vector<std::wstring> drives;
        wchar_t buf[512] = { 0 };
        DWORD n = GetLogicalDriveStringsW(511, buf);
        for (DWORD i = 0; i < n;) {
            std::wstring d = buf + i;
            i += (DWORD)d.size() + 1;
            if (!d.empty() && d.back() == L'\\') d.pop_back();   // "C:" 
            drives.push_back(d);
        }
        char st = 1;
        send_all(s, &st, 1);
        std::string body;
        char b4[4];
        wr32(b4, (uint32_t)drives.size());
        body.append(b4, 4);
        for (auto& d : drives) {
            put_str(body, w_to_utf8(d));
            put_str(body, to_wire(d));
            char meta[17] = { 1, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0 };
            body.append(meta, 17);
        }
        send_all(s, body.data(), body.size());
        closesocket(s);
        return;
    }

    std::wstring dir;
    if (!from_wire(path, dir)) { reply_err(s, L"路径无效"); closesocket(s); return; }

    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    std::string body;
    char st = 1;
    std::vector<std::pair<std::wstring, WIN32_FIND_DATAW>> found;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            found.push_back({ n, fd });
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    } else {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_NO_MORE_FILES) {
            // genuinely empty directory
        } else {
            reply_err(s, L"无法打开目录（可能没有权限）");
            closesocket(s);
            return;
        }
    }
    std::sort(found.begin(), found.end(), [](auto& a, auto& b) {
        bool da = (a.second.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        bool db = (b.second.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (da != db) return da;                       // directories first
        return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0;
    });

    send_all(s, &st, 1);
    char b4[4];
    wr32(b4, (uint32_t)found.size());
    body.append(b4, 4);
    for (auto& f : found) {
        bool isDir = (f.second.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::string base = w_to_utf8(f.first);
        // dir may already end with a separator (a drive root does), so joining
        // blindly would produce "C:\\name" and show up as "C://name" on the wire.
        std::wstring fullPath = dir;
        if (fullPath.empty() || fullPath.back() != L'\\') fullPath += L'\\';
        fullPath += f.first;
        std::string full = to_wire(fullPath);
        put_str(body, base);
        put_str(body, full);
        char meta[17];
        meta[0] = isDir ? 1 : 0;
        uint64_t sz = ((uint64_t)f.second.nFileSizeHigh << 32) | f.second.nFileSizeLow;
        memcpy(meta + 1, &sz, 8);
        uint64_t mt = ((uint64_t)f.second.ftLastWriteTime.dwHighDateTime << 32) |
                      f.second.ftLastWriteTime.dwLowDateTime;
        memcpy(meta + 9, &mt, 8);
        body.append(meta, 17);
    }
    send_all(s, body.data(), body.size());
    closesocket(s);
}

static void serve_get(SOCKET s) {    std::string path;
    if (!take_str(s, path)) { closesocket(s); return; }
    if (!g_set.allowBrowse) { reply_err(s, L"对方没有开启文件浏览"); closesocket(s); return; }

    std::wstring file;
    if (!from_wire(path, file)) { reply_err(s, L"路径无效"); closesocket(s); return; }

    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { reply_err(s, L"无法读取该文件（可能没有权限）"); closesocket(s); return; }

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz)) {
        CloseHandle(h);
        reply_err(s, L"无法读取文件大小");
        closesocket(s);
        return;
    }
    char st = 1;
    char b8[8]; wr64(b8, (uint64_t)sz.QuadPart);
    send_all(s, &st, 1);
    send_all(s, b8, 8);

    std::vector<char> buf(1 << 20);
    uint64_t left = (uint64_t)sz.QuadPart;
    bool ok = true;
    while (left > 0) {
        DWORD want = (DWORD)(left > (uint64_t)buf.size() ? buf.size() : left);
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), want, &got, nullptr) || got == 0) { ok = false; break; }
        if (!send_all(s, buf.data(), got)) { ok = false; break; }
        left -= got;
    }
    CloseHandle(h);
    graceful_close(s, ok);
}

// Socket-side record decoder (recv_all only exists here).
bool hist_decode(void* sock, HistRec& r) {
    SOCKET s = (SOCKET)(uintptr_t)sock;
    auto get = [&](std::string& v) -> bool {
        char b[4];
        if (!recv_all(s, b, 4)) return false;
        uint32_t n; memcpy(&n, b, 4);
        if (n > (1u << 20)) return false;
        v.assign(n, '\0');
        if (n && !recv_all(s, &v[0], n)) return false;
        return true;
    };
    if (!get(r.id) || !get(r.name) || !get(r.size) || !get(r.dir) ||
        !get(r.state) || !get(r.time) || !get(r.realName) || !get(r.peerIp)) return false;
    char b4[4];
    if (!recv_all(s, b4, 4)) return false;
    uint32_t p; memcpy(&p, b4, 4);
    r.port = (int)p;
    return true;
}

// ---- delete one file the peer previously received from us.
// The request carries a bare file name; it is resolved inside our receive
// folder, so a peer can never reach anything else on this machine.
static void serve_delete(SOCKET s) {
    std::string name;
    if (!take_str(s, name)) { closesocket(s); return; }
    std::wstring wn = utf8_to_w(name);
    // strip any path component the caller may have sent
    size_t cut = wn.find_last_of(L"\\/");
    if (cut != std::wstring::npos) wn = wn.substr(cut + 1);
    if (wn.empty() || wn == L"." || wn == L"..") {
        reply_err(s, L"文件名无效");
        graceful_close(s, false);
        return;
    }
    std::wstring dir;
    { std::lock_guard<std::mutex> lk(g_mu); dir = g_set.saveDir; }
    std::wstring file = dir + L"\\" + wn;

    DWORD attr = GetFileAttributesW(file.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        // already gone counts as success: record and file both end up deleted
        char st = 1;
        send_all(s, &st, 1);
        graceful_close(s, false);
        return;
    }
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        reply_err(s, L"这是文件夹，不能删除");
        graceful_close(s, false);
        return;
    }
    if (!DeleteFileW(file.c_str())) {
        reply_err(s, L"删除失败，可能没有权限或文件正被占用");
        graceful_close(s, false);
        return;
    }
    char st = 1;
    send_all(s, &st, 1);
    graceful_close(s, false);
}

// --------------------------------------------------------------- client side
// The browse client can run without net_init() (headless self-test), so make
// sure Winsock is up before the first socket is created.
static std::atomic<bool> g_wsaReady{ false };
static void ensure_wsa() {
    if (g_wsaReady) return;
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
        g_wsaReady = true;
    });
}

// Browse requests must fail fast: a stalled read used to hold the UI on
// "正在读取目录" for the full 60s socket timeout.
static void set_browse_timeout(SOCKET s) {
    DWORD tv = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));
}

static bool net_fs_list_once(const std::string& ip, int port, const std::string& pathUtf8,
                             std::vector<RemoteEntry>& out, std::wstring& err) {
    out.clear();
    SOCKET s = connect_to(ip, port, 8000);
    if (s == INVALID_SOCKET) { err = L"无法连接，设备可能已离线"; return false; }
    set_browse_timeout(s);

    std::string req;
    req.append(MAGIC, 4);
    req.push_back(1);
    req.push_back((char)MSG_LIST);
    put_str(req, pathUtf8);
    if (!send_all(s, req.data(), req.size())) { closesocket(s); err = L"连接中断"; return false; }

    char st = 0;
    if (!recv_all(s, &st, 1)) { closesocket(s); err = L"设备没有响应"; return false; }
    if (st != 1) {
        std::string msg;
        take_str(s, msg);
        closesocket(s);
        err = msg.empty() ? L"对方拒绝了请求" : utf8_to_w(msg);
        return false;
    }
    char b4[4];
    if (!recv_all(s, b4, 4)) { closesocket(s); err = L"数据不完整"; return false; }
    uint32_t n = rd32(b4);
    if (n > 200000) { closesocket(s); err = L"目录项过多"; return false; }
    for (uint32_t i = 0; i < n; i++) {
        std::string base, full;
        if (!take_str(s, base) || !take_str(s, full)) { closesocket(s); err = L"数据不完整"; return false; }
        char meta[17];
        if (!recv_all(s, meta, 17)) { closesocket(s); err = L"数据不完整"; return false; }
        RemoteEntry e;
        e.name = utf8_to_w(base);
        e.path = full;
        e.isDir = meta[0] != 0;
        memcpy(&e.size, meta + 1, 8);
        memcpy(&e.mtime, meta + 9, 8);
        out.push_back(e);
    }
    closesocket(s);
    return true;
}

bool net_fs_list(const std::string& ip, int port, const std::string& pathUtf8,
                 std::vector<RemoteEntry>& out, std::wstring& err) {
    ensure_wsa();
    // A sleeping phone can take a couple of seconds to answer the first SYN, so
    // keep trying: a fast single attempt reads as "device offline" while the
    // phone is merely waking its radio.
    std::wstring lastErr;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (net_fs_list_once(ip, port, pathUtf8, out, lastErr)) return true;
        if (attempt < 3) Sleep(1200);
    }
    err = lastErr;
    return false;
}

bool net_fs_fetch(const std::string& ip, int port, const std::string& pathUtf8,
                  const std::wstring& localPath, std::wstring& err,
                  void (*progress)(uint64_t done, uint64_t total)) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) { err = L"无法连接，设备可能已离线"; return false; }

    std::string req;
    req.append(MAGIC, 4);
    req.push_back(1);
    req.push_back((char)MSG_GET);
    put_str(req, pathUtf8);
    if (!send_all(s, req.data(), req.size())) { closesocket(s); err = L"连接中断"; return false; }

    char st = 0;
    if (!recv_all(s, &st, 1)) { closesocket(s); err = L"设备没有响应"; return false; }
    if (st != 1) {
        std::string msg;
        take_str(s, msg);
        closesocket(s);
        err = msg.empty() ? L"对方拒绝了请求" : utf8_to_w(msg);
        return false;
    }
    char b8[8];
    if (!recv_all(s, b8, 8)) { closesocket(s); err = L"数据不完整"; return false; }
    uint64_t total = rd64(b8);

    HANDLE h = CreateFileW(localPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { closesocket(s); err = L"无法写入本地文件"; return false; }

    std::vector<char> buf(1 << 20);
    uint64_t got = 0;
    bool ok = true;
    while (got < total) {
        uint64_t left = total - got;
        int want = (int)(left > (uint64_t)buf.size() ? buf.size() : left);
        int r = recv(s, buf.data(), want, 0);
        if (r <= 0) { ok = false; break; }
        DWORD wrote = 0;
        if (!WriteFile(h, buf.data(), (DWORD)r, &wrote, nullptr) || (int)wrote != r) { ok = false; break; }
        got += r;
        if (progress) progress(got, total);
    }
    CloseHandle(h);
    closesocket(s);
    if (!ok || got != total) { err = L"传输中断，文件不完整"; return false; }
    return true;
}

static void accept_thread() {    while (g_running) {
        SOCKET ls = g_ls;
        if (ls == INVALID_SOCKET) { Sleep(120); continue; }
        fd_set rf; FD_ZERO(&rf); FD_SET(ls, &rf);
        timeval tv{ 0, 200000 };
        int r = select(0, &rf, nullptr, nullptr, &tv);
        if (r > 0) {
            SOCKET c = accept(ls, nullptr, nullptr);
            if (c != INVALID_SOCKET) {
                int one = 1;
                setsockopt(c, IPPROTO_TCP, TCP_NODELAY, (char*)&one, sizeof(one));
                DWORD tv2 = 60000;
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv2, sizeof(tv2));
                setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv2, sizeof(tv2));
                std::thread(handle_incoming, c).detach();
            }
        }
    }
}

static bool listen_on(int port) {
    // Dual stack: one socket serves both families, so IPv6-only callers work and
    // existing IPv4 links keep working.
    BOOL on = TRUE;
    BOOL v6only = FALSE;
    SOCKET s = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (s != INVALID_SOCKET)
        setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char*)&v6only, sizeof(v6only));
    if (s != INVALID_SOCKET) {
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
        sockaddr_in6 a6{};
        a6.sin6_family = AF_INET6;
        a6.sin6_port = htons((u_short)port);
        a6.sin6_addr = in6addr_any;
        if (bind(s, (sockaddr*)&a6, sizeof(a6)) == SOCKET_ERROR) { closesocket(s); s = INVALID_SOCKET; }
    }
    if (s == INVALID_SOCKET) {          // no IPv6 on this host: fall back
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return false;
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)port);
        a.sin_addr.s_addr = INADDR_ANY;
        if (bind(s, (sockaddr*)&a, sizeof(a)) == SOCKET_ERROR) { closesocket(s); return false; }
    }

    if (s == INVALID_SOCKET) return false;
    if (listen(s, 16) == SOCKET_ERROR) { closesocket(s); return false; }
    g_ls = s;
    return true;
}

void net_restart_listener() {
    SOCKET old = g_ls;
    g_ls = INVALID_SOCKET;
    if (old != INVALID_SOCKET) closesocket(old);
    Sleep(150);
    listen_on(g_set.port);
    post_refresh();
}

// --------------------------------------------------------------- session + sync
bool net_session_connect(const std::string& ip, int port, const std::string& myId,
                         const std::string& myName, int myPort) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) return false;
    std::string req;
    req.append(MAGIC, 4);
    req.push_back(1);
    req.push_back((char)MSG_SESSION);
    put_str(req, myId);
    put_str(req, myName);
    char b4[4]; wr32(b4, (uint32_t)myPort);
    req.append(b4, 4);
    bool ok = false;
    if (send_all(s, req.data(), req.size())) {
        char r = 0;
        ok = recv_all(s, &r, 1) && r == 1;
    }
    graceful_close(s, false);
    return ok;
}

std::vector<HistRec> net_hist_pull(const std::string& ip, int port) {
    std::vector<HistRec> out;
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) return out;
    char req[6] = { 'A', 'L', 'F', 'T', 1, (char)MSG_HIST_GET };
    if (!send_all(s, req, sizeof(req))) { graceful_close(s, false); return out; }
    char b4[4];
    if (!recv_all(s, b4, 4)) { graceful_close(s, false); return out; }
    uint32_t n; memcpy(&n, b4, 4);
    if (n > 5000) { graceful_close(s, false); return out; }
    for (uint32_t i = 0; i < n; i++) {
        HistRec r;
        if (!hist_decode((void*)(uintptr_t)s, r)) break;
        out.push_back(r);
    }
    graceful_close(s, false);
    return out;
}

void net_hist_push(const std::string& ip, int port, const std::vector<HistRec>& recs) {
    if (recs.empty()) return;
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) return;
    std::string body;
    body.append(MAGIC, 4);
    body.push_back(1);
    body.push_back((char)MSG_HIST_PUT);
    char b4[4]; wr32(b4, (uint32_t)recs.size());
    body.append(b4, 4);
    for (auto& r : recs) hist_encode(body, r);
    if (send_all(s, body.data(), body.size())) {
        char r = 0;
        recv_all(s, &r, 1);
    }
    graceful_close(s, false);
}

void net_hist_del(const std::string& ip, int port, const std::string& id) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) return;
    std::string req;
    req.append(MAGIC, 4);
    req.push_back(1);
    req.push_back((char)MSG_HIST_DEL);
    put_str(req, id);
    if (send_all(s, req.data(), req.size())) {
        char r = 0;
        recv_all(s, &r, 1);
    }
    graceful_close(s, false);
}

// --------------------------------------------------------------- link control
bool net_ping(const std::string& ip, int port, int timeoutMs) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, timeoutMs);
    if (s == INVALID_SOCKET) return false;
    char req[6] = { 'A', 'L', 'F', 'T', 1, (char)MSG_PING };
    bool ok = false;
    if (send_all(s, req, sizeof(req))) {
        char reply = 0;
        ok = recv_all(s, &reply, 1);
    }
    graceful_close(s, false);
    return ok;
}

void net_say_bye(const std::string& ip, int port) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 1500);
    if (s == INVALID_SOCKET) return;
    char req[6] = { 'A', 'L', 'F', 'T', 1, (char)MSG_BYE };
    send_all(s, req, sizeof(req));
    char reply = 0;
    recv_all(s, &reply, 1);
    graceful_close(s, false);
}

bool net_fs_delete(const std::string& ip, int port, const std::string& pathUtf8, std::wstring& err) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 5000);
    if (s == INVALID_SOCKET) { err = L"无法连接，设备可能已离线"; return false; }
    std::string req;
    req.append(MAGIC, 4);
    req.push_back(1);
    req.push_back((char)MSG_DEL);
    put_str(req, pathUtf8);
    bool ok = false;
    if (!send_all(s, req.data(), req.size())) {
        err = L"连接中断";
    } else {
        char st = 0;
        if (!recv_all(s, &st, 1)) {
            err = L"设备没有响应";
        } else if (st == 1) {
            ok = true;
        } else {
            std::string msg;
            take_str(s, msg);
            err = msg.empty() ? L"对方拒绝了删除请求" : utf8_to_w(msg);
        }
    }
    graceful_close(s, false);
    return ok;
}

// --------------------------------------------------------------- send side
struct SendJob { std::vector<std::wstring> paths; Peer peer; int txId = 0; };

// Prove the pairing code to a remote device before dialling it.
bool net_auth_send(const std::string& ip, int port, const std::string& code) {
    ensure_wsa();
    SOCKET s = connect_to(ip, port, 4000);
    if (s == INVALID_SOCKET) return false;
    char hdr[6] = { 'A', 'L', 'F', 'T', 1, (char)MSG_AUTH };
    std::string body;
    put_str(body, code);
    bool ok = send_all(s, hdr, 6) && send_all(s, body.data(), body.size());
    char one = 0;
    ok = ok && recv_all(s, &one, 1) && one == 1;
    graceful_close(s, true);
    return ok;
}
// Live progress for a history record, looked up by the id written at transfer start.
bool net_transfer_progress(const std::string& id, uint64_t& done, uint64_t& total) {
    if (id.empty()) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    for (auto& x : g_tx) {
        if (x.histId == id) {
            done = x.done;
            total = x.total;
            return x.state == TS_Active || x.state == TS_Waiting;
        }
    }
    return false;
}
static void do_send(SendJob job) {
    int myId;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        myId = ++g_nextTransferId;
        Transfer t;
        t.id = myId;
        t.dir = TD_Out;
        t.state = TS_Active;
        t.peerName = utf8_to_w(job.peer.name);
        t.peerAddr = utf8_to_w(job.peer.ip);
        t.peerIp = job.peer.ip;
        t.peerPort = job.peer.port;
        t.t0 = GetTickCount64();
        t.cancel = std::make_shared<std::atomic<bool>>(false);
        for (auto& p : job.paths) {
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) continue;
            FileEntry fe;
            size_t pos = p.find_last_of(L'\\');
            fe.name = pos == std::wstring::npos ? p : p.substr(pos + 1);
            fe.path = p;
            fe.size = ((uint64_t)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
            t.total += fe.size;
            t.files.push_back(fe);
        }
        if (t.files.empty()) {
            t.state = TS_Failed;
            t.note = L"文件不存在";
            g_tx.push_back(t);
            return;
        }
        t.histId = new_hist_id();
        job.txId = myId;
        g_tx.push_back(t);
        history_begin(t);
    }
    post_refresh();

    auto setState = [&](TState st, const std::wstring& note, bool isHistory) {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& x : g_tx) if (x.id == myId) {
            finish_transfer(x, st, note);
            if (isHistory) history_append(x);
            break;
        }
        post_refresh();
    };
    auto isCancelled = [&]() {
        std::lock_guard<std::mutex> lk(g_mu);
        for (auto& x : g_tx) if (x.id == myId) return x.cancel && x.cancel->load();
        return true;
    };

    SOCKET s = connect_to(job.peer.ip, job.peer.port, 4000);
    if (s == INVALID_SOCKET) { setState(TS_Failed, L"无法连接，设备可能已离线", true); return; }

    // header
    {
        std::string body;
        body.append(MAGIC, 4);
        body.push_back(1); body.push_back(1);
        char b4[4];
        wr32(b4, (uint32_t)job.paths.size());
        body.append(b4, 4);
        // note: send sanitized names of actual existing files
        std::vector<FileEntry> fs;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == myId) { fs = x.files; break; }
        }
        for (auto& fe : fs) {
            std::string n = w_to_utf8(fe.name);
            wr32(b4, (uint32_t)n.size());
            body.append(b4, 4);
            body.append(n);
            char b8[8]; wr64(b8, fe.size);
            body.append(b8, 8);
        }
        if (!send_all(s, body.data(), body.size())) { closesocket(s); setState(TS_Failed, L"连接中断", true); return; }
    }

    char reply = 0;
    if (!recv_all(s, &reply, 1)) { closesocket(s); setState(TS_Failed, L"对方无响应", true); return; }
    if (reply != 1) {
        closesocket(s);
        setState(TS_Rejected, L"对方拒绝接收", true);
        return;
    }

    std::vector<char> buf(1 << 20);
    uint64_t moved = 0;
    for (size_t fi = 0; fi < job.paths.size(); fi++) {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            for (auto& x : g_tx) if (x.id == myId && fi < x.files.size()) { path = x.files[fi].path; x.curFile = fi; break; }
        }
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { closesocket(s); setState(TS_Failed, L"无法读取文件", true); return; }
        bool ok = true;
        for (;;) {
            if (isCancelled()) { ok = false; break; }
            DWORD rd = 0;
            if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &rd, nullptr)) { ok = false; break; }
            if (rd == 0) break;
            if (!send_all(s, buf.data(), rd)) { ok = false; break; }
            moved += rd;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto& x : g_tx) if (x.id == myId) { x.done += rd; if (fi < x.files.size()) x.files[fi].done += rd; break; }
            }
            if ((moved & 0x3FFFF) < rd) post_refresh();
        }
        CloseHandle(h);
        if (!ok) {
            closesocket(s);
            if (isCancelled()) setState(TS_Canceled, L"已取消", true);
            else setState(TS_Failed, L"传输中断", true);
            return;
        }
    }
    shutdown(s, SD_SEND);
    closesocket(s);
    setState(TS_Done, L"", true);
}

// -------- debug: dump detected adapters in priority order
void net_dump_local(const std::wstring& path) {
    net_refresh_local();
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    std::lock_guard<std::mutex> lk(g_mu);
    fprintf(f, "session_id=%s session_ip=%s session_port=%d\n",
            g_sessionId.c_str(), g_sessionIp.c_str(), g_sessionPort);
    fprintf(f, "count=%d\n", (int)g_local.size());
    for (size_t i = 0; i < g_local.size(); i++) {
        auto& a = g_local[i];
        fprintf(f, "[%d] ip=%-16s primary=%d usb=%d  adapter=%s\n",
                (int)i, a.ip.c_str(), a.primary ? 1 : 0, a.usb ? 1 : 0, a.adapter.c_str());
    }
    fclose(f);
}

// -------- headless self test: send <file> to <ip>:<port>, wait for completion
int net_selfsend(const std::wstring& file, const std::string& ip, int port) {
    SendJob job;
    job.paths.push_back(file);
    job.peer.id = "selftest";
    job.peer.name = "selftest";
    job.peer.ip = ip;
    job.peer.port = port;
    std::thread(do_send, job).detach();
    for (int i = 0; i < 600; i++) {
        Sleep(50);
        std::lock_guard<std::mutex> lk(g_mu);
        if (!g_tx.empty()) {
            TState st = g_tx.back().state;
            if (st == TS_Done) return 0;
            if (st == TS_Failed || st == TS_Canceled || st == TS_Rejected) return 2;
        }
    }
    return 3;
}

void net_start_send(std::vector<std::wstring> paths, const Peer& peer) {
    SendJob job;
    job.paths = paths;
    job.peer = peer;
    std::thread(do_send, job).detach();
}

// --------------------------------------------------------------- init
void net_init() {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    char idbuf[64];
    _snprintf_s(idbuf, sizeof(idbuf), _TRUNCATE, "%08x%08x", (unsigned)GetTickCount(), (unsigned)(uintptr_t)&wsa);
    g_selfId = idbuf;

    net_refresh_local();
    if (!listen_on(g_set.port)) {
        // try a few alternates
        for (int p = g_set.port + 1; p < g_set.port + 20; p++) {
            if (listen_on(p)) { g_set.port = p; break; }
        }
    }
    std::thread(accept_thread).detach();
    std::thread(disc_thread).detach();
}
