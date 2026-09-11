#include "ui_controls.h"
#include "ui_theme.h"

#include <commctrl.h>

namespace twosemi::ui {
namespace {

HMENU ControlId(int id) {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

} // namespace

HWND CreateTextInput(HWND parent, int id, const std::wstring& value) {
    HWND control = CreateWindowExW(
        0, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
        0, 0, 100, 28, parent, ControlId(id), GetModuleHandleW(nullptr), nullptr);
    ApplyDarkControlTheme(control);
    return control;
}

HWND CreateMultilineInput(HWND parent, int id, const std::wstring& value) {
    HWND control = CreateWindowExW(
        0, L"EDIT", value.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
            WS_VSCROLL | WS_TABSTOP,
        0, 0, 100, 100, parent, ControlId(id), GetModuleHandleW(nullptr), nullptr);
    ApplyDarkControlTheme(control);
    return control;
}

HWND CreateChoiceInput(HWND parent, int id) {
    HWND control = CreateWindowExW(
        0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        0, 0, 100, 150, parent, ControlId(id), GetModuleHandleW(nullptr), nullptr);
    ApplyDarkControlTheme(control);
    return control;
}

HWND CreateDateTimeInput(HWND parent, int id, const SYSTEMTIME& value) {
    HWND control = CreateWindowExW(
        0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 220, 28, parent, ControlId(id), GetModuleHandleW(nullptr), nullptr);
    if (control != nullptr) {
        SendMessageW(control, DTM_SETFORMATW, 0,
                     reinterpret_cast<LPARAM>(L"yyyy-MM-dd HH:mm"));
        SendMessageW(control, DTM_SETSYSTEMTIME, GDT_VALID,
                     reinterpret_cast<LPARAM>(&value));
        ApplyDarkControlTheme(control);
    }
    return control;
}

bool ReadDateTimeInput(HWND control, SYSTEMTIME& value) {
    return control != nullptr &&
           SendMessageW(control, DTM_GETSYSTEMTIME, 0,
                        reinterpret_cast<LPARAM>(&value)) == GDT_VALID;
}

HWND CreateActionButton(HWND parent, int id, const wchar_t* label) {
    return CreateWindowW(
        L"BUTTON", label, WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
        0, 0, 68, 28, parent, ControlId(id), GetModuleHandleW(nullptr), nullptr);
}

} // namespace twosemi::ui
