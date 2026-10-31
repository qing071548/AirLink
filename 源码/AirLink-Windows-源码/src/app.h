// AirLink - shared application state and types
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <cstdint>
#include <condition_variable>

// ------------------------------------------------------------------ messages
#define WM_APP_REFRESH   (WM_APP + 1)   // any state changed -> repaint
#define WM_APP_TOAST     (WM_APP + 2)   // a toast was queued

// ------------------------------------------------------------------ helpers
std::wstring utf8_to_w(const std::string& s);
std::string  w_to_utf8(const std::wstring& s);
std::wstring fmt_size(uint64_t bytes);          // "1.2 MB"
std::wstring fmt_speed(double bps);             // "12.3 MB/s"
std::wstring fmt_time(int64_t secs);            // "1:23"
std::wstring now_clock();                       // "14:03"
std::wstring exe_dir();

// ------------------------------------------------------------------ model
struct Settings {
    std::wstring deviceName;
    int          port = 45678;
    std::wstring saveDir;
    bool         autoAccept = true;
    bool         usbPriority = true;
    bool         allowBrowse = true;   // let paired peers list/read this machine's files
    bool         keepAwake = false;
    std::string  wanCode;      // 6-digit pairing code required for WAN peers
    std::wstring wanPeerAddr;  // address of a remote device to dial out to
    std::wstring wanPeerCode;    // hold the machine awake while AirLink runs
};

struct Peer {
    std::string  id;
    std::string  name;
    std::string  ip;
    int          port = 0;
    bool         usb = false;
    bool         linkDown = false;   // peer said goodbye (or stopped answering)
    ULONGLONG    lastSeen = 0;
    ULONGLONG    firstSeen = 0;
};

struct LocalAddr {
    std::string ip;
    bool        usb = false;
    bool        primary = false;   // adapter carrying the current default route
    bool        virt = false;      // VMware / VirtualBox / VPN: unreachable for a phone
    std::string adapter;
};

enum TState { TS_Waiting, TS_Active, TS_Done, TS_Failed, TS_Canceled, TS_Rejected };
enum TDirection { TD_Out, TD_In };

struct FileEntry {
    std::wstring name;
    std::wstring path;
    uint64_t     size = 0;
    uint64_t     done = 0;
};

// handshake gate for incoming transfers
struct Gate {
    std::mutex              m;
    std::condition_variable cv;
    int                     decision = 0;   // 0 waiting, 1 accept, -1 reject
    void set(int d) {
        { std::lock_guard<std::mutex> lk(m); decision = d; }
        cv.notify_all();
    }
    int wait(int ms) {
        std::unique_lock<std::mutex> lk(m);
        if (decision != 0) return decision;
        cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return decision != 0; });
        return decision;
    }
};

struct Transfer {
    int         id = 0;
    int         dir = TD_Out;
    TState      state = TS_Active;
    std::wstring peerName;
    std::wstring peerAddr;
    std::string  peerIp;          // kept for history / remote delete
    int          peerPort = 0;
    std::vector<FileEntry> files;
    size_t      curFile = 0;
    uint64_t    total = 0;
    uint64_t    done = 0;
    double      speed = 0;
    std::wstring note;              // error text / status hint
    uint64_t    t0 = 0, t1 = 0;     // GetTickCount64
    std::string histId;             // the record created when this transfer started
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<Gate> gate;
    // speed sampling (ui thread only)
    uint64_t    sampleBytes = 0;
    uint64_t    sampleTick = 0;

    uint64_t bytes() const { return done; }
    int progress_permille() const {
        if (state == TS_Done) return 1000;
        if (total == 0) return 0;
        uint64_t d = done; if (d > total) d = total;
        return (int)(d * 1000 / total);
    }
    std::wstring curName() const {
        if (files.empty()) return L"";
        size_t i = curFile < files.size() ? curFile : files.size() - 1;
        return files[i].name;
    }
};

// ------------------------------------------------------------------ globals
extern HWND             g_hwnd;
extern std::mutex       g_mu;
extern std::vector<Transfer> g_tx;
extern std::vector<Peer>     g_peers;
extern std::vector<LocalAddr> g_local;
extern Settings          g_set;
extern int               g_nextTransferId;
extern std::vector<std::pair<std::wstring, uint64_t>> g_toasts; // text, expiry tick
extern std::atomic<bool> g_running;

void post_refresh();
void toast(const std::wstring& text);
// Re-assert (or release) the keep-awake request after the setting changes.
void apply_keep_awake();

// settings / paths
void settings_load();
void settings_save();
std::wstring appdata_dir();
std::wstring history_path();
void history_append(const Transfer& t);
void history_begin(const Transfer& t);              // write the record when a transfer starts
void history_set_state(const std::string& id, const wchar_t* state);
bool net_transfer_progress(const std::string& id, uint64_t& done, uint64_t& total);
bool net_auth_send(const std::string& ip, int port, const std::string& code);
void history_remove(const std::wstring& line);
std::vector<std::wstring> history_load_lines();

// Device history: every successful connection, so "设备管理" can list them.
void device_log_add(const std::wstring& name, const std::string& ip, int port);
std::vector<std::wstring> device_log_lines();
void device_log_remove(const std::wstring& name, const std::wstring& ip);
std::wstring now_stamp();

// One transfer record. Records carry an id so both devices can keep the same
// list: merges are by id, deletes travel by id.
struct HistRec {
    std::string id, name, size, dir, state, time, realName, peerIp;
    int port = 0;
};
std::vector<HistRec> history_records();
bool hist_parse_line(const std::wstring& line, HistRec& out);
void history_upsert(const HistRec& r);
bool history_delete_id(const std::string& id);
void hist_encode(std::string& out, const HistRec& r);
bool hist_decode(void* sock, HistRec& r);

// Session (who this machine is currently connected to).
std::string net_self_id();
std::string net_session_id();
void net_session_set(const std::string& id, const std::string& ip, int port);
void net_session_clear();
bool net_session_is(const std::string& ip);
bool net_session_connect(const std::string& ip, int port, const std::string& myId,
                         const std::string& myName, int myPort);
std::vector<HistRec> net_hist_pull(const std::string& ip, int port);
void net_hist_push(const std::string& ip, int port, const std::vector<HistRec>& recs);
void net_hist_del(const std::string& ip, int port, const std::string& id);

// ------------------------------------------------------------------ remote file browsing
struct RemoteEntry {
    std::wstring name;
    std::string  path;        // full remote path, UTF-8
    bool         isDir = false;
    uint64_t     size = 0;
    uint64_t     mtime = 0;
};

// Blocking calls: run them on a worker thread, never from the UI thread.
bool net_fs_list(const std::string& ip, int port, const std::string& pathUtf8,
                 std::vector<RemoteEntry>& out, std::wstring& err);
bool net_fs_fetch(const std::string& ip, int port, const std::string& pathUtf8,
                  const std::wstring& localPath, std::wstring& err,
                  void (*progress)(uint64_t done, uint64_t total));
// Ask the peer to delete one file (by name) from its receive folder.
bool net_fs_delete(const std::string& ip, int port, const std::string& fileNameUtf8, std::wstring& err);
// Liveness probe used to notice a peer that vanished without saying goodbye.
bool net_ping(const std::string& ip, int port, int timeoutMs);
// Tell the peer we are leaving so its list can show "disconnected" at once.
void net_say_bye(const std::string& ip, int port);

// net
void net_dump_local(const std::wstring& path);
int net_selfsend(const std::wstring& file, const std::string& ip, int port);
void net_init();
void net_restart_listener();
void net_start_send(std::vector<std::wstring> paths, const Peer& peer);
void net_refresh_local();
std::vector<LocalAddr> local_addrs_snapshot();
bool has_usb_link();

// ui
void ui_paint(HWND hwnd, HDC hdc);
void ui_shot(const std::wstring& path, float scale);
void ui_demo_state(int page = 0);
void ui_open_browse(const std::string& wirePath);
void ui_shot_qr(const std::string& text, const std::wstring& path, int pixelScale);
void ui_dump_hits(const std::wstring& path);
void ui_dpi_changed(UINT dpi);
void ui_reset_fonts();
void ui_mouse_move(int x, int y);
void ui_mouse_down(int x, int y);
void ui_mouse_up(int x, int y);
void ui_mouse_leave();
void ui_right_click(int x, int y);
void ui_set_selected_peer(const std::string& id);
void ui_reload_history();
void ui_clear_selection_if_ip(const std::string& ip);


// ---- WAN (UPnP port forwarding) ----
void wan_status(bool& mapped, bool& busy, int& extPort, int& inPort,
                std::string& extIp, std::string& publicIp, std::string& err);
void wan_open_async(int inPort);
void wan_close_async();
void wan_refresh_public_ip_async();
std::string net_primary_ip();
void ui_mouse_wheel(int x, int y, int delta);
bool ui_want_anim();
int  ui_anim_tick(RECT* rl);
void ui_key(int vk, bool down, bool ctrl);
void ui_char(wchar_t c);
void ui_set_size(int w, int h);
void ui_focus_lost();
void ui_drop_files(std::vector<std::wstring> paths, int x, int y);
void ui_drop_hover(int x, int y, bool active);
BOOL ui_background_erase(HWND hwnd, HDC hdc);
