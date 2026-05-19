#include "App.h"
#include "CaptureEngine.h"
#include "TrayIcon.h"
#include "HotkeyDialog.h"
#include <atlapp.h>
#include <wrl/client.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <strsafe.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

using Microsoft::WRL::ComPtr;

extern WTL::CAppModule _Module;

App::App()  = default;
App::~App() = default;

int App::Run(HINSTANCE) {
    cfg_ = Config::Load();
    cfg_.EnsureDirectories();

    engine_ = std::make_unique<CaptureEngine>(cfg_);
    if (!engine_->Initialize()) {
        MessageBoxW(nullptr,
            L"Failed to initialise the capture engine.\n"
            L"Souions including attemping a relaunch or installing a new github release (HarshTheSharma/cliplite).",
            L"ClipLite", MB_OK | MB_ICONERROR);
        return 1;
    }

    tray_ = std::make_unique<TrayWindow>();
    tray_->cfg = &cfg_;

    tray_->onDuration       = [this](int s)  { OnDuration(s); };
    tray_->onSaveLocation   = [this]()        { OnSaveLocation(); };
    tray_->onEnableMic      = [this](bool e)  { OnEnableMic(e); };
    tray_->onChangeHotkey   = [this]()        { OnChangeHotkey(); };
    tray_->onOpenSaveFolder = [this]()        { OnOpenSaveFolder(); };
    tray_->onOpenConfigFile = [this]()        { OnOpenConfigFile(); };
    tray_->onQuit           = [this]()        { OnQuit(); };
    tray_->onHotkey         = [this]() {
        PlaySoundW(MAKEINTRESOURCEW(IDR_CLIP_SOUND),
           GetModuleHandle(nullptr),
           SND_RESOURCE | SND_ASYNC);
        engine_->RequestClipSave();
    };

    if (!tray_->Create()) return 1;
    tray_hwnd_ = tray_->m_hWnd;

    if (!RegisterHotkey()) {
        MessageBoxW(nullptr,
            L"Could not register the hotkey — it may be in use by another application.\n"
            L"Use Tray → Change Hotkey to pick a different combination.",
            L"ClipLite", MB_OK | MB_ICONWARNING);
    }

    engine_->Start();

    WTL::CMessageLoop loop;
    _Module.AddMessageLoop(&loop);
    int ret = loop.Run();
    _Module.RemoveMessageLoop();

    UnregisterHotkey();
    engine_->Stop();
    cfg_.Save();
    tray_->Destroy();
    return ret;
}

void App::OnDuration(int seconds) {
    cfg_.duration_seconds = seconds;
    cfg_.Save();
}

void App::OnSaveLocation() {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dlg.GetAddressOf()))))
        return;

    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Choose Save Folder");

    if (FAILED(dlg->Show(tray_hwnd_))) return;

    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(item.GetAddressOf()))) return;

    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
        cfg_.save_path = path;
        CoTaskMemFree(path);
        cfg_.EnsureDirectories();
        cfg_.Save();
    }
}

void App::OnEnableMic(bool enabled) {
    cfg_.enable_mic = enabled;
    cfg_.Save();
}

void App::OnChangeHotkey() {
    HotkeyDialog dlg;
    dlg.mod_str = cfg_.hotkey_mod;
    dlg.key_str = cfg_.hotkey_key;

    if (dlg.DoModal(tray_hwnd_) == IDOK && dlg.accepted) {
        UnregisterHotkey();
        cfg_.hotkey_mod = dlg.mod_str;
        cfg_.hotkey_key = dlg.key_str;
        cfg_.Save();
        if (!RegisterHotkey()) {
            MessageBoxW(tray_hwnd_,
                L"The chosen hotkey is already in use by another application.",
                L"ClipLite", MB_OK | MB_ICONWARNING);
        }
    }
}

void App::OnOpenSaveFolder() {
    ShellExecuteW(nullptr, L"open", cfg_.save_path.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void App::OnOpenConfigFile() {
    const auto path = Config::ConfigFilePath().wstring();
    ShellExecuteW(nullptr, L"open", path.c_str(),
                  nullptr, nullptr, SW_SHOWNORMAL);
}

void App::OnQuit() {
    PostQuitMessage(0);
}

UINT App::ModStringToFlags(const std::wstring& s) {
    UINT m = 0;
    if (s.find(L"CTRL")  != std::wstring::npos) m |= MOD_CONTROL;
    if (s.find(L"ALT")   != std::wstring::npos) m |= MOD_ALT;
    if (s.find(L"SHIFT") != std::wstring::npos) m |= MOD_SHIFT;
    if (s.find(L"WIN")   != std::wstring::npos) m |= MOD_WIN;
    return m;
}

UINT App::KeyStringToVk(const std::wstring& key) {
    if (key.size() == 1) {
        wchar_t c = key[0];
        if (c >= L'A' && c <= L'Z') return (UINT)c;
        if (c >= L'0' && c <= L'9') return (UINT)c;
    }
    if (key.size() >= 2 && key[0] == L'F') {
        int n = _wtoi(key.c_str() + 1);
        if (n >= 1 && n <= 24) return VK_F1 + n - 1;
    }
    // Numpad: "Num0".."Num9"
    if (key.size() >= 4 && key.substr(0, 3) == L"Num" && key[3] >= L'0' && key[3] <= L'9' && key.size() == 4)
        return VK_NUMPAD0 + (key[3] - L'0');

    static const struct { const wchar_t* name; UINT vk; } table[] = {
        {L"Space",   VK_SPACE},  {L"Home",    VK_HOME},   {L"End",    VK_END},
        {L"PageUp",  VK_PRIOR},  {L"PageDown",VK_NEXT},   {L"Insert", VK_INSERT},
        {L"Delete",  VK_DELETE}, {L"Left",    VK_LEFT},   {L"Right",  VK_RIGHT},
        {L"Up",      VK_UP},     {L"Down",    VK_DOWN},   {L"Tab",    VK_TAB},
        {L"Enter",   VK_RETURN}, {L"-",       VK_OEM_MINUS}, {L"=",   VK_OEM_PLUS},
        {L",",       VK_OEM_COMMA}, {L".",    VK_OEM_PERIOD},{L";",   VK_OEM_1},
        {L"/",       VK_OEM_2},  {L"`",       VK_OEM_3},  {L"[",     VK_OEM_4},
        {L"\\",      VK_OEM_5},  {L"]",       VK_OEM_6},  {L"'",     VK_OEM_7},
        {L"Num*",       VK_MULTIPLY}, {L"Num+",      VK_ADD},      {L"Num-",  VK_SUBTRACT},
        {L"Num.",       VK_DECIMAL},  {L"Num/",      VK_DIVIDE},
        {L"Backspace",  VK_BACK},     {L"PrintScreen",VK_SNAPSHOT}, {L"ScrollLock",VK_SCROLL},
        {L"Pause",      VK_PAUSE},
    };
    for (const auto& e : table)
        if (key == e.name) return e.vk;
    return 0;
}

bool App::RegisterHotkey() {
    const UINT mods = ModStringToFlags(cfg_.hotkey_mod) | MOD_NOREPEAT;
    const UINT vk   = KeyStringToVk(cfg_.hotkey_key);
    if (!vk) return false;
    if (::RegisterHotKey(tray_hwnd_, hotkey_id_, mods, vk)) {
        hotkey_reg_ = true;
        return true;
    }
    return false;
}

void App::UnregisterHotkey() {
    if (hotkey_reg_ && tray_hwnd_)
        ::UnregisterHotKey(tray_hwnd_, hotkey_id_);
    hotkey_reg_ = false;
}
