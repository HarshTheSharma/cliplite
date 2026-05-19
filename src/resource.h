#pragma once

// Icons
#define IDI_APPICON                 101

// Sounds
#define IDR_CLIP_SOUND              102

// Menus
#define IDR_TRAYMENU                201

// Dialogs
#define IDD_HOTKEY_DIALOG           301
#define IDC_HOTKEY_DISPLAY          1001
#define IDC_HOTKEY_CLEAR            1002

// Custom window messages
#define WM_TRAYICON                 (WM_APP + 1)

// Tray menu: duration items (sequential, index 0-9 → 30..300 s in 30 s steps)
#define ID_TRAY_DURATION_FIRST      4001
#define ID_TRAY_DURATION_LAST       4010

// Other tray menu items
#define ID_TRAY_SAVELOCATION        4020
#define ID_TRAY_ENABLEMIC           4021
#define ID_TRAY_CHANGEHOTKEY        4022
#define ID_TRAY_OPENSAVEFOLDER      4023
#define ID_TRAY_OPENCONFIGFILE      4024
#define ID_TRAY_STARTWITHWINDOWS    4026
#define ID_TRAY_QUIT                4025
