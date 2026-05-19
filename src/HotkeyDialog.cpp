#include "HotkeyDialog.h"
#include <cstdlib>

HotkeyDialog* HotkeyDialog::s_active_ = nullptr;

std::wstring HotkeyDialog::VkToString(UINT vk) {
    if (vk >= VK_F1  && vk <= VK_F24)  return L"F" + std::to_wstring(vk - VK_F1 + 1);
    if (vk >= '0'    && vk <= '9')      return std::wstring(1, (wchar_t)vk);
    if (vk >= 'A'    && vk <= 'Z')      return std::wstring(1, (wchar_t)vk);
    switch (vk) {
    case VK_SPACE:      return L"Space";
    case VK_HOME:       return L"Home";
    case VK_END:        return L"End";
    case VK_PRIOR:      return L"PageUp";
    case VK_NEXT:       return L"PageDown";
    case VK_INSERT:     return L"Insert";
    case VK_DELETE:     return L"Delete";
    case VK_LEFT:       return L"Left";
    case VK_RIGHT:      return L"Right";
    case VK_UP:         return L"Up";
    case VK_DOWN:       return L"Down";
    case VK_BACK:       return L"Backspace";
    case VK_SNAPSHOT:   return L"PrintScreen";
    case VK_SCROLL:     return L"ScrollLock";
    case VK_PAUSE:      return L"Pause";
    case VK_OEM_MINUS:  return L"-";
    case VK_OEM_PLUS:   return L"=";
    case VK_OEM_COMMA:  return L",";
    case VK_OEM_PERIOD: return L".";
    case VK_OEM_1:      return L";";
    case VK_OEM_2:      return L"/";
    case VK_OEM_3:      return L"`";
    case VK_OEM_4:      return L"[";
    case VK_OEM_5:      return L"\\";
    case VK_OEM_6:      return L"]";
    case VK_OEM_7:      return L"'";
    case VK_TAB:        return L"Tab";
    case VK_RETURN:     return L"Enter";
    case VK_MULTIPLY:   return L"Num*";
    case VK_ADD:        return L"Num+";
    case VK_SUBTRACT:   return L"Num-";
    case VK_DECIMAL:    return L"Num.";
    case VK_DIVIDE:     return L"Num/";
    default:
        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9)
            return L"Num" + std::to_wstring(vk - VK_NUMPAD0);
        return L"?";
    }
}

std::wstring HotkeyDialog::ModsToString(bool ctrl, bool alt, bool shift, bool win) {
    std::wstring s;
    if (ctrl)  s += L"CTRL+";
    if (alt)   s += L"ALT+";
    if (shift) s += L"SHIFT+";
    if (win)   s += L"WIN+";
    return s;
}

void HotkeyDialog::UpdateDisplay() {
    std::wstring text;
    if (has_key_)
        text = ModsToString(mod_ctrl_, mod_alt_, mod_shift_, mod_win_) + VkToString(vk_);
    SetDlgItemText(IDC_HOTKEY_DISPLAY, text.c_str());
}

void HotkeyDialog::Unhook() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    s_active_ = nullptr;
}

LRESULT CALLBACK HotkeyDialog::LLKeyProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION && s_active_ &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
    {
        const auto* ks = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const UINT  vk = ks->vkCode;

        const bool is_mod =
            vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT   ||
            vk == VK_MENU    || vk == VK_LMENU    || vk == VK_RMENU    ||
            vk == VK_LWIN    || vk == VK_RWIN;

        if (!is_mod) {
            const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool alt   = (ks->flags & LLKHF_ALTDOWN) != 0;
            const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
            const bool win   = ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) != 0;
            const bool fkey  = (vk >= VK_F1 && vk <= VK_F24);

            if (ctrl || alt || shift || win || fkey) {
                s_active_->vk_        = vk;
                s_active_->has_key_   = true;
                s_active_->mod_ctrl_  = ctrl;
                s_active_->mod_alt_   = alt;
                s_active_->mod_shift_ = shift;
                s_active_->mod_win_   = win;
                s_active_->UpdateDisplay();
                return 1; // swallow the key so it doesn't trigger any registered hotkeys
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT HotkeyDialog::OnInitDialog(UINT, WPARAM, LPARAM, BOOL& bHandled) {
    std::wstring cur = mod_str;
    if (!key_str.empty()) {
        if (!cur.empty()) cur += L"+";
        cur += key_str;
    }
    SetDlgItemText(IDC_HOTKEY_DISPLAY, cur.c_str());

    s_active_ = this;
    hook_ = SetWindowsHookEx(WH_KEYBOARD_LL, LLKeyProc, GetModuleHandle(nullptr), 0);

    bHandled = TRUE;
    return 1; // let the dialog manager set focus rather than taking it ourselves
}

LRESULT HotkeyDialog::OnClear(WORD, WORD, HWND, BOOL& bHandled) {
    has_key_ = false;
    vk_ = 0;
    mod_ctrl_ = mod_alt_ = mod_shift_ = mod_win_ = false;
    UpdateDisplay();
    bHandled = TRUE;
    return 0;
}

LRESULT HotkeyDialog::OnOK(WORD, WORD, HWND, BOOL& bHandled) {
    Unhook();
    if (has_key_) {
        mod_str = ModsToString(mod_ctrl_, mod_alt_, mod_shift_, mod_win_);
        if (!mod_str.empty() && mod_str.back() == L'+')
            mod_str.pop_back();
        key_str  = VkToString(vk_);
        accepted = true;
    }
    EndDialog(IDOK);
    bHandled = TRUE;
    return 0;
}

LRESULT HotkeyDialog::OnCancel(WORD, WORD, HWND, BOOL& bHandled) {
    Unhook();
    EndDialog(IDCANCEL);
    bHandled = TRUE;
    return 0;
}
