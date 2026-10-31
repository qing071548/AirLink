// AirLink - Win32 host: window, chrome, input
#include "app.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <objbase.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

// ------------------------------------------------------------------ globals
HWND                    g_hwnd = nullptr;
std::mutex              g_mu;
std::vector<Transfer>   g_tx;
std::vector<Peer>       g_peers;
std::vector<LocalAddr>  g_local;
Settings                g_set;
int                     g_nextTransferId = 0;
std::vector<std::pair<std::wstring, uint64_t>> g_toasts;
std::atomic<bool>       g_running{ true };

void post_refresh() {
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_REFRESH, 0, 0);
}

static const wchar_t* kClass = L"AirLinkMainWindow";
static std::wstring    g_tipText;
static const int       kTitleH = 52;

static UINT g_dpi = 96;
static bool g_tracking = false;
static bool g_headless = false;
#if defined(AIRLINK_DEBUG)
static void alog(const wchar_t* fmt, ...) {
    if (GetEnvironmentVariableW(L"AIRLINK_LOG", nullptr, 0) == 0) return;
    wchar_t line[512];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(line, 512, _TRUNCATE, fmt, ap);
    va_end(ap);
    HANDLE h = CreateFileW(L"airlink-log.txt", FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wrote = 0;
    WriteFile(h, line, (DWORD)(wcslen(line) * sizeof(wchar_t)), &wrote, nullptr);
    WriteFile(h, L"\r\n", 4, &wrote, nullptr);
    CloseHandle(h);
}
#else
#define alog(...) ((void)0)
#endif

// Keep-awake: ES_SYSTEM_REQUIRED stops the machine sleeping, ES_DISPLAY_REQUIRED
// also keeps the screen lit. Re-asserted periodically because Windows clears the
// state whenever the thread's execution state is reset.
void apply_keep_awake() {
    if (g_set.keepAwake) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
}

static void update_dpi(HWND hwnd) {    UINT d = 96;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        typedef UINT(WINAPI* Fn)(HWND);
        auto fn = (Fn)GetProcAddress(user32, "GetDpiForWindow");
        if (fn) d = fn(hwnd);
    }
    if (d == 0) d = 96;
    if (d != g_dpi) {
        g_dpi = d;
        ui_dpi_changed(d);
    }
}

// scale helper for the window chrome
static int D(int v) { return MulDiv(v, g_dpi, 96); }

static LRESULT hit_test(HWND hwnd, POINT pt) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int x = pt.x - rc.left, y = pt.y - rc.top;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;

    if (IsZoomed(hwnd)) return HTCLIENT;

    int b = D(6);
    bool L = x < b, R = x >= w - b, T = y < b, B = y >= h - b;
    if (T && L) return HTTOPLEFT;
    if (T && R) return HTTOPRIGHT;
    if (B && L) return HTBOTTOMLEFT;
    if (B && R) return HTBOTTOMRIGHT;
    if (L) return HTLEFT;
    if (R) return HTRIGHT;
    if (T) return HTTOP;
    if (B) return HTBOTTOM;

    int th = D(kTitleH);
    if (y < th) {
        int bw = D(46), bh = D(38), pad = D(6);
        int bx = w - bw * 3 - pad;
        if (x >= bx && y < D(6) + bh) return HTCLIENT;
        return HTCAPTION;
    }
    return HTCLIENT;
}

static void apply_round_corners(HWND hwnd) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (!dwm) return;
    typedef HRESULT(WINAPI* FnSet)(HWND, DWORD, LPCVOID, DWORD);
    auto set = (FnSet)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (!set) return;
    DWORD pref = 2;      // DWMWCP_ROUND
    set(hwnd, 33, &pref, sizeof(pref));
    COLORREF border = RGB(28, 33, 43);
    set(hwnd, 34, &border, sizeof(border));   // dark border on Win11
}

// ------------------------------------------------------------------ wndproc
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        update_dpi(hwnd);
        apply_round_corners(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        SetTimer(hwnd, 1, 30, nullptr);
        return 0;

    case WM_NCCALCSIZE: {
        RECT* r = (RECT*)lp;
        alog(L"WM_NCCALCSIZE wp=%d rect=%d,%d-%d,%d", (int)wp,
             (int)r[0].left, (int)r[0].top, (int)r[0].right, (int)r[0].bottom);
        return 0;
    }

    case WM_NCHITTEST: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        return hit_test(hwnd, pt);
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left;
            mmi->ptMaxPosition.y = mi.rcWork.top;
            mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
            mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
        }
        if (g_headless) {
            mmi->ptMinTrackSize.x = 0;
            mmi->ptMinTrackSize.y = 0;
        } else {
            mmi->ptMinTrackSize.x = D(980);
            mmi->ptMinTrackSize.y = D(640);
        }
        return 0;
    }

    case WM_SIZE:
        alog(L"WM_SIZE client=%dx%d", (int)LOWORD(lp), (int)HIWORD(lp));
        ui_set_size(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        ui_reset_fonts();
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH b = CreateSolidBrush(RGB(12, 14, 18));
        FillRect((HDC)wp, &rc, b);
        DeleteObject(b);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        ui_paint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TIMER:
        if (wp == 1) {
            RECT rl{};
            int mode = ui_anim_tick(&rl);
            if (mode == 1) InvalidateRect(hwnd, nullptr, FALSE);
            else if (mode == 2) InvalidateRect(hwnd, &rl, FALSE);
            // re-arm keep-awake about once a minute
            static int kaTick = 0;
            if (g_set.keepAwake && ++kaTick >= 2000) { kaTick = 0; apply_keep_awake(); }
        }
        return 0;

    case WM_APP_REFRESH:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE: {
        if (!g_tracking) {
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
            g_tracking = true;
        }
        ui_mouse_move(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    }
    case WM_RBUTTONUP:
        ui_right_click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_MOUSELEAVE:
        g_tracking = false;
        ui_mouse_leave();
        return 0;

    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        ui_mouse_down(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;

    case WM_LBUTTONUP: {
        ReleaseCapture();
        ui_mouse_up(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    }

    case WM_MOUSEWHEEL: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        ui_mouse_wheel(pt.x, pt.y, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }

    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        ui_key((int)wp, true, ctrl);
        return 0;
    }
    case WM_KEYUP:
        ui_key((int)wp, false, false);
        return 0;
    case WM_CHAR:
        ui_char((wchar_t)wp);
        return 0;

    case WM_KILLFOCUS:
        ui_focus_lost();
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            // hand cursor over clickable things
            HCURSOR c = LoadCursorW(nullptr, IDC_ARROW);
            SetCursor(c);
            return TRUE;
        }
        break;

    case WM_DROPFILES: {
        HDROP drop = (HDROP)wp;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        POINT pt;
        DragQueryPoint(drop, &pt);
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(drop, i, nullptr, 0);
            std::wstring path(len + 1, L'\0');
            DragQueryFileW(drop, i, &path[0], len + 1);
            path.resize(len);
            files.push_back(path);
        }
        DragFinish(drop);
        ui_drop_files(files, pt.x, pt.y);
        return 0;
    }

    case WM_CLOSE:
        g_running = false;
        settings_save();
        // say goodbye so the phone's device list reads "disconnected" at once
        {
            std::vector<Peer> ps;
            { std::lock_guard<std::mutex> lk(g_mu); ps = g_peers; }
            for (auto& p : ps) net_say_bye(p.ip, p.port);
        }
        // release the keep-awake request
        SetThreadExecutionState(ES_CONTINUOUS);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ main
static UINT system_dpi() {
    HMODULE u = GetModuleHandleW(L"user32.dll");
    if (u) {
        typedef UINT(WINAPI * Fn)();
        auto fn = (Fn)GetProcAddress(u, "GetDpiForSystem");
        if (fn) { UINT d = fn(); if (d) return d; }
    }
    HDC dc = GetDC(nullptr);
    int d = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return d > 0 ? (UINT)d : 96;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // headless render for verification:  --shot <png> <width> <height> <scale>
    std::wstring shotPath;
    int shotW = 1060, shotH = 760;
    float shotScale = 1.0f;
    bool headless = false, demo = false; int demoPage = 0;
    std::wstring dumpPath;
    std::wstring netInfoPath;
    bool selfSend = false; std::wstring sendFile; std::string sendIp; int sendPort = 45678;
    // file-browse self test
    bool fsList = false, fsGet = false;
    std::string fsIp; int fsPort = 45678;
    std::string fsPath; std::wstring fsOut;
    // dev hook: pretend a phone is online so the UI can be driven end to end
    bool fakePeer = false; std::wstring fakeName; std::string fakeIp; int fakePort = 45678;
    // dev hook: open the files page at a given wire path on startup
    bool startBrowse = false; std::string startPath;
    // dev hook: encode a QR symbol straight to a PNG (used to verify the encoder)
    bool qrShot = false; std::string qrText; std::wstring qrOut; int qrScale = 8;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                std::wstring a = argv[i];
                if (a == L"--demo") demo = true;
                if (a == L"--netinfo" && i + 1 < argc) netInfoPath = argv[++i];
                if (a.rfind(L"--page", 0) == 0) demoPage = _wtoi(a.c_str() + 6);
                if (a == L"--dump" && i + 1 < argc) dumpPath = argv[++i];
                if (a == L"--send" && i + 3 < argc) { selfSend = true; headless = true; sendFile = argv[++i]; sendIp = w_to_utf8(argv[++i]); sendPort = _wtoi(argv[++i]); }
                if (a == L"--fsls" && i + 4 < argc) {
                    fsList = true; headless = true;
                    fsIp = w_to_utf8(argv[++i]); fsPort = _wtoi(argv[++i]);
                    fsPath = w_to_utf8(argv[++i]); fsOut = argv[++i];
                }
                if (a == L"--fsget" && i + 4 < argc) {
                    fsGet = true; headless = true;
                    fsIp = w_to_utf8(argv[++i]); fsPort = _wtoi(argv[++i]);
                    fsPath = w_to_utf8(argv[++i]); fsOut = argv[++i];
                }
                if (a == L"--fakepeer" && i + 3 < argc) {
                    fakePeer = true;
                    fakeName = argv[++i];
                    fakeIp = w_to_utf8(argv[++i]);
                    fakePort = _wtoi(argv[++i]);
                }
                if (a == L"--brpath" && i + 1 < argc) {
                    startBrowse = true;
                    startPath = w_to_utf8(argv[++i]);
                }
                if (a == L"--qr" && i + 2 < argc) {
                    qrShot = true; headless = true;
                    qrText = w_to_utf8(argv[++i]);
                    qrOut = argv[++i];
                    if (i + 1 < argc && argv[i + 1][0] != L'-') qrScale = _wtoi(argv[++i]);
                }
                if (a == L"--shot" && i + 1 < argc) {
                    headless = true;
                    shotPath = argv[++i];
                    if (i + 1 < argc) shotW = _wtoi(argv[++i]);
                    if (i + 1 < argc) shotH = _wtoi(argv[++i]);
                    if (i + 1 < argc) shotScale = (float)_wtof(argv[++i]);
                }
            }
            LocalFree(argv);
        }
    }

    // single instance (not for headless renders)
    if (!headless) {
        CreateMutexW(nullptr, FALSE, L"AirLink_SingleInstance_Mutex_v1");
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND old = FindWindowW(kClass, nullptr);
            if (old) {
                if (IsIconic(old)) ShowWindow(old, SW_RESTORE);
                SetForegroundWindow(old);
            }
            return 0;
        }
    }

    settings_load();
    if (g_set.wanCode.empty()) {
        // 6 digits, stable across runs so a phone can keep using it
        srand((unsigned)GetTickCount64());
        char code[8];
        _snprintf_s(code, sizeof(code), _TRUNCATE, "%06d", rand() % 1000000);
        g_set.wanCode = code;
        settings_save();
    }
    apply_keep_awake();
    wan_refresh_public_ip_async();

    // Headless self-test: send a file and report the outcome as the exit code.
    // Runs before any window exists, so it neither needs a UI nor trips the
    // single-instance guard.
    if (selfSend) {
        net_init();
        return net_selfsend(sendFile, sendIp, sendPort);
    }
    if (!netInfoPath.empty()) { net_init(); net_dump_local(netInfoPath); return 0; }

    // Dev hook: render a QR symbol to PNG, no window needed.
    if (qrShot) {
        ui_shot_qr(qrText, qrOut, qrScale);
        return 0;
    }

    // Remote file-browse self test (client side, no window).
    if (fsList) {
        std::vector<RemoteEntry> ents; std::wstring err;
        if (!net_fs_list(fsIp, fsPort, fsPath, ents, err)) {
            FILE* f = _wfopen(fsOut.c_str(), L"wb");
            if (f) { fprintf(f, "ERROR: %s\n", w_to_utf8(err).c_str()); fclose(f); }
            return 2;
        }
        FILE* f = _wfopen(fsOut.c_str(), L"wb");
        if (f) {
            fprintf(f, "count=%d\n", (int)ents.size());
            for (auto& e : ents) {
                fprintf(f, "[%s] size=%llu  name=%s\n", e.isDir ? "D" : "F",
                        (unsigned long long)e.size, w_to_utf8(e.name).c_str());
            }
            fclose(f);
        }
        return 0;
    }
    if (fsGet) {
        std::wstring err;
        bool ok = net_fs_fetch(fsIp, fsPort, fsPath, fsOut, err, nullptr);
        if (!ok) {
            FILE* f = _wfopen((fsOut + L".err.txt").c_str(), L"wb");
            if (f) { fprintf(f, "ERROR: %s\n", w_to_utf8(err).c_str()); fclose(f); }
            return 2;
        }
        return 0;
    }

    UINT dpi = system_dpi();
    g_dpi = dpi;
    ui_dpi_changed(dpi);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = kClass;
    wc.hbrBackground = CreateSolidBrush(RGB(12, 14, 18));
    RegisterClassExW(&wc);

    // logical design size -> physical pixels
    int lw = 1060, lh = 748;
    int w = MulDiv(lw, dpi, 96), h = MulDiv(lh, dpi, 96);
    g_headless = headless;
    if (headless) { w = shotW; h = shotH; }

    RECT work{ 0, 0, 1920, 1080 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int availW = work.right - work.left, availH = work.bottom - work.top;
    if (w > availW) w = availW;
    if (h > availH) h = availH;
    int x = work.left + (availW - w) / 2;
    int y = work.top + (availH - h) / 2;

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClass, L"AirLink 隔空快传",
                                (headless ? WS_POPUP : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN,
                                x, y, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 0;
    g_hwnd = hwnd;

    if (GetEnvironmentVariableW(L"AIRLINK_LOG", nullptr, 0) > 0) {
        RECT wr{}, cr{};
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        alog(L"--- after CreateWindow ---");
        alog(L"dpi=%u sysdpi=%u logical=%dx%d created=%dx%d at %d,%d",
             g_dpi, system_dpi(), lw, lh, w, h, x, y);
        alog(L"work=%d,%d-%d,%d window=%dx%d client=%dx%d win_dpi=%u",
             (int)work.left, (int)work.top, (int)work.right, (int)work.bottom,
             (int)(wr.right - wr.left), (int)(wr.bottom - wr.top),
             (int)(cr.right - cr.left), (int)(cr.bottom - cr.top),
             GetDpiForWindow(hwnd));
    }

    if (headless) {
        if (demo) ui_demo_state(demoPage);
        ui_shot(shotPath, shotScale);
        if (!dumpPath.empty()) ui_dump_hits(dumpPath);
        return 0;
    }

    if (!netInfoPath.empty()) { net_init(); net_dump_local(netInfoPath); return 0; }

    net_init();

    if (fakePeer) {
        std::lock_guard<std::mutex> lk(g_mu);
        Peer p;
        p.id = "fake-phone";
        p.name = w_to_utf8(fakeName);
        p.ip = fakeIp;
        p.port = fakePort;
        p.firstSeen = p.lastSeen = GetTickCount64();
        g_peers.push_back(p);
    }

    ShowWindow(hwnd, show == 0 ? SW_SHOW : show);
    UpdateWindow(hwnd);

    if (startBrowse) ui_open_browse(startPath);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_running = false;
    return (int)msg.wParam;
}

