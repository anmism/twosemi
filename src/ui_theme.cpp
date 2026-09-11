#include "ui_theme.h"

#include <uxtheme.h>

namespace twosemi::ui {
namespace {

int CALLBACK FontEnumerationProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM l_param) {
    *reinterpret_cast<bool*>(l_param) = true;
    return 0;
}

bool FontFaceAvailable(const wchar_t* face_name) {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
        return false;
    }
    LOGFONTW requested{};
    requested.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(requested.lfFaceName, face_name);
    bool available = false;
    EnumFontFamiliesExW(screen, &requested, FontEnumerationProc,
                        reinterpret_cast<LPARAM>(&available), 0);
    ReleaseDC(nullptr, screen);
    return available;
}

const wchar_t* UiFontFace() {
    static const wchar_t* face = FontFaceAvailable(L"Cascadia Mono")
                                     ? L"Cascadia Mono"
                                     : L"Consolas";
    return face;
}

} // namespace

HFONT UiFont(int size, bool bold) {
    HDC screen = GetDC(nullptr);
    const int dpi = screen != nullptr ? GetDeviceCaps(screen, LOGPIXELSY) : 96;
    if (screen != nullptr) {
        ReleaseDC(nullptr, screen);
    }
    return CreateFontW(
        -MulDiv(size, dpi, 72),
        0,
        0,
        0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE,
        FALSE,
        FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        UiFontFace());
}

void SetFont(HWND control, HFONT font) {
    if (control != nullptr) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

void ApplyDarkControlTheme(HWND control) {
    if (control != nullptr) {
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
}

} // namespace twosemi::ui
