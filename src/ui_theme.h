#pragma once

#include <windows.h>

namespace twosemi::ui {

inline constexpr int kLauncherWidth = 620;
inline constexpr int kLauncherHeight = 370;
inline constexpr int kChatWidth = 760;
inline constexpr int kChatHeight = 640;
inline constexpr int kNoteEditorWidth = 820;
inline constexpr int kNoteEditorHeight = 620;

inline constexpr COLORREF kBackground = RGB(10, 12, 14);
inline constexpr COLORREF kInputBackground = RGB(17, 21, 20);
inline constexpr COLORREF kText = RGB(214, 227, 216);
inline constexpr COLORREF kMutedText = RGB(111, 139, 120);
inline constexpr COLORREF kSelection = RGB(176, 255, 0);
inline constexpr COLORREF kAccent = RGB(176, 255, 0);
inline constexpr COLORREF kSelectionText = RGB(8, 12, 4);
inline constexpr COLORREF kSelectionMutedText = RGB(35, 52, 10);

HFONT UiFont(int size, bool bold = false);
void SetFont(HWND control, HFONT font);
void ApplyDarkControlTheme(HWND control);

} // namespace twosemi::ui
