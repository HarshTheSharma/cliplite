#include "TrayIcon.h"
#include <strsafe.h>
#include <shlobj.h>
#include <string>

static const int kDurations[] = { 30,60,90,120,150,180,210,240,270,300 };

// ── Startup registry helpers ──────────────────────────────────────────────────
// Uses HKCU Run key — no elevation required.

static const wchar_t* kRunKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunName = L"ClipLite";

static bool IsStartupEnabled() {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &hk) != ERROR_SUCCESS)
        return false;
    wchar_t stored[MAX_PATH + 4]{};
    DWORD size = sizeof(stored);
    bool found = RegQueryValueExW(hk, kRunName, nullptr, nullptr,
                                  reinterpret_cast<BYTE*>(stored), &size) == ERROR_SUCCESS;
    RegCloseKey(hk);
    if (!found) return false;
    // stale entry pointing to a different exe counts as disabled
    wchar_t current[MAX_PATH];
    GetModuleFileNameW(nullptr, current, MAX_PATH);
    std::wstring expected = std::wstring(L"\"") + current + L"\"";
    return _wcsicmp(stored, expected.c_str()) == 0;
}

static void SetStartup(bool enable) {
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hk) != ERROR_SUCCESS)
        return;
    if (enable) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring val = std::wstring(L"\"") + path + L"\"";
        RegSetValueExW(hk, kRunName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(val.c_str()),
            static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hk, kRunName);
    }
    RegCloseKey(hk);
}

bool TrayWindow::Create() {
    taskbar_created_msg_ = RegisterWindowMessageW(L"TaskbarCreated");

    ATL::CWindowImpl<TrayWindow>::Create(
        nullptr, ATL::CWindow::rcDefault, nullptr,
        WS_POPUP, 0);
    if (!m_hWnd) return false;

    AddTrayIcon();
    return true;
}

void TrayWindow::Destroy() {
    RemoveTrayIcon();
    if (m_hWnd) DestroyWindow();
}

void TrayWindow::AddTrayIcon() {
    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = m_hWnd;
    nid_.uID              = 1;
    nid_.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_TRAYICON;
    nid_.hIcon = LoadIconW(GetModuleHandle(nullptr),
                           MAKEINTRESOURCEW(IDI_APPICON));
    StringCchCopyW(nid_.szTip, ARRAYSIZE(nid_.szTip), L"ClipLite");
    Shell_NotifyIconW(NIM_ADD, &nid_);

    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);
}

void TrayWindow::RemoveTrayIcon() {
    Shell_NotifyIconW(NIM_DELETE, &nid_);
}

void TrayWindow::UpdateMicCheck() {
    // State reflected the next time the menu is opened.
}

// ── Tray notification ─────────────────────────────────────────────────────────

LRESULT TrayWindow::OnTrayIcon(UINT, WPARAM, LPARAM lParam, BOOL& bHandled) {
    if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
        ShowContextMenu();
    bHandled = TRUE;
    return 0;
}

void TrayWindow::ShowContextMenu() {
    HMENU menu = LoadMenuW(GetModuleHandle(nullptr),
                           MAKEINTRESOURCEW(IDR_TRAYMENU));
    if (!menu) return;
    HMENU popup = GetSubMenu(menu, 0);

    if (cfg) {
        for (int i = 0; i < 10; ++i) {
            UINT flag = (kDurations[i] == cfg->duration_seconds) ? MF_CHECKED : MF_UNCHECKED;
            CheckMenuItem(popup, ID_TRAY_DURATION_FIRST + i, MF_BYCOMMAND | flag);
        }
        CheckMenuItem(popup, ID_TRAY_ENABLEMIC,
                      MF_BYCOMMAND | (cfg->enable_mic ? MF_CHECKED : MF_UNCHECKED));
    }
    CheckMenuItem(popup, ID_TRAY_STARTWITHWINDOWS,
                  MF_BYCOMMAND | (IsStartupEnabled() ? MF_CHECKED : MF_UNCHECKED));

    SetForegroundWindow(m_hWnd);
    POINT pt{};
    GetCursorPos(&pt);
    TrackPopupMenu(popup, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_hWnd, nullptr);
    PostMessage(WM_NULL, 0, 0);

    DestroyMenu(menu);
}

// ── Hotkey ────────────────────────────────────────────────────────────────────

LRESULT TrayWindow::OnHotkey(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    if (onHotkey) onHotkey();
    bHandled = TRUE;
    return 0;
}

// ── WM_DESTROY ────────────────────────────────────────────────────────────────

LRESULT TrayWindow::OnDestroy(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    RemoveTrayIcon();
    PostQuitMessage(0);
    bHandled = TRUE;
    return 0;
}

// ── Command handlers ──────────────────────────────────────────────────────────

LRESULT TrayWindow::OnDuration(WORD, WORD wID, HWND, BOOL& bHandled) {
    int idx = (int)wID - (int)ID_TRAY_DURATION_FIRST;
    if (idx >= 0 && idx < 10 && onDuration)
        onDuration(kDurations[idx]);
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnSaveLocation(WORD, WORD, HWND, BOOL& bHandled) {
    if (onSaveLocation) onSaveLocation();
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnEnableMic(WORD, WORD, HWND, BOOL& bHandled) {
    if (cfg && onEnableMic) onEnableMic(!cfg->enable_mic);
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnChangeHotkey(WORD, WORD, HWND, BOOL& bHandled) {
    if (onChangeHotkey) onChangeHotkey();
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnOpenSaveFolder(WORD, WORD, HWND, BOOL& bHandled) {
    if (onOpenSaveFolder) onOpenSaveFolder();
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnOpenConfigFile(WORD, WORD, HWND, BOOL& bHandled) {
    if (onOpenConfigFile) onOpenConfigFile();
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnStartWithWindows(WORD, WORD, HWND, BOOL& bHandled) {
    SetStartup(!IsStartupEnabled());
    bHandled = TRUE;
    return 0;
}

LRESULT TrayWindow::OnQuit(WORD, WORD, HWND, BOOL& bHandled) {
    if (onQuit) onQuit();
    bHandled = TRUE;
    return 0;
}
