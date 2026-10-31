// WAN support: ask the router (UPnP IGD) to forward our TCP port, discover the
// public address, and remember the mapping so it can be removed again.
//
// No third-party library: SSDP discovery, a tiny HTTP GET, and the SOAP calls
// are all plain Winsock + string handling.
#include "app.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <cstdio>

#pragma comment(lib, "ws2_32.lib")

namespace {

struct WanState {
    bool   mapped = false;
    bool   busy = false;
    int    extPort = 0;
    int    inPort = 0;
    std::string extIp;
    std::string publicIp;      // from an external lookup when UPnP is unavailable
    std::string controlUrl;    // IGD control endpoint, kept for removal
    std::string serviceType;
    std::string error;
};
WanState g_wan;
std::mutex g_wanMu;

void ensure_wsa_once() {
    static std::once_flag once;
    std::call_once(once, [] { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); });
}

// ---------------------------------------------------------------- tiny helpers
std::string http_get(const std::string& host, const std::string& path, int port, bool& ok) {
    ok = false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) { closesocket(s); return ""; }
        a.sin_addr = ((sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
    }
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return ""; }
    DWORD tv = 4000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    char req[512];
    _snprintf_s(req, sizeof(req), _TRUNCATE,
                "GET %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: close\r\n\r\n",
                path.c_str(), host.c_str(), port);
    if (send(s, req, (int)strlen(req), 0) <= 0) { closesocket(s); return ""; }

    std::string all;
    char buf[4096];
    while (true) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        all.append(buf, n);
        if (all.size() > (2 << 20)) break;
    }
    closesocket(s);
    ok = !all.empty();
    return all;
}

// Pull the body out of an HTTP response and cope with chunked encoding.
std::string http_body(const std::string& resp) {
    size_t p = resp.find("\r\n\r\n");
    if (p == std::string::npos) return "";
    std::string head = resp.substr(0, p);
    std::string body = resp.substr(p + 4);
    if (head.find("Transfer-Encoding: chunked") == std::string::npos) return body;
    std::string out;
    size_t i = 0;
    while (i < body.size()) {
        size_t e = body.find("\r\n", i);
        if (e == std::string::npos) break;
        long len = strtol(body.substr(i, e - i).c_str(), nullptr, 16);
        if (len <= 0) break;
        i = e + 2;
        if (i + (size_t)len > body.size()) break;
        out.append(body, i, (size_t)len);
        i += (size_t)len + 2;
    }
    return out;
}

std::string between(const std::string& s, const std::string& a, const std::string& b, size_t from = 0) {
    size_t p = s.find(a, from);
    if (p == std::string::npos) return "";
    p += a.size();
    size_t q = s.find(b, p);
    if (q == std::string::npos) return "";
    return s.substr(p, q - p);
}

// ---------------------------------------------------------------- SSDP find IGD
struct Igd { std::string location; std::string control; std::string service; };

bool ssdp_find(Igd& out) {
    ensure_wsa_once();
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return false;
    DWORD tv = 2500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    int ttl = 2;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (char*)&ttl, sizeof(ttl));

    const char* msg =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n";

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &dst.sin_addr);
    sendto(s, msg, (int)strlen(msg), 0, (sockaddr*)&dst, sizeof(dst));

    std::string location;
    char buf[4096];
    for (int i = 0; i < 8; i++) {
        int n = recv(s, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = 0;
        std::string r(buf);
        std::string loc = between(r, "LOCATION:", "\r\n");
        if (loc.empty()) loc = between(r, "location:", "\r\n");
        if (!loc.empty()) { location = loc; break; }
    }
    closesocket(s);
    if (location.empty()) return false;

    // fetch the device description
    std::string host, path;
    int port = 80;
    std::string rest = location;
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = atoi(hostport.substr(colon + 1).c_str());
    } else host = hostport;

    bool ok = false;
    std::string xml = http_body(http_get(host, path, port, ok));
    if (!ok) return false;

    // first WANIPConnection / WANPPPConnection service wins
    const char* types[] = { "WANIPConnection:1", "WANIPConnection:2", "WANPPPConnection:1" };
    for (auto t : types) {
        size_t p = xml.find(t);
        if (p == std::string::npos) continue;
        size_t svcStart = xml.rfind("<service>", p);
        size_t svcEnd = xml.find("</service>", p);
        if (svcStart == std::string::npos || svcEnd == std::string::npos) continue;
        std::string svc = xml.substr(svcStart, svcEnd - svcStart);
        std::string ctl = between(svc, "<controlURL>", "</controlURL>");
        if (ctl.empty()) continue;
        if (ctl[0] != '/') ctl = "/" + ctl;
        out.location = location;
        out.control = ctl;
        out.service = std::string("urn:schemas-upnp-org:service:") + t;
        out.control = "http://" + host + ":" + std::to_string(port) + ctl;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- SOAP
std::string soap(const std::string& url, const std::string& service,
                 const std::string& action, const std::string& args, bool& ok) {
    ok = false;
    std::string host, path;
    int port = 80;
    std::string rest = url;
    if (rest.rfind("http://", 0) == 0) rest = rest.substr(7);
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        port = atoi(hostport.substr(colon + 1).c_str());
    } else host = hostport;

    std::string body = "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:" + action + " xmlns:u=\"" + service + "\">" + args +
        "</u:" + action + "></s:Body></s:Envelope>\r\n";

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons((u_short)port);
    if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { closesocket(s); return ""; }
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) { closesocket(s); return ""; }
    DWORD tv = 5000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv));

    char head[1024];
    _snprintf_s(head, sizeof(head), _TRUNCATE,
        "POST %s HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPAction: \"%s#%s\"\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
        path.c_str(), host.c_str(), port, service.c_str(), action.c_str(), (int)body.size());
    std::string all(head);
    if (send(s, all.data(), (int)all.size(), 0) <= 0) { closesocket(s); return ""; }
    if (send(s, body.data(), (int)body.size(), 0) <= 0) { closesocket(s); return ""; }

    std::string resp;
    char buf[4096];
    while (true) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        resp.append(buf, n);
        if (resp.size() > (1 << 20)) break;
    }
    closesocket(s);
    ok = !resp.empty();
    return resp;
}

} // namespace

// ------------------------------------------------------------------ public API
void wan_status(bool& mapped, bool& busy, int& extPort, int& inPort,
                std::string& extIp, std::string& publicIp, std::string& err) {
    std::lock_guard<std::mutex> lk(g_wanMu);
    mapped = g_wan.mapped;
    busy = g_wan.busy;
    extPort = g_wan.extPort;
    inPort = g_wan.inPort;
    extIp = g_wan.extIp;
    publicIp = g_wan.publicIp;
    err = g_wan.error;
}

// Ask an external service for the public address (works even without UPnP).
static std::string lookup_public_ip() {
    ensure_wsa_once();
    bool ok = false;
    std::string body = http_body(http_get("api.ipify.org", "/", 80, ok));
    if (!ok || body.empty() || body.size() > 64) return "";
    for (char ch : body) {
        if (!(isdigit((unsigned char)ch) || ch == '.')) return "";
    }
    return body;
}

static bool do_map(int inPort, int extPort, std::string& err) {
    Igd igd;
    if (!ssdp_find(igd)) { err = "没有找到支持 UPnP 的路由器"; return false; }

    std::string localIp;
    {
        // the address the router should forward to: the LAN IP we listen on
        localIp = net_primary_ip();
        if (localIp.empty()) { err = "没有可用的局域网地址"; return false; }
    }

    char args[1024];
    _snprintf_s(args, sizeof(args), _TRUNCATE,
        "<NewRemoteHost></NewRemoteHost><NewExternalPort>%d</NewExternalPort>"
        "<NewProtocol>TCP</NewProtocol><NewInternalPort>%d</NewInternalPort>"
        "<NewInternalClient>%s</NewInternalClient><NewEnabled>1</NewEnabled>"
        "<NewPortMappingDescription>AirLink</NewPortMappingDescription>"
        "<NewLeaseDuration>0</NewLeaseDuration>",
        extPort, inPort, localIp.c_str());

    bool ok = false;
    std::string r = soap(igd.control, igd.service, "AddPortMapping", args, ok);
    if (r.find("AddPortMappingResponse") == std::string::npos &&
        r.find("200 OK") == std::string::npos) {
        // some firmware needs the mapping removed first
        soap(igd.control, igd.service,
             "DeletePortMapping",
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(extPort) +
             "</NewExternalPort><NewProtocol>TCP</NewProtocol>", ok);
        r = soap(igd.control, igd.service, "AddPortMapping", args, ok);
        if (r.find("AddPortMappingResponse") == std::string::npos &&
            r.find("200 OK") == std::string::npos) {
            err = "路由器拒绝了端口映射";
            return false;
        }
    }

    bool ok2 = false;
    std::string ipResp = soap(igd.control, igd.service, "GetExternalIPAddress", "", ok2);
    std::string ext = between(ipResp, "<NewExternalIPAddress>", "</NewExternalIPAddress>");
    {
        std::lock_guard<std::mutex> lk(g_wanMu);
        g_wan.controlUrl = igd.control;
        g_wan.serviceType = igd.service;
        g_wan.extIp = ext;
        g_wan.extPort = extPort;
        g_wan.inPort = inPort;
        g_wan.mapped = true;
        g_wan.error.clear();
    }
    return true;
}

void wan_open_async(int inPort) {
    {
        std::lock_guard<std::mutex> lk(g_wanMu);
        if (g_wan.busy || g_wan.mapped) return;
        g_wan.busy = true;
        g_wan.error.clear();
    }
    std::thread([inPort] {
        std::string err;
        bool ok = do_map(inPort, inPort, err);
        if (!ok) {
            // still useful: show the public address even when UPnP failed
            std::string pub = lookup_public_ip();
            std::lock_guard<std::mutex> lk(g_wanMu);
            g_wan.publicIp = pub;
            g_wan.error = err;
            g_wan.busy = false;
            return;
        }
        std::lock_guard<std::mutex> lk(g_wanMu);
        g_wan.busy = false;
        if (g_wan.publicIp.empty()) g_wan.publicIp = g_wan.extIp;
        post_refresh();
    }).detach();
}

void wan_close_async() {
    std::string url, svc;
    int extPort = 0;
    {
        std::lock_guard<std::mutex> lk(g_wanMu);
        if (!g_wan.mapped) return;
        url = g_wan.controlUrl;
        svc = g_wan.serviceType;
        extPort = g_wan.extPort;
        g_wan.mapped = false;
    }
    std::thread([url, svc, extPort] {
        if (url.empty()) return;
        bool ok = false;
        soap(url, svc, "DeletePortMapping",
             "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(extPort) +
             "</NewExternalPort><NewProtocol>TCP</NewProtocol>", ok);
    }).detach();
}

// Called once at startup: cache the public address so the WAN page has something
// to show even before the user asks for a mapping.
void wan_refresh_public_ip_async() {
    std::thread([] {
        std::string ip = lookup_public_ip();
        std::lock_guard<std::mutex> lk(g_wanMu);
        if (!ip.empty()) g_wan.publicIp = ip;
        post_refresh();
    }).detach();
}
