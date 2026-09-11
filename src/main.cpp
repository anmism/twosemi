#include <windows.h>
#include "resource.h"
#include "ui_theme.h"
#include "ui_controls.h"
#include "app_types.h"
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <winsqlite/winsqlite3.h>
#include <gdiplus.h>
#include <richedit.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace twosemi::ui;
using namespace twosemi::model;

constexpr wchar_t kHostClassName[] = L"TwoSemi.HostWindow";
constexpr wchar_t kLauncherClassName[] = L"TwoSemi.LauncherWindow";
constexpr wchar_t kNoteEditorClassName[] = L"TwoSemi.NoteEditorWindow";
constexpr wchar_t kQuickEditorClassName[] = L"TwoSemi.QuickEditorWindow";
constexpr wchar_t kFocusEditorClassName[] = L"TwoSemi.FocusEditorWindow";
constexpr wchar_t kScreenshotSelectorClassName[] = L"TwoSemi.ScreenshotSelectorWindow";
constexpr wchar_t kChatDialogClassName[] = L"TwoSemi.ChatDialogWindow";
constexpr wchar_t kModelManagerClassName[] = L"TwoSemi.ModelManagerWindow";
constexpr wchar_t kModelEditorClassName[] = L"TwoSemi.ModelEditorWindow";
constexpr wchar_t kMutexName[] = L"Local\\TwoSemi.SingleInstance";

constexpr UINT kTrayMessage = WM_APP + 10;
constexpr UINT kShowLauncherMessage = WM_APP + 11;
constexpr UINT kPasteNoteMessage = WM_APP + 12;
constexpr UINT kReplayActivationTimer = 1;
constexpr UINT kTrayOpenCommand = 1001;
constexpr UINT kTrayExitCommand = 1002;
constexpr UINT kChatResponseMessage = WM_APP + 40;
constexpr UINT kChatScreenshotCommand = 304;
constexpr UINT kChatUploadCommand = 305;
constexpr UINT kChatClearAttachmentCommand = 306;
constexpr UINT kChatAttachmentPreview = 307;
constexpr UINT kFocusTimer = 2;
constexpr UINT kReminderTimer = 3;
constexpr wchar_t kSelectAllOriginalProperty[] = L"TwoSemi.SelectAll.Original";

constexpr int kActivationIntervalMs = 650;

ULONG_PTR g_gdiplus_token = 0;
HICON g_app_icon_large = nullptr;
HICON g_app_icon_small = nullptr;

std::wstring WideFromUtf8(const char* value, int bytes = -1) {
    if (value == nullptr) {
        return {};
    }

    const int length = bytes < 0 ? -1 : bytes;
    const int required = MultiByteToWideChar(CP_UTF8, 0, value, length, nullptr, 0);
    if (required <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value, length, result.data(), required);
    if (bytes < 0 && !result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::wstring WideFromSqlite16(const void* value, int bytes) {
    if (value == nullptr) {
        return {};
    }

    const auto* text = static_cast<const wchar_t*>(value);
    if (bytes < 0) {
        return text;
    }
    if (bytes == 0) {
        return {};
    }
    return std::wstring(text, static_cast<size_t>(bytes) / sizeof(wchar_t));
}

std::string Utf8FromWide(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }

    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                             value.data(), static_cast<int>(value.size()),
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                        value.data(), static_cast<int>(value.size()),
                        result.data(), required, nullptr, nullptr);
    return result;
}

std::wstring Base64Encode(const BYTE* bytes, size_t size) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((size + 2) / 3) * 4);
    for (size_t index = 0; index < size; index += 3) {
        const unsigned int first = bytes[index];
        const unsigned int second = index + 1 < size ? bytes[index + 1] : 0;
        const unsigned int third = index + 2 < size ? bytes[index + 2] : 0;
        encoded.push_back(alphabet[(first >> 2) & 0x3F]);
        encoded.push_back(alphabet[((first << 4) | (second >> 4)) & 0x3F]);
        encoded.push_back(index + 1 < size ? alphabet[((second << 2) | (third >> 6)) & 0x3F] : '=');
        encoded.push_back(index + 2 < size ? alphabet[third & 0x3F] : '=');
    }
    return std::wstring(encoded.begin(), encoded.end());
}

std::optional<std::vector<BYTE>> Base64Decode(std::wstring_view encoded) {
    if (encoded.empty() || encoded.size() % 4 != 0) {
        return std::nullopt;
    }

    auto value_of = [](wchar_t character) -> int {
        if (character >= L'A' && character <= L'Z') return character - L'A';
        if (character >= L'a' && character <= L'z') return character - L'a' + 26;
        if (character >= L'0' && character <= L'9') return character - L'0' + 52;
        if (character == L'+') return 62;
        if (character == L'/') return 63;
        return -1;
    };

    std::vector<BYTE> decoded;
    decoded.reserve((encoded.size() / 4) * 3);
    for (size_t index = 0; index < encoded.size(); index += 4) {
        const bool third_padding = encoded[index + 2] == L'=';
        const bool fourth_padding = encoded[index + 3] == L'=';
        const int first = value_of(encoded[index]);
        const int second = value_of(encoded[index + 1]);
        const int third = third_padding ? 0 : value_of(encoded[index + 2]);
        const int fourth = fourth_padding ? 0 : value_of(encoded[index + 3]);
        if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
            (third_padding && !fourth_padding)) {
            return std::nullopt;
        }
        decoded.push_back(static_cast<BYTE>((first << 2) | (second >> 4)));
        if (!third_padding) {
            decoded.push_back(static_cast<BYTE>((second << 4) | (third >> 2)));
        }
        if (!fourth_padding) {
            decoded.push_back(static_cast<BYTE>((third << 6) | fourth));
        }
    }
    return decoded;
}

bool ProtectSecret(const std::wstring& value, std::wstring& protected_value) {
    const std::string plain = Utf8FromWide(value);
    DATA_BLOB input{static_cast<DWORD>(plain.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"TwoSemi secret note", nullptr, nullptr, nullptr, 0, &output)) {
        return false;
    }

    protected_value = Base64Encode(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return true;
}

std::wstring UnprotectSecret(const std::wstring& protected_value, bool& success) {
    success = false;
    const auto encoded = Base64Decode(protected_value);
    if (!encoded.has_value()) {
        return {};
    }

    DATA_BLOB input{static_cast<DWORD>(encoded->size()),
                    const_cast<BYTE*>(encoded->data())};
    DATA_BLOB output{};
    LPWSTR description = nullptr;
    if (!CryptUnprotectData(&input, &description, nullptr, nullptr, nullptr, 0, &output)) {
        return {};
    }
    if (description != nullptr) {
        LocalFree(description);
    }

    const std::wstring value = WideFromUtf8(reinterpret_cast<const char*>(output.pbData),
                                            static_cast<int>(output.cbData));
    LocalFree(output.pbData);
    success = true;
    return value;
}

std::wstring Lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }

    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::wstring> SplitLines(const std::wstring& value) {
    std::vector<std::wstring> lines;
    std::wstring line;
    std::wstringstream stream(value);
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    return lines;
}

std::wstring JoinLines(const std::vector<std::wstring>& lines) {
    std::wstring result;
    for (const std::wstring& line : lines) {
        if (!result.empty()) {
            result += L"\n";
        }
        result += line;
    }
    return result;
}

std::vector<std::wstring> NormalizeFocusDomains(const std::wstring& input) {
    std::wstring tokens = input;
    std::replace(tokens.begin(), tokens.end(), L',', L'\n');
    std::replace(tokens.begin(), tokens.end(), L';', L'\n');
    std::vector<std::wstring> normalized;
    for (std::wstring domain : SplitLines(tokens)) {
        const size_t scheme = domain.find(L"://");
        if (scheme != std::wstring::npos) {
            domain = domain.substr(scheme + 3);
        }
        const size_t path = domain.find_first_of(L"/\\");
        if (path != std::wstring::npos) {
            domain.resize(path);
        }
        const size_t port = domain.find(L':');
        if (port != std::wstring::npos) {
            domain.resize(port);
        }
        while (!domain.empty() && domain.front() == L'.') {
            domain.erase(domain.begin());
        }
        if (domain.rfind(L"*.", 0) == 0) {
            domain.erase(0, 2);
        }
        domain = Lowercase(Trim(domain));
        if (domain.rfind(L"www.", 0) == 0) {
            domain.erase(0, 4);
        }
        if (domain.empty() || domain == L"localhost" || domain == L"127.0.0.1" ||
            domain == L"::1" || domain.find(L'.') == std::wstring::npos) {
            continue;
        }
        const bool valid = std::all_of(domain.begin(), domain.end(), [](wchar_t character) {
            return std::iswalnum(character) || character == L'.' || character == L'-';
        });
        if (!valid) {
            continue;
        }
        normalized.push_back(domain);
    }
    std::sort(normalized.begin(), normalized.end());
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

std::wstring MakeNoteTitle(const std::wstring& body) {
    std::wstring title = Trim(body);
    const size_t line_break = title.find_first_of(L"\r\n");
    if (line_break != std::wstring::npos) {
        title.resize(line_break);
        title = Trim(title);
    }
    if (title.empty()) {
        title = L"Untitled note";
    }
    if (title.size() > 72) {
        title.resize(69);
        title += L"...";
    }
    return title;
}

std::wstring MakeChatTitle(const std::wstring& body) {
    std::wstring title = body;
    std::replace(title.begin(), title.end(), L'\r', L' ');
    std::replace(title.begin(), title.end(), L'\n', L' ');
    title = Trim(title);
    if (title.empty()) {
        return L"New chat";
    }
    if (title.size() > 56) {
        title.resize(53);
        title += L"...";
    }
    return title;
}

std::wstring NotePreview(const std::wstring& body) {
    std::wstring preview = body;
    std::replace(preview.begin(), preview.end(), L'\r', L' ');
    std::replace(preview.begin(), preview.end(), L'\n', L' ');
    preview = Trim(preview);
    if (preview.size() > 90) {
        preview.resize(87);
        preview += L"...";
    }
    return preview;
}

std::wstring FormatFocusRemaining(ULONGLONG seconds) {
    const ULONGLONG minutes = seconds / 60;
    const ULONGLONG remainder = seconds % 60;
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%02llu:%02llu", minutes, remainder);
    return buffer;
}

std::wstring NewId() {
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t buffer[64]{};
    StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer)));
    return buffer;
}

std::wstring LocalDatabasePath() {
    wchar_t localAppData[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, localAppData))) {
        std::wstring directory = std::wstring(localAppData) + L"\\TwoSemi";
        CreateDirectoryW(directory.c_str(), nullptr);
        return directory + L"\\twosemi.db";
    }

    return L"twosemi.db";
}

std::wstring ExecutableDirectory() {
    wchar_t executable[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, std::size(executable));
    if (length == 0 || length >= std::size(executable)) {
        return {};
    }
    const std::wstring path(executable, length);
    const size_t separator = path.find_last_of(L"\\/");
    return separator == std::wstring::npos ? L"." : path.substr(0, separator);
}

std::wstring AppIconPath() {
    const std::wstring executable_directory = ExecutableDirectory();
    const std::vector<std::wstring> candidates = {
        executable_directory + L"\\assets\\icon.png",
        executable_directory + L"\\..\\..\\assets\\icon.png",
        L"assets\\icon.png",
    };
    for (const std::wstring& candidate : candidates) {
        if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
            return candidate;
        }
    }
    return {};
}

HICON LoadPngIcon(int size) {
    const std::wstring path = AppIconPath();
    if (path.empty()) {
        return nullptr;
    }
    Gdiplus::Bitmap source(path.c_str(), FALSE);
    if (source.GetLastStatus() != Gdiplus::Ok || source.GetWidth() == 0 || source.GetHeight() == 0) {
        return nullptr;
    }
    Gdiplus::Bitmap canvas(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    graphics.Clear(Gdiplus::Color::MakeARGB(0, 0, 0, 0));
    graphics.DrawImage(&source, Gdiplus::Rect(0, 0, size, size), 0, 0,
                       static_cast<int>(source.GetWidth()), static_cast<int>(source.GetHeight()),
                       Gdiplus::UnitPixel);
    HICON icon = nullptr;
    canvas.GetHICON(&icon);
    return icon;
}

HICON LoadEmbeddedIcon(int size) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
                                         MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                         size, size, LR_DEFAULTCOLOR));
}

void SetAppWindowClassIcon(WNDCLASSW& window_class) {
    window_class.hIcon = g_app_icon_large;
}

void SetAppWindowIcons(HWND window) {
    if (window == nullptr) {
        return;
    }
    if (g_app_icon_large != nullptr) {
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(g_app_icon_large));
    }
    if (g_app_icon_small != nullptr) {
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(g_app_icon_small));
    }
}

void InitializeAppIcons() {
    if (g_gdiplus_token == 0) {
        Gdiplus::GdiplusStartupInput startup_input;
        if (Gdiplus::GdiplusStartup(&g_gdiplus_token, &startup_input, nullptr) != Gdiplus::Ok) {
            g_gdiplus_token = 0;
            return;
        }
    }
    g_app_icon_large = LoadEmbeddedIcon(32);
    g_app_icon_small = LoadEmbeddedIcon(16);
    if (g_app_icon_large == nullptr) {
        g_app_icon_large = LoadPngIcon(32);
    }
    if (g_app_icon_small == nullptr) {
        g_app_icon_small = LoadPngIcon(16);
    }
}

void ShutdownAppIcons() {
    if (g_app_icon_large != nullptr) {
        DestroyIcon(g_app_icon_large);
        g_app_icon_large = nullptr;
    }
    if (g_app_icon_small != nullptr) {
        DestroyIcon(g_app_icon_small);
        g_app_icon_small = nullptr;
    }
    if (g_gdiplus_token != 0) {
        Gdiplus::GdiplusShutdown(g_gdiplus_token);
        g_gdiplus_token = 0;
    }
}

std::wstring HostsFilePath() {
    wchar_t system_directory[MAX_PATH]{};
    if (GetSystemDirectoryW(system_directory, std::size(system_directory)) == 0) {
        return {};
    }
    return std::wstring(system_directory) + L"\\drivers\\etc\\hosts";
}

std::wstring FocusBlockRequestPath() {
    const std::wstring database_path = LocalDatabasePath();
    const size_t separator = database_path.find_last_of(L"\\/");
    return (separator == std::wstring::npos ? L"." : database_path.substr(0, separator)) +
           L"\\focus-block-request.txt";
}

bool WriteFocusHosts(const std::vector<std::wstring>& domains, bool apply) {
    const std::wstring path = HostsFilePath();
    if (path.empty()) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return !apply;
    }
    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();

    const std::string begin_marker = "# TwoSemi focus block begin";
    const std::string end_marker = "# TwoSemi focus block end";
    const size_t block_start = contents.find(begin_marker);
    if (block_start != std::string::npos) {
        size_t line_start = contents.rfind('\n', block_start);
        line_start = line_start == std::string::npos ? 0 : line_start + 1;
        const size_t marker_end = contents.find(end_marker, block_start);
        if (marker_end == std::string::npos) {
            return false;
        }
        size_t block_end = marker_end + end_marker.size();
        while (block_end < contents.size() && (contents[block_end] == '\r' || contents[block_end] == '\n')) {
            ++block_end;
        }
        contents.erase(line_start, block_end - line_start);
    }

    if (apply && !domains.empty()) {
        if (!contents.empty() && contents.back() != '\n') {
            contents += "\r\n";
        }
        contents += begin_marker + "\r\n";
        for (const std::wstring& domain : domains) {
            const std::string ascii_domain = Utf8FromWide(domain);
            contents += "0.0.0.0 " + ascii_domain + "\r\n";
            if (domain.find(L'.') == domain.rfind(L'.')) {
                contents += "0.0.0.0 www." + ascii_domain + "\r\n";
            }
        }
        contents += end_marker + "\r\n";
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return output.good();
}

bool ReadFocusBlockRequest(const std::wstring& path, std::vector<std::wstring>& domains) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    domains = NormalizeFocusDomains(WideFromUtf8(contents.data(), static_cast<int>(contents.size())));
    return true;
}

bool RunElevatedFocusCommand(bool apply, const std::vector<std::wstring>& domains) {
    if (IsUserAnAdmin() != FALSE) {
        return WriteFocusHosts(domains, apply);
    }

    const std::wstring request_path = FocusBlockRequestPath();
    if (apply) {
        std::ofstream request(request_path, std::ios::binary | std::ios::trunc);
        if (!request) {
            return false;
        }
        const std::string content = Utf8FromWide(JoinLines(domains));
        request.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!request.good()) {
            return false;
        }
    }

    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, std::size(executable)) == 0) {
        if (apply) DeleteFileW(request_path.c_str());
        return false;
    }
    const std::wstring parameters = apply
                                        ? L"--twosemi-hosts-apply \"" + request_path + L"\""
                                        : L"--twosemi-hosts-clear";
    SHELLEXECUTEINFOW execute{};
    execute.cbSize = sizeof(execute);
    execute.fMask = SEE_MASK_NOCLOSEPROCESS;
    execute.lpVerb = L"runas";
    execute.lpFile = executable;
    execute.lpParameters = parameters.c_str();
    execute.nShow = SW_HIDE;
    const bool launched = ShellExecuteExW(&execute) != FALSE;
    if (!launched) {
        if (apply) DeleteFileW(request_path.c_str());
        return false;
    }
    WaitForSingleObject(execute.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(execute.hProcess, &exit_code);
    CloseHandle(execute.hProcess);
    if (apply) {
        DeleteFileW(request_path.c_str());
    }
    return exit_code == 0;
}

int HandleFocusHostCommand(const std::wstring& command_line) {
    if (command_line == L"--twosemi-hosts-clear") {
        return WriteFocusHosts({}, false) ? 0 : 1;
    }
    const std::wstring prefix = L"--twosemi-hosts-apply ";
    if (command_line.rfind(prefix, 0) != 0) {
        return -1;
    }
    std::wstring request_path = Trim(command_line.substr(prefix.size()));
    if (request_path.size() >= 2 && request_path.front() == L'"' && request_path.back() == L'"') {
        request_path = request_path.substr(1, request_path.size() - 2);
    }
    std::vector<std::wstring> domains;
    if (!ReadFocusBlockRequest(request_path, domains)) {
        return 1;
    }
    return WriteFocusHosts(domains, true) ? 0 : 1;
}

std::wstring NowUtcIso() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
               now.wYear, now.wMonth, now.wDay,
               now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    return buffer;
}

std::wstring TodayLocalDate() {
    SYSTEMTIME local{};
    GetLocalTime(&local);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", local.wYear, local.wMonth, local.wDay);
    return buffer;
}

bool ParseLocalDate(const std::wstring& value, SYSTEMTIME& date) {
    unsigned short year = 0;
    unsigned short month = 0;
    unsigned short day = 0;
    if (swscanf_s(value.c_str(), L"%hu-%hu-%hu", &year, &month, &day) != 3) {
        return false;
    }
    date = {};
    date.wYear = year;
    date.wMonth = month;
    date.wDay = day;
    FILETIME file_time{};
    return SystemTimeToFileTime(&date, &file_time) != FALSE;
}

std::wstring ShiftLocalDate(const std::wstring& value, int days) {
    SYSTEMTIME date{};
    if (!ParseLocalDate(value, date)) {
        return value;
    }

    FILETIME file_time{};
    SystemTimeToFileTime(&date, &file_time);
    ULARGE_INTEGER ticks{file_time.dwLowDateTime, file_time.dwHighDateTime};
    constexpr ULONGLONG kTicksPerDay = 24ULL * 60ULL * 60ULL * 10'000'000ULL;
    if (days >= 0) {
        ticks.QuadPart += static_cast<ULONGLONG>(days) * kTicksPerDay;
    } else {
        ticks.QuadPart -= static_cast<ULONGLONG>(-days) * kTicksPerDay;
    }
    file_time.dwLowDateTime = ticks.LowPart;
    file_time.dwHighDateTime = ticks.HighPart;

    SYSTEMTIME shifted{};
    FileTimeToSystemTime(&file_time, &shifted);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u", shifted.wYear, shifted.wMonth, shifted.wDay);
    return buffer;
}

int LocalDateWeekday(const std::wstring& value) {
    SYSTEMTIME date{};
    if (!ParseLocalDate(value, date)) {
        return -1;
    }
    FILETIME file_time{};
    SystemTimeToFileTime(&date, &file_time);
    SYSTEMTIME utc{};
    FileTimeToSystemTime(&file_time, &utc);
    return utc.wDayOfWeek;
}

bool IsScheduledStreakDate(const Streak& streak, const std::wstring& date) {
    if (streak.frequency != 2) {
        return true;
    }
    const int weekday = LocalDateWeekday(date);
    return weekday >= 1 && weekday <= 5;
}

std::wstring PreviousScheduledStreakDate(const Streak& streak, std::wstring date) {
    do {
        date = ShiftLocalDate(date, -1);
    } while (!IsScheduledStreakDate(streak, date));
    return date;
}

bool HasStreakCompletion(const Streak& streak, const std::wstring& date) {
    return std::find(streak.completed_dates.begin(), streak.completed_dates.end(), date) !=
           streak.completed_dates.end();
}

int CurrentStreak(const Streak& streak) {
    const std::wstring today = TodayLocalDate();
    std::wstring date = today;
    if (!IsScheduledStreakDate(streak, date) || !HasStreakCompletion(streak, date)) {
        date = PreviousScheduledStreakDate(streak, date);
    }
    if (!HasStreakCompletion(streak, date)) {
        return 0;
    }

    int count = 0;
    while (HasStreakCompletion(streak, date)) {
        ++count;
        date = PreviousScheduledStreakDate(streak, date);
    }
    return count;
}

int BestStreak(const Streak& streak) {
    int best = 0;
    for (const std::wstring& completion : streak.completed_dates) {
        if (!IsScheduledStreakDate(streak, completion)) {
            continue;
        }
        int count = 1;
        std::wstring date = PreviousScheduledStreakDate(streak, completion);
        while (HasStreakCompletion(streak, date)) {
            ++count;
            date = PreviousScheduledStreakDate(streak, date);
        }
        best = std::max(best, count);
    }
    return best;
}

std::wstring StreakFrequencyLabel(const Streak& streak) {
    return streak.frequency == 2 ? L"weekdays" : L"daily";
}

std::wstring StreakSummary(const Streak& streak) {
    const std::wstring today = TodayLocalDate();
    const int current = CurrentStreak(streak);
    const int best = BestStreak(streak);
    const std::wstring today_state = !IsScheduledStreakDate(streak, today)
                                         ? L"rest day"
                                         : HasStreakCompletion(streak, today) ? L"done today" : L"due today";
    return today_state + L" | " + std::to_wstring(current) + L" day streak | best " +
           std::to_wstring(best) + L" | " + StreakFrequencyLabel(streak);
}

std::wstring UtcAfterMinutes(unsigned int minutes) {
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER ticks{file_time.dwLowDateTime, file_time.dwHighDateTime};
    ticks.QuadPart += static_cast<ULONGLONG>(minutes) * 60ULL * 10'000'000ULL;
    file_time.dwLowDateTime = ticks.LowPart;
    file_time.dwHighDateTime = ticks.HighPart;
    SYSTEMTIME utc{};
    FileTimeToSystemTime(&file_time, &utc);
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
               utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
    return buffer;
}

std::wstring FormatLocalSystemTime(const SYSTEMTIME& time) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u",
               time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
    return buffer;
}

std::wstring DefaultReminderTime() {
    FILETIME file_time{};
    GetSystemTimeAsFileTime(&file_time);
    ULARGE_INTEGER ticks{file_time.dwLowDateTime, file_time.dwHighDateTime};
    ticks.QuadPart += 60ULL * 60ULL * 10'000'000ULL;
    file_time.dwLowDateTime = ticks.LowPart;
    file_time.dwHighDateTime = ticks.HighPart;
    SYSTEMTIME utc{};
    SYSTEMTIME local{};
    FileTimeToSystemTime(&file_time, &utc);
    SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local);
    return FormatLocalSystemTime(local);
}

bool ParseLocalReminderTime(const std::wstring& value, std::wstring& utc_value) {
    SYSTEMTIME local{};
    unsigned short year = 0;
    unsigned short month = 0;
    unsigned short day = 0;
    unsigned short hour = 0;
    unsigned short minute = 0;
    if (swscanf_s(value.c_str(), L"%hu-%hu-%hu %hu:%hu",
                  &year, &month, &day, &hour, &minute) != 5) {
        return false;
    }
    local.wYear = year;
    local.wMonth = month;
    local.wDay = day;
    local.wHour = hour;
    local.wMinute = minute;
    SYSTEMTIME utc{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &local, &utc)) {
        return false;
    }

    wchar_t buffer[64]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
               utc.wYear, utc.wMonth, utc.wDay,
               utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds);
    utc_value = buffer;
    return true;
}

std::wstring FormatReminderTime(const std::wstring& utc_value) {
    SYSTEMTIME utc{};
    unsigned short year = 0;
    unsigned short month = 0;
    unsigned short day = 0;
    unsigned short hour = 0;
    unsigned short minute = 0;
    unsigned short second = 0;
    if (swscanf_s(utc_value.c_str(), L"%hu-%hu-%huT%hu:%hu:%hu",
                  &year, &month, &day, &hour, &minute, &second) != 6) {
        return utc_value;
    }
    utc.wYear = year;
    utc.wMonth = month;
    utc.wDay = day;
    utc.wHour = hour;
    utc.wMinute = minute;
    utc.wSecond = second;
    SYSTEMTIME local{};
    if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
        return utc_value;
    }
    return FormatLocalSystemTime(local);
}

#include "database.inc"

std::wstring WindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring result(static_cast<size_t>(length) + 1, L'\0');
    if (length > 0) {
        GetWindowTextW(control, result.data(), length + 1);
    }
    result.resize(static_cast<size_t>(length));
    return result;
}

void CenterOver(HWND window, HWND owner) {
    RECT window_rect{};
    GetWindowRect(window, &window_rect);
    RECT owner_rect{};
    if (owner != nullptr && GetWindowRect(owner, &owner_rect)) {
        const int width = window_rect.right - window_rect.left;
        const int height = window_rect.bottom - window_rect.top;
        const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
        const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
        SetWindowPos(window, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

void ActivateAndFocus(HWND window, HWND focus) {
    if (window == nullptr || !IsWindow(window)) {
        return;
    }

    const DWORD current_thread = GetCurrentThreadId();
    const HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread = foreground == nullptr
                                        ? current_thread
                                        : GetWindowThreadProcessId(foreground, nullptr);
    const bool attached = foreground_thread != current_thread &&
                          AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE;

    ShowWindow(window, SW_SHOW);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    if (focus != nullptr && IsWindow(focus)) {
        SetFocus(focus);
    } else {
        SetFocus(window);
    }

    if (attached) {
        AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
}

void RestoreLauncherFocus(HWND owner) {
    HWND focus = GetDlgItem(owner, 201);
    if (focus == nullptr) {
        focus = GetDlgItem(owner, 501);
    }
    ActivateAndFocus(owner, focus);
}

#include "modal_editors.inc"

#include "chat.inc"

#include "model_manager.inc"
#include "launcher_window_decl.inc"
#include "app_controller.inc"
#include "launcher_window_impl.inc"
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    const int host_command_result = HandleFocusHostCommand(Trim(command_line == nullptr ? L"" : command_line));
    if (host_command_result >= 0) {
        return host_command_result;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex != nullptr) {
            CloseHandle(mutex);
        }
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    InitializeAppIcons();
    int result = 1;
    {
        TwoSemiApp app;
        const bool initialized = app.Initialize();
        if (!initialized) {
            MessageBoxW(nullptr, L"TwoSemi could not initialize its local SQLite database.",
                        L"TwoSemi", MB_OK | MB_ICONERROR);
        } else {
            result = app.Run();
        }
    }
    ShutdownAppIcons();
    CoUninitialize();
    CloseHandle(mutex);
    return result;
}
