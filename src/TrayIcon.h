#pragma once
#define _ATL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>
#include <atlapp.h>   // ATL::CAppModule, CMessageLoop (WTL)
#include <atlwin.h>   // CWindowImpl, CWindow, message-map macros
#include <shellapi.h>
#include <functional>
#include <string>
#include "Config.h"
#include "resource.h"

extern WTL::CAppModule _Module;

// Hidden message window that owns the tray icon and receives WM_HOTKEY.
class TrayWindow : public ATL::CWindowImpl<TrayWindow> {
public:
    DECLARE_WND_CLASS(L"ClipLiteTrayWnd")

    // Callbacks wired by App before calling Create().
    std::function<void(int seconds)>  onDuration;
    std::function<void()>             onSaveLocation;
    std::function<void(bool enabled)> onEnableMic;
    std::function<void()>             onChangeHotkey;
    std::function<void()>             onOpenSaveFolder;
    std::function<void()>             onOpenConfigFile;
    std::function<void()>             onQuit;
    std::function<void()>             onHotkey;

    Config* cfg = nullptr; // pointer to live config for menu state

    bool Create();
    void Destroy();
    void UpdateMicCheck();

    BEGIN_MSG_MAP(TrayWindow)
        MESSAGE_HANDLER(WM_TRAYICON, OnTrayIcon)
        MESSAGE_HANDLER(WM_HOTKEY,   OnHotkey)
        MESSAGE_HANDLER(WM_DESTROY,  OnDestroy)
        COMMAND_RANGE_HANDLER(ID_TRAY_DURATION_FIRST, ID_TRAY_DURATION_LAST, OnDuration)
        COMMAND_ID_HANDLER(ID_TRAY_SAVELOCATION,    OnSaveLocation)
        COMMAND_ID_HANDLER(ID_TRAY_ENABLEMIC,       OnEnableMic)
        COMMAND_ID_HANDLER(ID_TRAY_CHANGEHOTKEY,    OnChangeHotkey)
        COMMAND_ID_HANDLER(ID_TRAY_OPENSAVEFOLDER,   OnOpenSaveFolder)
        COMMAND_ID_HANDLER(ID_TRAY_OPENCONFIGFILE,  OnOpenConfigFile)
        COMMAND_ID_HANDLER(ID_TRAY_STARTWITHWINDOWS, OnStartWithWindows)
        COMMAND_ID_HANDLER(ID_TRAY_QUIT,            OnQuit)
    END_MSG_MAP()

private:
    LRESULT OnTrayIcon(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnHotkey(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDuration(WORD, WORD wID, HWND, BOOL&);
    LRESULT OnSaveLocation(WORD, WORD, HWND, BOOL&);
    LRESULT OnEnableMic(WORD, WORD, HWND, BOOL&);
    LRESULT OnChangeHotkey(WORD, WORD, HWND, BOOL&);
    LRESULT OnOpenSaveFolder(WORD, WORD, HWND, BOOL&);
    LRESULT OnOpenConfigFile(WORD, WORD, HWND, BOOL&);
    LRESULT OnStartWithWindows(WORD, WORD, HWND, BOOL&);
    LRESULT OnQuit(WORD, WORD, HWND, BOOL&);

    void ShowContextMenu();
    void AddTrayIcon();
    void RemoveTrayIcon();

    NOTIFYICONDATAW nid_{};
    UINT            taskbar_created_msg_ = 0;
};
