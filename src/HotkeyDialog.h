#pragma once
#define _ATL_NO_AUTOMATIC_NAMESPACE
#include <atlbase.h>
#include <atlwin.h>
#include <string>
#include "resource.h"

// DoModal() returns IDOK when accepted; read mod_str and key_str for the result.
class HotkeyDialog : public ATL::CDialogImpl<HotkeyDialog> {
public:
    enum { IDD = IDD_HOTKEY_DIALOG };

    std::wstring mod_str;  // pre-populate before DoModal, e.g. "CTRL+ALT"
    std::wstring key_str;  // pre-populate before DoModal, e.g. "F10"
    bool         accepted = false;

    BEGIN_MSG_MAP(HotkeyDialog)
        MESSAGE_HANDLER(WM_INITDIALOG,  OnInitDialog)
        COMMAND_ID_HANDLER(IDC_HOTKEY_CLEAR, OnClear)
        COMMAND_ID_HANDLER(IDOK,             OnOK)
        COMMAND_ID_HANDLER(IDCANCEL,         OnCancel)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClear(WORD, WORD, HWND, BOOL&);
    LRESULT OnOK(WORD, WORD, HWND, BOOL&);
    LRESULT OnCancel(WORD, WORD, HWND, BOOL&);

private:
    void Unhook();
    void UpdateDisplay();

    static std::wstring VkToString(UINT vk);
    static std::wstring ModsToString(bool ctrl, bool alt, bool shift, bool win);

    // WH_KEYBOARD_LL so we intercept keys even when none of our controls have focus
    static LRESULT CALLBACK LLKeyProc(int code, WPARAM wParam, LPARAM lParam);
    static HotkeyDialog*    s_active_;
    HHOOK                   hook_ = nullptr;

    bool has_key_  = false;
    UINT vk_       = 0;
    bool mod_ctrl_ = false, mod_alt_ = false, mod_shift_ = false, mod_win_ = false;
};
