#define _ATL_NO_AUTOMATIC_NAMESPACE
#define ATL_NO_ASSERT_ON_DESTROY_NONEXISTENT_WINDOW

#include <atlbase.h>
#include <atlapp.h>   // WTL::CAppModule, WTL::CMessageLoop
#include <mfapi.h>
#include <commctrl.h>

#include "App.h"

// must exist before any ATL window class is registered
WTL::CAppModule _Module;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ClipLiteSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) return 1;

    hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { CoUninitialize(); return 1; }

    // needed for the button and edit controls in HotkeyDialog
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    _Module.Init(nullptr, hInstance);

    int ret = App{}.Run(hInstance);

    _Module.Term();
    MFShutdown();
    CoUninitialize();
    CloseHandle(mutex);
    return ret;
}
