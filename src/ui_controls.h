#pragma once

#include <string>
#include <windows.h>

namespace twosemi::ui {

HWND CreateTextInput(HWND parent, int id, const std::wstring& value);
HWND CreateMultilineInput(HWND parent, int id, const std::wstring& value);
HWND CreateChoiceInput(HWND parent, int id);
HWND CreateDateTimeInput(HWND parent, int id, const SYSTEMTIME& value);
bool ReadDateTimeInput(HWND control, SYSTEMTIME& value);
HWND CreateActionButton(HWND parent, int id, const wchar_t* label);

} // namespace twosemi::ui
