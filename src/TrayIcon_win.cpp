#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "volt-ui/TrayIcon.h"

namespace volt {

static UINT g_taskbarRestartMsg = 0;

struct TrayIcon::PlatformData {
    HWND hwnd = nullptr;
    HICON hIcon = nullptr;
    NOTIFYICONDATAA nid = {};
    std::string tooltip;
    bool visible = false;
    std::thread eventThread;
    std::atomic<bool> running{false};
};

static const UINT WM_TRAYICON = WM_APP + 100;

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    TrayIcon* tray = reinterpret_cast<TrayIcon*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (msg == WM_TRAYICON && tray) {
        TrayEvent ev;
        switch (lParam) {
        case WM_LBUTTONUP:     ev.type = TrayEventType::LeftClick; break;
        case WM_RBUTTONUP:     ev.type = TrayEventType::RightClick; break;
        case WM_LBUTTONDBLCLK: ev.type = TrayEventType::DoubleClick; break;
        default: return DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        tray->PushEvent(ev);
        return 0;
    }
    if (msg == WM_COMMAND && HIWORD(wParam) == 0 && tray) {
        TrayEvent ev;
        ev.type = TrayEventType::MenuSelect;
        ev.menuId = std::to_string(LOWORD(wParam));
        tray->PushEvent(ev);
        return 0;
    }
    if (tray && g_taskbarRestartMsg && msg == g_taskbarRestartMsg) {
        tray->Hide(); tray->Show();
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

TrayIcon::TrayIcon()  { m_data = std::make_unique<PlatformData>(); }
TrayIcon::~TrayIcon() { Hide(); if (m_data->hIcon) DestroyIcon(m_data->hIcon); if (m_data->hwnd) DestroyWindow(m_data->hwnd); }
TrayIcon::Ptr TrayIcon::Create() { return std::make_shared<TrayIcon>(); }

bool TrayIcon::PlatformInit(const std::string& iconPath, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;

    HINSTANCE hInst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "TransFlint_TrayClass";
    RegisterClassA(&wc);

    d.hwnd = CreateWindowExA(0, "TransFlint_TrayClass", "TransFlintTray", 0, 0, 0, 0, 0,
                             nullptr, nullptr, hInst, nullptr);
    SetWindowLongPtrA(d.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    g_taskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated");

    d.hIcon = iconPath.empty() ? LoadIconA(nullptr, IDI_APPLICATION) :
              (HICON)LoadImageA(nullptr, iconPath.c_str(), IMAGE_ICON, 16, 16,
                                LR_LOADFROMFILE | LR_DEFAULTSIZE);

    memset(&d.nid, 0, sizeof(d.nid));
    d.nid.cbSize = sizeof(d.nid);
    d.nid.hWnd = d.hwnd;
    d.nid.uID = 1;
    d.nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    d.nid.uCallbackMessage = WM_TRAYICON;
    d.nid.hIcon = d.hIcon;
    strncpy(d.nid.szTip, tooltip.c_str(), sizeof(d.nid.szTip) - 1);
    return true;
}

bool TrayIcon::PlatformInitFromData(const uint8_t* rgba, int w, int h, const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;

    HINSTANCE hInst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "TransFlint_TrayClass";
    RegisterClassA(&wc);

    d.hwnd = CreateWindowExA(0, "TransFlint_TrayClass", "TransFlintTray", 0, 0, 0, 0, 0,
                             nullptr, nullptr, hInst, nullptr);
    SetWindowLongPtrA(d.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    g_taskbarRestartMsg = RegisterWindowMessageA("TaskbarCreated");

    HDC hdc = GetDC(nullptr);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdc, w, h);
    HDC hdcMem = CreateCompatibleDC(hdc);
    SelectObject(hdcMem, hBitmap);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBits(hdcMem, hBitmap, 0, h, rgba, &bmi, DIB_RGB_COLORS);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hBitmap;
    ii.hbmMask = CreateBitmap(w, h, 1, 1, nullptr);
    d.hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBitmap); DeleteObject(ii.hbmMask); DeleteDC(hdcMem); ReleaseDC(nullptr, hdc);

    memset(&d.nid, 0, sizeof(d.nid));
    d.nid.cbSize = sizeof(d.nid);
    d.nid.hWnd = d.hwnd;
    d.nid.uID = 1;
    d.nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    d.nid.uCallbackMessage = WM_TRAYICON;
    d.nid.hIcon = d.hIcon;
    strncpy(d.nid.szTip, tooltip.c_str(), sizeof(d.nid.szTip) - 1);
    return true;
}

void TrayIcon::PlatformShow() {
    auto& d = *m_data;
    Shell_NotifyIconA(NIM_ADD, &d.nid);
    d.visible = true;
}

void TrayIcon::PlatformHide() {
    auto& d = *m_data;
    if (d.visible) { Shell_NotifyIconA(NIM_DELETE, &d.nid); d.visible = false; }
}

void TrayIcon::PlatformUpdateMenu() {
    auto& d = *m_data;
    if (!d.visible || !d.hwnd) return;

    HMENU hMenu = CreatePopupMenu();
    for (size_t i = 0; i < m_menuItems.size(); i++) {
        const auto& item = m_menuItems[i];
        if (item.separator) AppendMenuA(hMenu, MF_SEPARATOR, 0, nullptr);
        else {
            UINT flags = MF_STRING;
            if (!item.enabled) flags |= MF_GRAYED;
            if (item.checked) flags |= MF_CHECKED;
            AppendMenuA(hMenu, flags, (UINT_PTR)i, item.label.c_str());
        }
    }
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(d.hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, d.hwnd, nullptr);
    PostMessageA(d.hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void TrayIcon::PlatformSetTooltip(const std::string& tooltip) {
    auto& d = *m_data;
    d.tooltip = tooltip;
    strncpy(d.nid.szTip, tooltip.c_str(), sizeof(d.nid.szTip) - 1);
    if (d.visible) Shell_NotifyIconA(NIM_MODIFY, &d.nid);
}

void TrayIcon::PlatformSetIcon(const std::string& iconPath) {
    auto& d = *m_data;
    if (d.hIcon) DestroyIcon(d.hIcon);
    d.hIcon = (HICON)LoadImageA(nullptr, iconPath.c_str(), IMAGE_ICON, 16, 16,
                                 LR_LOADFROMFILE | LR_DEFAULTSIZE);
    if (d.hIcon) { d.nid.hIcon = d.hIcon; if (d.visible) Shell_NotifyIconA(NIM_MODIFY, &d.nid); }
}

bool TrayIcon::PlatformIsVisible() const { return m_data->visible; }

// Public wrappers
bool TrayIcon::Init(const std::string& p, const std::string& t) { return PlatformInit(p, t); }
bool TrayIcon::InitFromData(const uint8_t* rgba, int w, int h, const std::string& t) { return PlatformInitFromData(rgba, w, h, t); }
void TrayIcon::Show() { if (!m_visible) { m_visible = true; PlatformShow(); } }
void TrayIcon::UpdateMenu() { PlatformUpdateMenu(); }
void TrayIcon::Hide() { if (m_visible) { m_visible = false; PlatformHide(); } }
bool TrayIcon::IsVisible() const { return PlatformIsVisible(); }
void TrayIcon::SetTooltip(const std::string& s) { PlatformSetTooltip(s); }
void TrayIcon::SetIcon(const std::string& s) { PlatformSetIcon(s); }
void TrayIcon::SetMenu(const std::vector<TrayMenuItem>& items) { m_menuItems = items; }
void TrayIcon::PollEvents() {
    if (!m_data->hwnd) return;
    MSG msg;
    while (PeekMessageA(&msg, m_data->hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg); DispatchMessageA(&msg);
    }
}
void TrayIcon::PushEvent(TrayEvent ev) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    m_eventQueue.push_back(ev);
}
bool TrayIcon::PopEvent(TrayEvent& ev) {
    std::lock_guard<std::mutex> lock(m_eventMutex);
    if (m_eventQueue.empty()) return false;
    ev = m_eventQueue.front(); m_eventQueue.pop_front(); return true;
}
void TrayIcon::Process() { PollEvents(); }

} // namespace volt
#endif
