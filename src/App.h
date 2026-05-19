#pragma once
#define _ATL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>
#include <atlapp.h>
#include <windows.h>
#include <memory>
#include <string>
#include "Config.h"

class CaptureEngine;
class TrayWindow;

class App {
public:
    App();
    ~App();

    int Run(HINSTANCE hInst);

private:
    // Tray callbacks
    void OnDuration(int seconds);
    void OnSaveLocation();
    void OnEnableMic(bool enabled);
    void OnChangeHotkey();
    void OnOpenSaveFolder();
    void OnOpenConfigFile();
    void OnQuit();

    // Hotkey management
    bool RegisterHotkey();
    void UnregisterHotkey();

    // Utility: parse "CTRL+ALT" → MOD_CONTROL | MOD_ALT
    static UINT ModStringToFlags(const std::wstring& mod);
    // Parse "F10" / "A" / etc. → virtual key code
    static UINT KeyStringToVk(const std::wstring& key);

    Config                      cfg_;
    std::unique_ptr<CaptureEngine> engine_;
    std::unique_ptr<TrayWindow>    tray_;

    HWND tray_hwnd_   = nullptr;
    int  hotkey_id_   = 1; // ID passed to RegisterHotKey
    bool hotkey_reg_  = false;
};
