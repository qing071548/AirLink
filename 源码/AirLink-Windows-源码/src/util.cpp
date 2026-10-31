#include "app.h"
#include <thread>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#include <cmath>
#include <ctime>

// ---------------------------------------------------------------- conversions
std::wstring utf8_to_w(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n);
    return out;
}

std::string w_to_utf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring fmt_size(uint64_t b) {
    if (b < 1024) return std::to_wstring(b) + L" B";
    const wchar_t* u[] = { L"KB", L"MB", L"GB", L"TB" };
    double v = (double)b; int i = -1;
    while (v >= 1024.0 && i < 3) { v /= 1024.0; i++; }
    wchar_t buf[64];
    if (v >= 100.0) swprintf(buf, 64, L"%.0f %s", v, u[i]);
    else if (v >= 10.0) swprintf(buf, 64, L"%.1f %s", v, u[i]);
    else swprintf(buf, 64, L"%.2f %s", v, u[i]);
    return buf;
}

std::wstring fmt_speed(double bps) {
    if (bps < 1.0) return L"0 B/s";
    return fmt_size((uint64_t)bps) + L"/s";
}

std::wstring fmt_time(int64_t secs) {
    if (secs < 0) secs = 0;
    wchar_t buf[64];
    if (secs < 60) swprintf(buf, 64, L"%lld 秒", (long long)secs);
    else if (secs < 3600) swprintf(buf, 64, L"%lld 分 %lld 秒", (long long)(secs / 60), (long long)(secs % 60));
    else swprintf(buf, 64, L"%lld 时 %lld 分", (long long)(secs / 3600), (long long)((secs % 3600) / 60));
    return buf;
}

std::wstring now_clock() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%02d:%02d", st.wHour, st.wMinute);
    return buf;
}

std::wstring now_stamp() {
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t buf[48];
    swprintf(buf, 48, L"%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

std::wstring appdata_dir() {
    wchar_t path[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path)))
        return L".";
    std::wstring d = std::wstring(path) + L"\\AirLink";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

std::wstring exe_dir() {
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p = path;
    size_t pos = p.find_last_of(L'\\');
    return pos == std::wstring::npos ? L"." : p.substr(0, pos);
}

std::wstring history_path() { return appdata_dir() + L"\\history.txt"; }
static std::wstring devices_path() { return appdata_dir() + L"\\devices.txt"; }

// ------------------------------------------------------------- device history
// name \t ip \t port \t timestamp — one line per connection, appended forever.
void device_log_add(const std::wstring& name, const std::string& ip, int port) {
    FILE* f = _wfopen(devices_path().c_str(), L"ab");
    if (!f) return;
    std::string line = w_to_utf8(name) + "\t" + ip + "\t" + std::to_string(port) +
                       "\t" + w_to_utf8(now_stamp()) + "\n";
    fwrite(line.data(), 1, line.size(), f);
    fclose(f);
}

std::vector<std::wstring> device_log_lines() {
    std::vector<std::wstring> out;
    FILE* f = _wfopen(devices_path().c_str(), L"rb");
    if (!f) return out;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        // tolerate a UTF-8 BOM if the file was ever edited by hand
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB
            && (unsigned char)s[2] == 0xBF) s = s.substr(3);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) out.push_back(utf8_to_w(s));
    }
    fclose(f);
    return out;
}

// Drop every record belonging to one device (matched by name + ip).
void device_log_remove(const std::wstring& name, const std::wstring& ip) {
    std::vector<std::wstring> all = device_log_lines();
    FILE* f = _wfopen(devices_path().c_str(), L"wb");
    if (!f) return;
    for (auto& s : all) {
        size_t a = s.find(L'\t');
        if (a == std::wstring::npos) continue;
        size_t b = s.find(L'\t', a + 1);
        if (b == std::wstring::npos) continue;
        std::wstring n = s.substr(0, a);
        std::wstring i = s.substr(a + 1, b - a - 1);
        if (n == name && i == ip) continue;
        std::string u = w_to_utf8(s);
        fwrite(u.data(), 1, u.size(), f);
        fwrite("\n", 1, 1, f);
    }
    fclose(f);
}

static std::wstring default_save_dir() {
    wchar_t path[MAX_PATH] = { 0 };
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, path))) {
        std::wstring d = std::wstring(path) + L"\\AirLink 接收";
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }
    return exe_dir();
}

// ---------------------------------------------------------------- settings
static std::wstring settings_file() { return appdata_dir() + L"\\settings.ini"; }

void settings_load() {
    // defaults
    g_set.port = 45678;
    g_set.autoAccept = true;
    g_set.allowBrowse = true;
    g_set.keepAwake = false;
    g_set.wanCode.clear();
    g_set.saveDir = default_save_dir();
    wchar_t comp[256] = { 0 }; DWORD sz = 255;
    GetComputerNameW(comp, &sz);
    g_set.deviceName = comp[0] ? comp : L"Windows PC";

    FILE* f = _wfopen(settings_file().c_str(), L"rb");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string k = s.substr(0, eq), v = s.substr(eq + 1);
            if (k == "name") g_set.deviceName = utf8_to_w(v);
            else if (k == "port") g_set.port = atoi(v.c_str());
            else if (k == "savedir") g_set.saveDir = utf8_to_w(v);
            else if (k == "autoaccept") g_set.autoAccept = (v == "1");
            else if (k == "allowbrowse") g_set.allowBrowse = (v == "1");
            else if (k == "keepawake") g_set.keepAwake = (v == "1");
            else if (k == "wancode") g_set.wanCode = v;
        }
        fclose(f);
    }
    if (g_set.port < 1024 || g_set.port > 65535) g_set.port = 45678;
    if (g_set.deviceName.empty()) g_set.deviceName = L"Windows PC";
    CreateDirectoryW(g_set.saveDir.c_str(), nullptr);
}

void settings_save() {
    FILE* f = _wfopen(settings_file().c_str(), L"wb");
    if (!f) return;
    fprintf(f, "name=%s\n", w_to_utf8(g_set.deviceName).c_str());
    fprintf(f, "port=%d\n", g_set.port);
    fprintf(f, "savedir=%s\n", w_to_utf8(g_set.saveDir).c_str());
    fprintf(f, "autoaccept=%d\n", g_set.autoAccept ? 1 : 0);
    fprintf(f, "allowbrowse=%d\n", g_set.allowBrowse ? 1 : 0);
    fprintf(f, "keepawake=%d\n", g_set.keepAwake ? 1 : 0);
    fprintf(f, "wancode=%s\n", g_set.wanCode.c_str());
    fclose(f);
}

static std::string make_id();
// ---------------------------------------------------------------- history
// Layout: name \t size \t dir \t state \t time \t realFileName \t peerIp \t peerPort
// The last three fields let the history page delete the counterpart copy.
static void split_tabs(const std::wstring& s, std::vector<std::wstring>& out);
// Writes the record as soon as a transfer starts, marked 传输中, so it shows up
// in the history page immediately instead of only when it finishes.
void history_begin(const Transfer& t) {
    FILE* f = _wfopen(history_path().c_str(), L"ab");
    if (!f) return;
    std::wstring name = t.files.size() > 1
        ? t.files[0].name + L" 等 " + std::to_wstring(t.files.size()) + L" 个文件"
        : (t.files.empty() ? L"文件" : t.files[0].name);
    std::wstring real = t.files.empty() ? L"" : t.files[0].name;
    fprintf(f, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%d\n",
            t.histId.c_str(), w_to_utf8(name).c_str(), w_to_utf8(fmt_size(t.total)).c_str(),
            t.dir == TD_In ? "in" : "out", "传输中",
            w_to_utf8(now_stamp()).c_str(), w_to_utf8(real).c_str(),
            t.peerIp.c_str(), t.peerPort);
    fclose(f);
}

// Rewrite one record's state in place (the transfer finished or was cancelled).
void history_set_state(const std::string& id, const wchar_t* state) {
    if (id.empty()) return;
    std::vector<std::wstring> lines = history_load_lines();
    std::string st = w_to_utf8(state ? state : L"");
    for (auto& ln : lines) {
        std::vector<std::wstring> f;
        split_tabs(ln, f);
        if (f.size() >= 9 && w_to_utf8(f[0]) == id) {
            f[4] = utf8_to_w(st);
            std::wstring joined;
            for (size_t i = 0; i < f.size(); i++) {
                if (i) joined += L'\t';
                joined += f[i];
            }
            ln = joined;
        }
    }
    FILE* f = _wfopen(history_path().c_str(), L"wb");
    if (!f) return;
    for (auto& ln : lines) {
        std::string u = w_to_utf8(ln);
        fwrite(u.data(), 1, u.size(), f);
        fputc('\n', f);
    }
    fclose(f);
}

void history_append(const Transfer& t) {
    // A record was already written when the transfer started: update its state
    // instead of adding a second line for the same transfer.
    if (!t.histId.empty()) {
        std::wstring st = t.state == TS_Done ? L"完成"
                        : (t.state == TS_Canceled ? L"已取消" : L"失败");
        history_set_state(t.histId, st.c_str());
        return;
    }
    FILE* f = _wfopen(history_path().c_str(), L"ab");
    if (!f) return;
    std::wstring name = t.files.size() > 1
        ? t.files[0].name + L" 等 " + std::to_wstring(t.files.size()) + L" 个文件"
        : (t.files.empty() ? L"文件" : t.files[0].name);
    std::wstring st = t.state == TS_Done ? L"完成" : (t.state == TS_Canceled ? L"已取消" : L"失败");
    std::wstring real = t.files.empty() ? L"" : t.files[0].name;
    std::string id = make_id();
    std::string line = id + "\t" + w_to_utf8(name) + "\t" + w_to_utf8(fmt_size(t.total)) + "\t" +
        (t.dir == TD_In ? "in" : "out") + "\t" + w_to_utf8(st) + "\t" + w_to_utf8(now_clock()) +
        "\t" + w_to_utf8(real) + "\t" + t.peerIp + "\t" + std::to_string(t.peerPort) + "\n";
    fwrite(line.data(), 1, line.size(), f);
    fclose(f);

    // live sync: the other device sees the same record immediately
    std::string sid = net_session_id();
    if (!sid.empty() && !t.peerIp.empty()) {
        HistRec r;
        r.id = id;
        r.name = w_to_utf8(name);
        r.size = w_to_utf8(fmt_size(t.total));
        r.dir = (t.dir == TD_In ? "in" : "out");
        r.state = w_to_utf8(st);
        r.time = w_to_utf8(now_clock());
        r.realName = w_to_utf8(real);
        r.peerIp.clear();
        r.port = 0;
        std::string ip = t.peerIp;
        int port = t.peerPort;
        std::thread([ip, port, r] { net_hist_push(ip, port, { r }); }).detach();
    }
}

// Drop one history line (matched verbatim) and rewrite the file.
void history_remove(const std::wstring& line) {
    std::vector<std::wstring> all = history_load_lines();
    FILE* f = _wfopen(history_path().c_str(), L"wb");
    if (!f) return;
    bool removed = false;
    for (auto& s : all) {
        if (!removed && s == line) { removed = true; continue; }
        std::string u = w_to_utf8(s);
        fwrite(u.data(), 1, u.size(), f);
        fwrite("\n", 1, 1, f);
    }
    fclose(f);
}

// ------------------------------------------------------------- record records
// Line layout: id \t name \t size \t dir \t state \t time \t realName \t ip \t port
// Older lines without the leading id are still read (a synthetic id is made).
static std::string make_id() {
    static int seq = 0;
    char buf[64];
    snprintf(buf, sizeof(buf), "%llx-%x-%x",
             (unsigned long long)GetTickCount64(), (unsigned)GetCurrentProcessId(), ++seq);
    return buf;
}

static void split_tabs(const std::wstring& s, std::vector<std::wstring>& out) {
    out.clear();
    size_t p = 0;
    while (true) {
        size_t t = s.find(L'\t', p);
        if (t == std::wstring::npos) { out.push_back(s.substr(p)); break; }
        out.push_back(s.substr(p, t - p));
        p = t + 1;
    }
}

void hist_encode(std::string& out, const HistRec& r) {
    auto put = [&](const std::string& v) {
        char b[4]; uint32_t n = (uint32_t)v.size(); memcpy(b, &n, 4);
        out.append(b, 4); out.append(v);
    };
    put(r.id); put(r.name); put(r.size); put(r.dir);
    put(r.state); put(r.time); put(r.realName); put(r.peerIp);
    char b8[8]; uint32_t p = (uint32_t)r.port; memcpy(b8, &p, 4);
    out.append(b8, 4);
}

// Parse one stored line into fields. Handles both the current id-prefixed
// layout and the older one that started with the display name.
bool hist_parse_line(const std::wstring& line, HistRec& r) {
    std::vector<std::wstring> f;
    split_tabs(line, f);
    bool hasId = f.size() >= 9;
    size_t base = hasId ? 1 : 0;
    if (f.size() < base + 7) return false;
    r.id = hasId ? w_to_utf8(f[0]) : std::string();
    r.name = w_to_utf8(f[base + 0]);
    r.size = w_to_utf8(f[base + 1]);
    r.dir = w_to_utf8(f[base + 2]);
    r.state = w_to_utf8(f[base + 3]);
    r.time = w_to_utf8(f[base + 4]);
    r.realName = w_to_utf8(f[base + 5]);
    r.peerIp = w_to_utf8(f[base + 6]);
    r.port = (f.size() > base + 7) ? _wtoi(f[base + 7].c_str()) : 0;
    return true;
}

std::vector<HistRec> history_records() {
    std::vector<HistRec> out;
    for (auto& line : history_load_lines()) {
        HistRec r;
        if (!hist_parse_line(line, r)) continue;
        if (r.id.empty()) r.id = make_id();
        out.push_back(r);
    }
    return out;
}

static std::string hist_line(const HistRec& r) {
    return r.id + "\t" + r.name + "\t" + r.size + "\t" + r.dir + "\t" + r.state + "\t" +
           r.time + "\t" + r.realName + "\t" + r.peerIp + "\t" + std::to_string(r.port) + "\n";
}

// Insert, or replace the record with the same id.
void history_upsert(const HistRec& r) {
    std::vector<HistRec> all = history_records();
    bool replaced = false;
    for (auto& x : all) if (x.id == r.id) { x = r; replaced = true; break; }
    if (!replaced) all.push_back(r);
    FILE* f = _wfopen(history_path().c_str(), L"wb");
    if (!f) return;
    for (auto& x : all) {
        std::string line = hist_line(x);
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
}

bool history_delete_id(const std::string& id) {
    std::vector<HistRec> all = history_records();
    FILE* f = _wfopen(history_path().c_str(), L"wb");
    if (!f) return false;
    bool removed = false;
    for (auto& x : all) {
        if (x.id == id) { removed = true; continue; }
        std::string line = hist_line(x);
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
    return removed;
}

std::vector<std::wstring> history_load_lines() {
    std::vector<std::wstring> out;
    FILE* f = _wfopen(history_path().c_str(), L"rb");
    if (!f) return out;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (!s.empty()) out.push_back(utf8_to_w(s));
    }
    fclose(f);
    if (out.size() > 300) out.erase(out.begin(), out.end() - 300);
    return out;
}
