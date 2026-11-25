/*
 * m280bat - mostra a bateria de mouses Logitech (receptor nano/Unifying)
 * na bandeja do Windows (perto do relógio). Win32 puro, sem dependências.
 *
 * Fala HID++ direto com o receptor:
 *   HID++ 2.0: recursos 0x1004 (Unified Battery), 0x1000 (Battery Status),
 *              0x1001 (Battery Voltage)
 *   HID++ 1.0: registradores 0x0D (carga %) e 0x07 (nível)
 *
 * Compilar (MinGW):
 *   x86_64-w64-mingw32-gcc -municode -Os -s -mwindows m280bat.c -o m280bat.exe \
 *       -lhid -lsetupapi -lgdi32 -lshell32 -ladvapi32
 */
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define APP_NAME     L"M280Bateria"
#define WM_TRAY      (WM_APP + 1)
#define WM_RESULT    (WM_APP + 2)
#define ID_REFRESH   1
#define ID_STARTUP   2
#define ID_EXIT      3
#define POLL_OK_MS   (5 * 60 * 1000)   /* 5 min quando está tudo bem      */
#define POLL_FAIL_MS (30 * 1000)       /* 30 s se o mouse estiver dormindo */
#define TIMEOUT_MS   1200

/* ------------------------------------------------------------------ HID */

typedef struct { HANDLE hs, hl; } Rx;   /* coleções curta (0x10) e longa (0x11) */

static void path_key(const wchar_t *p, wchar_t *key, size_t n)
{
    size_t i = 0;
    for (; p[i] && i < n - 1; i++) key[i] = (wchar_t)towlower(p[i]);
    key[i] = 0;
    wchar_t *c = wcsstr(key, L"&col");
    if (c && wcslen(c) >= 6) wmemmove(c, c + 6, wcslen(c + 6) + 1);
    wchar_t *g = wcsstr(key, L"#{");
    if (g) {
        *g = 0;
        wchar_t *a = wcsrchr(key, L'&'), *h = wcsrchr(key, L'#');
        if (a && h && a > h) *a = 0;
    }
}

static int find_receivers(Rx *out, int max)
{
    struct { wchar_t key[512]; HANDLE h; int type; } cand[32];
    int nc = 0, n = 0;
    GUID g;
    HidD_GetHidGuid(&g);
    HDEVINFO di = SetupDiGetClassDevsW(&g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE) return 0;

    SP_DEVICE_INTERFACE_DATA ifd = { sizeof ifd };
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(di, NULL, &g, i, &ifd); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &ifd, NULL, 0, &need, NULL);
        if (!need) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W d = malloc(need);
        if (!d) continue;
        d->cbSize = sizeof(*d);
        if (SetupDiGetDeviceInterfaceDetailW(di, &ifd, d, need, NULL, NULL)) {
            wchar_t low[64] = {0};
            for (int k = 0; k < 63 && d->DevicePath[k]; k++) low[k] = (wchar_t)towlower(d->DevicePath[k]);
            if (wcsstr(low, L"vid_046d")) {
                HANDLE h = CreateFileW(d->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    int type = 0;
                    PHIDP_PREPARSED_DATA pp;
                    if (HidD_GetPreparsedData(h, &pp)) {
                        HIDP_CAPS c;
                        if (HidP_GetCaps(pp, &c) == HIDP_STATUS_SUCCESS && c.UsagePage == 0xFF00) {
                            if (c.Usage == 1 && c.OutputReportByteLength == 7)  type = 1;
                            if (c.Usage == 2 && c.OutputReportByteLength == 20) type = 2;
                        }
                        HidD_FreePreparsedData(pp);
                    }
                    if (type && nc < 32) {
                        path_key(d->DevicePath, cand[nc].key, 512);
                        cand[nc].h = h; cand[nc].type = type; nc++;
                    } else CloseHandle(h);
                }
            }
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(di);

    /* junta curta + longa do mesmo receptor */
    for (int s = 0; s < nc; s++) {
        if (cand[s].type != 1 || !cand[s].h) continue;
        for (int l = 0; l < nc; l++) {
            if (cand[l].type == 2 && cand[l].h && !wcscmp(cand[s].key, cand[l].key) && n < max) {
                out[n].hs = cand[s].h; out[n].hl = cand[l].h; n++;
                cand[s].h = cand[l].h = NULL;
                break;
            }
        }
    }
    /* plano B: um de cada sobrando */
    if (n == 0) {
        int s1 = -1, l1 = -1, cs = 0, cl = 0;
        for (int k = 0; k < nc; k++) {
            if (cand[k].type == 1) { s1 = k; cs++; }
            if (cand[k].type == 2) { l1 = k; cl++; }
        }
        if (cs == 1 && cl == 1 && max > 0) {
            out[0].hs = cand[s1].h; out[0].hl = cand[l1].h; n = 1;
            cand[s1].h = cand[l1].h = NULL;
        }
    }
    for (int k = 0; k < nc; k++) if (cand[k].h) CloseHandle(cand[k].h);
    return n;
}

typedef struct { HANDLE h; OVERLAPPED ov; BYTE buf[64]; BOOL pending; } Rd;

static BOOL rd_start(Rd *r)
{
    DWORD n;
    ResetEvent(r->ov.hEvent);
    r->pending = FALSE;
    if (ReadFile(r->h, r->buf, sizeof r->buf, &n, &r->ov) || GetLastError() == ERROR_IO_PENDING) {
        r->pending = TRUE;
        return TRUE;
    }
    return FALSE;
}

static BOOL write_report(HANDLE h, const BYTE *b, DWORD len)
{
    OVERLAPPED ov = {0};
    DWORD n = 0;
    ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    BOOL ok = WriteFile(h, b, len, &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, TIMEOUT_MS) == WAIT_OBJECT_0)
            ok = GetOverlappedResult(h, &ov, &n, FALSE);
        else { CancelIo(h); GetOverlappedResult(h, &ov, &n, TRUE); ok = FALSE; }
    }
    CloseHandle(ov.hEvent);
    return ok;
}

/* envia pedido curto; 1 = resposta, 0 = erro HID++, -1 = sem resposta */
static int xfer(Rx *rx, BYTE dev, BYTE b2, BYTE b3, const BYTE *p, int np, BYTE *resp)
{
    BYTE req[7] = { 0x10, dev, b2, b3, 0, 0, 0 };
    if (p && np > 0) memcpy(req + 4, p, np > 3 ? 3 : np);

    Rd r[2];
    memset(r, 0, sizeof r);
    r[0].h = rx->hs; r[1].h = rx->hl;
    for (int i = 0; i < 2; i++) {
        r[i].ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        rd_start(&r[i]);
    }

    int res = -1;
    if (r[0].pending && r[1].pending && write_report(rx->hs, req, 7)) {
        DWORD t0 = GetTickCount();
        for (;;) {
            DWORD el = GetTickCount() - t0;
            if (el >= TIMEOUT_MS) break;
            HANDLE ev[2] = { r[0].ov.hEvent, r[1].ov.hEvent };
            DWORD w = WaitForMultipleObjects(2, ev, FALSE, TIMEOUT_MS - el);
            if (w != WAIT_OBJECT_0 && w != WAIT_OBJECT_0 + 1) break;
            int i = (int)(w - WAIT_OBJECT_0);
            DWORD n = 0;
            BOOL ok = GetOverlappedResult(r[i].h, &r[i].ov, &n, FALSE);
            r[i].pending = FALSE;
            if (!ok) break;
            BYTE *b = r[i].buf;
            if (n >= 7 && b[1] == dev) {
                if (b[0] == 0x10 && b[2] == 0x8F && b[3] == b2 && b[4] == b3) { res = 0; break; }
                if (b[0] == 0x11 && b[2] == 0xFF && b[3] == b2 && b[4] == b3) { res = 0; break; }
                if ((b[0] == 0x10 || b[0] == 0x11) && b[2] == b2 && b[3] == b3) {
                    memset(resp, 0, 20);
                    memcpy(resp, b, n < 20 ? n : 20);
                    res = 1; break;
                }
            }
            if (!rd_start(&r[i])) break;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (r[i].pending) {
            DWORD n;
            CancelIo(r[i].h);
            GetOverlappedResult(r[i].h, &r[i].ov, &n, TRUE);
        }
        CloseHandle(r[i].ov.hEvent);
    }
    return res;
}

/* ------------------------------------------------------------- bateria */

typedef struct { int ok, pct, mv, approx, receivers; } Result;

static int interp(int x, const int (*t)[2], int n)   /* tabela em ordem decrescente */
{
    if (x >= t[0][0]) return t[0][1];
    for (int i = 1; i < n; i++)
        if (x >= t[i][0])
            return t[i][1] + (x - t[i][0]) * (t[i-1][1] - t[i][1]) / (t[i-1][0] - t[i][0]);
    return 0;
}

static int volt_pct(int mv)
{
    static const int aa[][2]   = { {1550,100},{1450,80},{1350,55},{1250,30},{1150,10},{1050,0} };
    static const int lipo[][2] = { {4150,100},{4000,85},{3900,70},{3800,50},{3750,35},{3700,20},{3600,5},{3500,0} };
    return mv < 2500 ? interp(mv, aa, 6) : interp(mv, lipo, 8);
}

static int levels_pct(BYTE f)   /* 0x1004: bit3 cheio, bit2 bom, bit1 baixo, bit0 crítico */
{
    if (f & 8) return 90;
    if (f & 4) return 50;
    if (f & 2) return 20;
    if (f & 1) return 5;
    return -1;
}

static int read_battery(Rx *rx, BYTE dev, Result *o)
{
    BYTE r[20];
    BYTE ping[3] = { 0, 0, 0x5A };
    int v = xfer(rx, dev, 0x00, 0x1A, ping, 3, r);

    if (v == 1 && r[4] >= 2) {                       /* HID++ 2.0 */
        static const WORD feats[3] = { 0x1004, 0x1000, 0x1001 };
        for (int k = 0; k < 3; k++) {
            BYTE fp[2] = { (BYTE)(feats[k] >> 8), (BYTE)feats[k] };
            if (xfer(rx, dev, 0x00, 0x0A, fp, 2, r) != 1 || !r[4]) continue;
            BYTE idx = r[4];
            BYTE fn  = feats[k] == 0x1004 ? 0x1A : 0x0A;   /* função 1 / 0 */
            if (xfer(rx, dev, idx, fn, NULL, 0, r) != 1) continue;
            if (feats[k] == 0x1004) {
                o->pct = r[4];
                if (!o->pct) { o->pct = levels_pct(r[5]); o->approx = 1; }
            } else if (feats[k] == 0x1000) {
                o->pct = r[4];
            } else {
                o->mv = (r[4] << 8) | r[5];
                o->pct = volt_pct(o->mv); o->approx = 1;
            }
            if (o->pct < 0 || o->pct > 100) continue;
            return 1;
        }
        return 0;
    }
    if (v == 0) {                                    /* HID++ 1.0 (ou desligado) */
        if (xfer(rx, dev, 0x81, 0x0D, NULL, 0, r) == 1 && r[4] && r[4] <= 100) {
            o->pct = r[4]; return 1;
        }
        if (xfer(rx, dev, 0x81, 0x07, NULL, 0, r) == 1 && r[4]) {
            BYTE l = r[4];
            o->pct = l >= 7 ? 90 : l >= 5 ? 50 : l >= 3 ? 20 : 5;
            o->approx = 1; return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ UI */

static HWND g_hwnd;
static UINT g_taskbar_created;
static NOTIFYICONDATAW g_nid;
static volatile LONG g_busy;
static int g_dev_cache = 1;
static int g_warned;

static DWORD WINAPI worker(LPVOID unused)
{
    (void)unused;
    Result *res = calloc(1, sizeof *res);
    Rx rx[8];
    int n = find_receivers(rx, 8);
    res->receivers = n;
    for (int i = 0; i < n && !res->ok; i++)
        for (int k = 0; k < 6 && !res->ok; k++) {
            int dev = ((g_dev_cache - 1 + k) % 6) + 1;
            Result t = {0};
            if (read_battery(&rx[i], (BYTE)dev, &t)) {
                t.ok = 1; t.receivers = n; *res = t; g_dev_cache = dev;
            }
        }
    for (int i = 0; i < n; i++) { CloseHandle(rx[i].hs); CloseHandle(rx[i].hl); }
    if (!PostMessageW(g_hwnd, WM_RESULT, 0, (LPARAM)res)) free(res);
    InterlockedExchange(&g_busy, 0);
    return 0;
}

static void start_poll(void)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    HANDLE t = CreateThread(NULL, 64 * 1024, worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (t) CloseHandle(t); else InterlockedExchange(&g_busy, 0);
}

static HICON make_icon(const wchar_t *txt, COLORREF col)
{
    int s = GetSystemMetrics(SM_CXSMICON);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = s; bi.bmiHeader.biHeight = -s;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ ob = SelectObject(dc, bmp);
    memset(bits, 0, (size_t)s * s * 4);

    int len = (int)wcslen(txt);
    int fh = len >= 3 ? s * 62 / 100 : s * 88 / 100;
    HFONT f = CreateFontW(-fh, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0,
                          ANTIALIASED_QUALITY, 0, L"Segoe UI");
    HGDIOBJ of = SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rc = { 0, 0, s, s };
    DrawTextW(dc, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();

    DWORD *px = bits;
    BYTE R = GetRValue(col), G = GetGValue(col), B = GetBValue(col);
    for (int i = 0; i < s * s; i++) {
        DWORD p = px[i];
        BYTE a = (BYTE)max(max(p & 0xFF, (p >> 8) & 0xFF), (p >> 16) & 0xFF);
        px[i] = ((DWORD)a << 24) | ((DWORD)(R * a / 255) << 16) | ((DWORD)(G * a / 255) << 8) | (B * a / 255);
    }
    SelectObject(dc, of); DeleteObject(f);
    SelectObject(dc, ob);

    BYTE *zeros = calloc((size_t)((s + 15) / 16 * 2) * s, 1);
    HBITMAP mask = CreateBitmap(s, s, 1, 1, zeros);
    ICONINFO ii = { TRUE, 0, 0, mask, bmp };
    HICON ic = CreateIconIndirect(&ii);
    DeleteObject(mask); DeleteObject(bmp); DeleteDC(dc); free(zeros);
    return ic;
}

static void set_tray(const wchar_t *txt, COLORREF col, const wchar_t *tip)
{
    HICON old = g_nid.hIcon;
    g_nid.hIcon = make_icon(txt, col);
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    lstrcpynW(g_nid.szTip, tip, ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    if (old) DestroyIcon(old);
}

static void add_tray(void)
{
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void balloon(const wchar_t *title, const wchar_t *msg)
{
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_WARNING;
    lstrcpynW(g_nid.szInfoTitle, title, ARRAYSIZE(g_nid.szInfoTitle));
    lstrcpynW(g_nid.szInfo, msg, ARRAYSIZE(g_nid.szInfo));
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void apply_result(Result *r)
{
    wchar_t tip[128], txt[8];
    if (r->ok) {
        COLORREF c = r->pct > 50 ? RGB(80, 220, 100) : r->pct > 20 ? RGB(255, 190, 0) : RGB(255, 70, 70);
        wsprintfW(txt, L"%d", r->pct);
        if (r->mv)
            wsprintfW(tip, L"Mouse Logitech: %d%%%s\nPilha: %d,%02d V", r->pct,
                      r->approx ? L" (aprox.)" : L"", r->mv / 1000, (r->mv % 1000) / 10);
        else
            wsprintfW(tip, L"Mouse Logitech: %d%%%s", r->pct, r->approx ? L" (aprox.)" : L"");
        set_tray(txt, c, tip);
        if (r->pct <= 15 && !g_warned) {
            wsprintfW(tip, L"Troque a pilha do mouse (%d%%).", r->pct);
            balloon(L"Bateria fraca", tip);
            g_warned = 1;
        } else if (r->pct > 30) g_warned = 0;
        SetTimer(g_hwnd, 1, POLL_OK_MS, NULL);
    } else {
        set_tray(L"?", RGB(170, 170, 170), r->receivers
                 ? L"Mouse sem resposta (mexa nele para acordar)"
                 : L"Receptor Logitech não encontrado");
        SetTimer(g_hwnd, 1, POLL_FAIL_MS, NULL);
    }
}

static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static BOOL startup_enabled(void)
{
    HKEY k; BOOL on = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExW(k, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

static void toggle_startup(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (startup_enabled()) RegDeleteValueW(k, APP_NAME);
    else {
        wchar_t path[MAX_PATH], val[MAX_PATH + 2];
        GetModuleFileNameW(NULL, path, MAX_PATH);
        wsprintfW(val, L"\"%s\"", path);
        RegSetValueExW(k, APP_NAME, 0, REG_SZ, (BYTE *)val, (DWORD)(wcslen(val) + 1) * sizeof(wchar_t));
    }
    RegCloseKey(k);
}

static void show_menu(HWND h)
{
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_REFRESH, L"Atualizar agora");
    AppendMenuW(m, MF_STRING | (startup_enabled() ? MF_CHECKED : 0), ID_STARTUP, L"Iniciar com o Windows");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_EXIT, L"Sair");
    SetForegroundWindow(h);
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, h, NULL);
    PostMessageW(h, WM_NULL, 0, 0);
    DestroyMenu(m);
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_TIMER:  start_poll(); return 0;
    case WM_RESULT: apply_result((Result *)lp); free((void *)lp); return 0;
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) show_menu(h);
        else if (lp == WM_LBUTTONDBLCLK) start_poll();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_REFRESH: start_poll(); break;
        case ID_STARTUP: toggle_startup(); break;
        case ID_EXIT:    DestroyWindow(h); break;
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC) SetTimer(h, 1, 10000, NULL);
        return TRUE;
    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_taskbar_created) { add_tray(); return 0; }
    return DefWindowProcW(h, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev; (void)cmd; (void)show;
    CreateMutexW(NULL, TRUE, L"Local\\" APP_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    SetProcessDPIAware();

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hi;
    wc.lpszClassName = APP_NAME;
    RegisterClassW(&wc);
    g_hwnd = CreateWindowW(APP_NAME, APP_NAME, 0, 0, 0, 0, 0, NULL, NULL, hi, NULL);
    g_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    g_nid.cbSize = sizeof g_nid;
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = make_icon(L"…", RGB(170, 170, 170));
    lstrcpyW(g_nid.szTip, L"Lendo bateria do mouse…");
    add_tray();
    start_poll();

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
