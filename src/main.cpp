#include <windows.h>
#include "resource.h"
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
constexpr int kLauncherWidth = 620;
constexpr int kLauncherHeight = 370;
constexpr int kChatWidth = 760;
constexpr int kChatHeight = 640;
constexpr int kNoteEditorWidth = 820;
constexpr int kNoteEditorHeight = 620;

ULONG_PTR g_gdiplus_token = 0;
HICON g_app_icon_large = nullptr;
HICON g_app_icon_small = nullptr;

constexpr COLORREF kBackground = RGB(10, 12, 14);
constexpr COLORREF kInputBackground = RGB(17, 21, 20);
constexpr COLORREF kText = RGB(214, 227, 216);
constexpr COLORREF kMutedText = RGB(111, 139, 120);
constexpr COLORREF kSelection = RGB(29, 64, 43);
constexpr COLORREF kAccent = RGB(90, 220, 125);

struct Note {
    enum class Kind {
        Text = 0,
        Secret = 1,
    };

    std::wstring id;
    std::wstring title;
    std::wstring body;
    Kind kind = Kind::Text;
    bool secret_available = true;
};

struct Reminder {
    std::wstring id;
    std::wstring text;
    std::wstring due_at_utc;
    std::wstring created_at_utc;
    std::wstring completed_at_utc;
    std::wstring snoozed_until_utc;
};

struct TodoItem {
    std::wstring id;
    std::wstring group_id;
    std::wstring text;
    bool completed = false;
    std::wstring created_at_utc;
    std::wstring updated_at_utc;
};

struct TodoGroup {
    std::wstring id;
    std::wstring title;
    std::wstring created_at_utc;
    std::wstring updated_at_utc;
    std::vector<TodoItem> items;
};

struct Streak {
    std::wstring id;
    std::wstring title;
    int frequency = 1;
    std::wstring created_at_utc;
    std::wstring updated_at_utc;
    std::vector<std::wstring> completed_dates;
};

struct ChatMessage {
    std::wstring id;
    std::wstring thread_id;
    std::wstring role;
    std::wstring body;
    std::wstring created_at_utc;
    bool is_error = false;
    std::wstring attachment_path;
};

struct ChatThread {
    std::wstring id;
    std::wstring title;
    std::wstring created_at_utc;
    std::wstring updated_at_utc;
    std::vector<ChatMessage> messages;
};

struct FocusSettings {
    int minutes = 25;
    std::vector<std::wstring> domains;
};

struct AiModelProfile {
    std::wstring id;
    std::wstring provider = L"Gemini";
    std::wstring name;
    std::wstring model_id;
    std::wstring encrypted_api_key;
    bool enabled = true;
    bool is_default = true;
};

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

class Database {
public:
    explicit Database(const std::wstring& path) {
        if (sqlite3_open16(path.c_str(), &database_) != SQLITE_OK) {
            last_error_ = database_ != nullptr ? WideFromSqlite16(sqlite3_errmsg16(database_), -1)
                                                : L"Could not open the SQLite database.";
            if (database_ != nullptr) {
                sqlite3_close(database_);
                database_ = nullptr;
            }
            return;
        }

        if (!Execute("PRAGMA journal_mode = WAL;") ||
            !Execute("PRAGMA foreign_keys = ON;") ||
            !Execute("CREATE TABLE IF NOT EXISTS notes ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Title TEXT NOT NULL,"
                     "Body TEXT NOT NULL,"
                     "Kind INTEGER NOT NULL DEFAULT 0,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "UpdatedAtUtc TEXT NOT NULL,"
                     "SourceApplication TEXT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS reminders ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Text TEXT NOT NULL,"
                     "DueAtUtc TEXT NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "CompletedAtUtc TEXT NULL,"
                     "SnoozedUntilUtc TEXT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS todo_groups ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Title TEXT NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "UpdatedAtUtc TEXT NOT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS todo_items ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "GroupId TEXT NOT NULL,"
                     "SortOrder INTEGER NOT NULL,"
                     "Text TEXT NOT NULL,"
                     "IsCompleted INTEGER NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "UpdatedAtUtc TEXT NOT NULL,"
                     "FOREIGN KEY (GroupId) REFERENCES todo_groups(Id) ON DELETE CASCADE"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS streaks ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Title TEXT NOT NULL,"
                     "Frequency INTEGER NOT NULL DEFAULT 1,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "UpdatedAtUtc TEXT NOT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS streak_completions ("
                     "StreakId TEXT NOT NULL,"
                     "CompletedDate TEXT NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "PRIMARY KEY (StreakId, CompletedDate),"
                     "FOREIGN KEY (StreakId) REFERENCES streaks(Id) ON DELETE CASCADE"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS chat_threads ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Title TEXT NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "UpdatedAtUtc TEXT NOT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS chat_messages ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "ThreadId TEXT NOT NULL,"
                     "Sequence INTEGER NOT NULL,"
                     "Role TEXT NOT NULL,"
                     "Body TEXT NOT NULL,"
                     "CreatedAtUtc TEXT NOT NULL,"
                     "IsError INTEGER NOT NULL DEFAULT 0,"
                     "AttachmentPath TEXT NULL,"
                     "FOREIGN KEY (ThreadId) REFERENCES chat_threads(Id) ON DELETE CASCADE"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS ai_models ("
                     "Id TEXT PRIMARY KEY NOT NULL,"
                     "Provider TEXT NOT NULL,"
                     "Name TEXT NOT NULL,"
                     "ModelId TEXT NOT NULL,"
                     "EncryptedApiKey TEXT NOT NULL,"
                     "Enabled INTEGER NOT NULL,"
                     "IsDefault INTEGER NOT NULL"
                     ");") ||
            !Execute("CREATE TABLE IF NOT EXISTS focus_settings ("
                     "Id INTEGER PRIMARY KEY CHECK (Id = 1),"
                     "Minutes INTEGER NOT NULL DEFAULT 25,"
                     "Domains TEXT NOT NULL DEFAULT ''"
                     ");") ||
            !Execute("CREATE INDEX IF NOT EXISTS ix_reminders_due_at ON reminders (DueAtUtc);") ||
            !Execute("CREATE INDEX IF NOT EXISTS ix_todo_items_group_order ON todo_items (GroupId, SortOrder);") ||
            !Execute("CREATE INDEX IF NOT EXISTS ix_streak_completions_date ON streak_completions (CompletedDate);") ||
            !Execute("CREATE INDEX IF NOT EXISTS ix_chat_messages_thread_order ON chat_messages (ThreadId, Sequence);") ) {
            sqlite3_close(database_);
            database_ = nullptr;
        } else {
            legacy_schema_ = HasColumn(L"created_at") && HasColumn(L"updated_at");
            has_kind_ = HasColumn(L"kind");
            if (legacy_schema_ && !has_kind_) {
                if (!Execute("ALTER TABLE notes ADD COLUMN kind INTEGER NOT NULL DEFAULT 0;")) {
                    sqlite3_close(database_);
                    database_ = nullptr;
                } else {
                    has_kind_ = true;
                }
            }
            if (database_ != nullptr) {
                if (!HasTableColumn("chat_messages", L"AttachmentPath") &&
                    !Execute("ALTER TABLE chat_messages ADD COLUMN AttachmentPath TEXT NULL;")) {
                    sqlite3_close(database_);
                    database_ = nullptr;
                }
            }
            if (database_ != nullptr) {
                Execute(legacy_schema_
                            ? "CREATE INDEX IF NOT EXISTS ix_notes_updated_at_legacy ON notes (updated_at DESC);"
                            : "CREATE INDEX IF NOT EXISTS ix_notes_updated_at ON notes (UpdatedAtUtc DESC);");
            }
        }
    }

    ~Database() {
        if (database_ != nullptr) {
            sqlite3_close(database_);
        }
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    bool IsOpen() const { return database_ != nullptr; }
    const std::wstring& LastError() const { return last_error_; }

    std::vector<Note> LoadNotes() {
        std::vector<Note> notes;
        if (!IsOpen()) {
            return notes;
        }

        sqlite3_stmt* statement = nullptr;
        const char* sql = legacy_schema_
                              ? "SELECT id, title, body, kind FROM notes ORDER BY updated_at DESC;"
                              : "SELECT Id, Title, Body, Kind FROM notes ORDER BY UpdatedAtUtc DESC;";
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return notes;
        }

        while (sqlite3_step(statement) == SQLITE_ROW) {
            Note note{
                WideFromSqlite16(sqlite3_column_text16(statement, 0), sqlite3_column_bytes16(statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(statement, 1), sqlite3_column_bytes16(statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(statement, 2), sqlite3_column_bytes16(statement, 2)),
                sqlite3_column_int(statement, 3) == 1 ? Note::Kind::Secret : Note::Kind::Text,
                true,
            };
            if (note.kind == Note::Kind::Secret) {
                bool decrypted = false;
                const std::wstring protected_body = note.body;
                note.body = UnprotectSecret(protected_body, decrypted);
                note.secret_available = decrypted;
            }
            notes.push_back(std::move(note));
        }

        sqlite3_finalize(statement);
        return notes;
    }

    bool SaveNote(const Note& note) {
        if (!IsOpen()) {
            return false;
        }

        const char* sql = legacy_schema_
                              ? "INSERT INTO notes (id, title, body, kind, created_at, updated_at) "
                                "VALUES (?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
                                "ON CONFLICT(id) DO UPDATE SET title = excluded.title, body = excluded.body, "
                                "kind = excluded.kind, updated_at = strftime('%s','now');"
                              : "INSERT INTO notes (Id, Title, Body, Kind, CreatedAtUtc, UpdatedAtUtc, SourceApplication) "
                                "VALUES (?, ?, ?, ?, ?, ?, NULL) "
                                "ON CONFLICT(Id) DO UPDATE SET Title = excluded.Title, Body = excluded.Body, "
                                "UpdatedAtUtc = excluded.UpdatedAtUtc;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }

        sqlite3_bind_text16(statement, 1, note.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, note.title.c_str(), -1, SQLITE_TRANSIENT);
        std::wstring stored_body = note.body;
        if (note.kind == Note::Kind::Secret && !ProtectSecret(note.body, stored_body)) {
            last_error_ = L"Windows could not protect this secret note.";
            sqlite3_finalize(statement);
            return false;
        }
        sqlite3_bind_text16(statement, 3, stored_body.c_str(), -1, SQLITE_TRANSIENT);
        const std::wstring now = NowUtcIso();
        if (legacy_schema_) {
            sqlite3_bind_int(statement, 4, note.kind == Note::Kind::Secret ? 1 : 0);
        } else {
            sqlite3_bind_int(statement, 4, note.kind == Note::Kind::Secret ? 1 : 0);
            sqlite3_bind_text16(statement, 5, now.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(statement, 6, now.c_str(), -1, SQLITE_TRANSIENT);
        }
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    bool DeleteNote(const std::wstring& id) {
        if (!IsOpen()) {
            return false;
        }

        const char* sql = legacy_schema_ ? "DELETE FROM notes WHERE id = ?;"
                                         : "DELETE FROM notes WHERE Id = ?;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        sqlite3_bind_text16(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    FocusSettings LoadFocusSettings() {
        FocusSettings settings;
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, "SELECT Minutes, Domains FROM focus_settings WHERE Id = 1;",
                               -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return settings;
        }
        if (sqlite3_step(statement) == SQLITE_ROW) {
            settings.minutes = std::clamp(sqlite3_column_int(statement, 0), 1, 480);
            settings.domains = NormalizeFocusDomains(
                WideFromSqlite16(sqlite3_column_text16(statement, 1), sqlite3_column_bytes16(statement, 1)));
        }
        sqlite3_finalize(statement);
        return settings;
    }

    bool SaveFocusSettings(const FocusSettings& settings) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "INSERT INTO focus_settings (Id, Minutes, Domains) VALUES (1, ?, ?) "
                               "ON CONFLICT(Id) DO UPDATE SET Minutes = excluded.Minutes, Domains = excluded.Domains;",
                               -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        sqlite3_bind_int(statement, 1, std::clamp(settings.minutes, 1, 480));
        const std::wstring domains = JoinLines(settings.domains);
        sqlite3_bind_text16(statement, 2, domains.c_str(), -1, SQLITE_TRANSIENT);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    std::vector<Reminder> LoadReminders() {
        std::vector<Reminder> reminders;
        if (!IsOpen()) {
            return reminders;
        }

        sqlite3_stmt* statement = nullptr;
        const char* sql = "SELECT Id, Text, DueAtUtc, CreatedAtUtc, CompletedAtUtc, SnoozedUntilUtc "
                          "FROM reminders ORDER BY CompletedAtUtc IS NOT NULL, DueAtUtc;";
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return reminders;
        }
        while (sqlite3_step(statement) == SQLITE_ROW) {
            reminders.push_back(Reminder{
                WideFromSqlite16(sqlite3_column_text16(statement, 0), sqlite3_column_bytes16(statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(statement, 1), sqlite3_column_bytes16(statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(statement, 2), sqlite3_column_bytes16(statement, 2)),
                WideFromSqlite16(sqlite3_column_text16(statement, 3), sqlite3_column_bytes16(statement, 3)),
                WideFromSqlite16(sqlite3_column_text16(statement, 4), sqlite3_column_bytes16(statement, 4)),
                WideFromSqlite16(sqlite3_column_text16(statement, 5), sqlite3_column_bytes16(statement, 5)),
            });
        }
        sqlite3_finalize(statement);
        return reminders;
    }

    bool SaveReminder(const Reminder& reminder) {
        const char* sql =
            "INSERT INTO reminders (Id, Text, DueAtUtc, CreatedAtUtc, CompletedAtUtc, SnoozedUntilUtc) "
            "VALUES (?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(Id) DO UPDATE SET Text = excluded.Text, DueAtUtc = excluded.DueAtUtc, "
            "CompletedAtUtc = excluded.CompletedAtUtc, SnoozedUntilUtc = excluded.SnoozedUntilUtc;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        const std::wstring created = reminder.created_at_utc.empty() ? NowUtcIso() : reminder.created_at_utc;
        sqlite3_bind_text16(statement, 1, reminder.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, reminder.text.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 3, reminder.due_at_utc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 4, created.c_str(), -1, SQLITE_TRANSIENT);
        if (reminder.completed_at_utc.empty()) {
            sqlite3_bind_null(statement, 5);
        } else {
            sqlite3_bind_text16(statement, 5, reminder.completed_at_utc.c_str(), -1, SQLITE_TRANSIENT);
        }
        if (reminder.snoozed_until_utc.empty()) {
            sqlite3_bind_null(statement, 6);
        } else {
            sqlite3_bind_text16(statement, 6, reminder.snoozed_until_utc.c_str(), -1, SQLITE_TRANSIENT);
        }
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    bool DeleteReminder(const std::wstring& id) {
        return ExecuteIdStatement("DELETE FROM reminders WHERE Id = ?;", id);
    }

    std::vector<TodoGroup> LoadTodoGroups() {
        std::vector<TodoGroup> groups;
        if (!IsOpen()) {
            return groups;
        }

        sqlite3_stmt* group_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, Title, CreatedAtUtc, UpdatedAtUtc FROM todo_groups ORDER BY UpdatedAtUtc DESC;",
                               -1, &group_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return groups;
        }
        while (sqlite3_step(group_statement) == SQLITE_ROW) {
            groups.push_back(TodoGroup{
                WideFromSqlite16(sqlite3_column_text16(group_statement, 0), sqlite3_column_bytes16(group_statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(group_statement, 1), sqlite3_column_bytes16(group_statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(group_statement, 2), sqlite3_column_bytes16(group_statement, 2)),
                WideFromSqlite16(sqlite3_column_text16(group_statement, 3), sqlite3_column_bytes16(group_statement, 3)),
                {},
            });
        }
        sqlite3_finalize(group_statement);

        sqlite3_stmt* item_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, GroupId, Text, IsCompleted, CreatedAtUtc, UpdatedAtUtc "
                               "FROM todo_items ORDER BY GroupId, SortOrder;",
                               -1, &item_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return groups;
        }
        while (sqlite3_step(item_statement) == SQLITE_ROW) {
            const std::wstring group_id = WideFromSqlite16(sqlite3_column_text16(item_statement, 1),
                                                           sqlite3_column_bytes16(item_statement, 1));
            const auto group = std::find_if(groups.begin(), groups.end(), [&](const TodoGroup& candidate) {
                return candidate.id == group_id;
            });
            if (group != groups.end()) {
                group->items.push_back(TodoItem{
                    WideFromSqlite16(sqlite3_column_text16(item_statement, 0), sqlite3_column_bytes16(item_statement, 0)),
                    group_id,
                    WideFromSqlite16(sqlite3_column_text16(item_statement, 2), sqlite3_column_bytes16(item_statement, 2)),
                    sqlite3_column_int(item_statement, 3) != 0,
                    WideFromSqlite16(sqlite3_column_text16(item_statement, 4), sqlite3_column_bytes16(item_statement, 4)),
                    WideFromSqlite16(sqlite3_column_text16(item_statement, 5), sqlite3_column_bytes16(item_statement, 5)),
                });
            }
        }
        sqlite3_finalize(item_statement);
        return groups;
    }

    bool SaveTodoGroup(const TodoGroup& group) {
        if (!Execute("BEGIN TRANSACTION;")) {
            return false;
        }
        const std::wstring created = group.created_at_utc.empty() ? NowUtcIso() : group.created_at_utc;
        const std::wstring updated = group.updated_at_utc.empty() ? NowUtcIso() : group.updated_at_utc;
        sqlite3_stmt* group_statement = nullptr;
        const char* group_sql =
            "INSERT INTO todo_groups (Id, Title, CreatedAtUtc, UpdatedAtUtc) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(Id) DO UPDATE SET Title = excluded.Title, UpdatedAtUtc = excluded.UpdatedAtUtc;";
        if (sqlite3_prepare_v2(database_, group_sql, -1, &group_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_bind_text16(group_statement, 1, group.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(group_statement, 2, group.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(group_statement, 3, created.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(group_statement, 4, updated.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(group_statement) != SQLITE_DONE) {
            SetLastError();
            sqlite3_finalize(group_statement);
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_finalize(group_statement);

        sqlite3_stmt* delete_items = nullptr;
        if (sqlite3_prepare_v2(database_, "DELETE FROM todo_items WHERE GroupId = ?;", -1, &delete_items, nullptr) != SQLITE_OK) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_bind_text16(delete_items, 1, group.id.c_str(), -1, SQLITE_TRANSIENT);
        const bool deleted = sqlite3_step(delete_items) == SQLITE_DONE;
        sqlite3_finalize(delete_items);
        if (!deleted) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }

        const char* item_sql =
            "INSERT INTO todo_items (Id, GroupId, SortOrder, Text, IsCompleted, CreatedAtUtc, UpdatedAtUtc) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);";
        for (size_t index = 0; index < group.items.size(); ++index) {
            const TodoItem& item = group.items[index];
            sqlite3_stmt* item_statement = nullptr;
            if (sqlite3_prepare_v2(database_, item_sql, -1, &item_statement, nullptr) != SQLITE_OK) {
                SetLastError();
                Execute("ROLLBACK;");
                return false;
            }
            const std::wstring item_created = item.created_at_utc.empty() ? created : item.created_at_utc;
            const std::wstring item_updated = item.updated_at_utc.empty() ? updated : item.updated_at_utc;
            sqlite3_bind_text16(item_statement, 1, item.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(item_statement, 2, group.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(item_statement, 3, static_cast<int>(index));
            sqlite3_bind_text16(item_statement, 4, item.text.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(item_statement, 5, item.completed ? 1 : 0);
            sqlite3_bind_text16(item_statement, 6, item_created.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(item_statement, 7, item_updated.c_str(), -1, SQLITE_TRANSIENT);
            const bool inserted = sqlite3_step(item_statement) == SQLITE_DONE;
            sqlite3_finalize(item_statement);
            if (!inserted) {
                SetLastError();
                Execute("ROLLBACK;");
                return false;
            }
        }
        return Execute("COMMIT;");
    }

    bool DeleteTodoGroup(const std::wstring& id) {
        if (!Execute("BEGIN TRANSACTION;")) {
            return false;
        }
        const bool deleted_items = ExecuteIdStatement("DELETE FROM todo_items WHERE GroupId = ?;", id);
        const bool deleted_group = deleted_items && ExecuteIdStatement("DELETE FROM todo_groups WHERE Id = ?;", id);
        if (deleted_items && deleted_group) {
            return Execute("COMMIT;");
        }
        Execute("ROLLBACK;");
        return false;
    }

    std::vector<Streak> LoadStreaks() {
        std::vector<Streak> streaks;
        if (!IsOpen()) {
            return streaks;
        }

        sqlite3_stmt* streak_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, Title, Frequency, CreatedAtUtc, UpdatedAtUtc "
                               "FROM streaks ORDER BY UpdatedAtUtc DESC;",
                               -1, &streak_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return streaks;
        }
        while (sqlite3_step(streak_statement) == SQLITE_ROW) {
            streaks.push_back(Streak{
                WideFromSqlite16(sqlite3_column_text16(streak_statement, 0),
                                 sqlite3_column_bytes16(streak_statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(streak_statement, 1),
                                 sqlite3_column_bytes16(streak_statement, 1)),
                sqlite3_column_int(streak_statement, 2),
                WideFromSqlite16(sqlite3_column_text16(streak_statement, 3),
                                 sqlite3_column_bytes16(streak_statement, 3)),
                WideFromSqlite16(sqlite3_column_text16(streak_statement, 4),
                                 sqlite3_column_bytes16(streak_statement, 4)),
                {},
            });
        }
        sqlite3_finalize(streak_statement);

        sqlite3_stmt* completion_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT StreakId, CompletedDate FROM streak_completions "
                               "ORDER BY CompletedDate;",
                               -1, &completion_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return streaks;
        }
        while (sqlite3_step(completion_statement) == SQLITE_ROW) {
            const std::wstring streak_id = WideFromSqlite16(sqlite3_column_text16(completion_statement, 0),
                                                             sqlite3_column_bytes16(completion_statement, 0));
            const auto streak = std::find_if(streaks.begin(), streaks.end(), [&](const Streak& candidate) {
                return candidate.id == streak_id;
            });
            if (streak != streaks.end()) {
                streak->completed_dates.push_back(
                    WideFromSqlite16(sqlite3_column_text16(completion_statement, 1),
                                     sqlite3_column_bytes16(completion_statement, 1)));
            }
        }
        sqlite3_finalize(completion_statement);
        return streaks;
    }

    bool SaveStreak(const Streak& streak) {
        const char* sql =
            "INSERT INTO streaks (Id, Title, Frequency, CreatedAtUtc, UpdatedAtUtc) "
            "VALUES (?, ?, ?, ?, ?) "
            "ON CONFLICT(Id) DO UPDATE SET Title = excluded.Title, Frequency = excluded.Frequency, "
            "UpdatedAtUtc = excluded.UpdatedAtUtc;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        const std::wstring created = streak.created_at_utc.empty() ? NowUtcIso() : streak.created_at_utc;
        const std::wstring updated = streak.updated_at_utc.empty() ? NowUtcIso() : streak.updated_at_utc;
        sqlite3_bind_text16(statement, 1, streak.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, streak.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 3, streak.frequency == 2 ? 2 : 1);
        sqlite3_bind_text16(statement, 4, created.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 5, updated.c_str(), -1, SQLITE_TRANSIENT);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    bool ToggleStreakCompletion(const std::wstring& id, const std::wstring& date) {
        sqlite3_stmt* lookup = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT 1 FROM streak_completions WHERE StreakId = ? AND CompletedDate = ?;",
                               -1, &lookup, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        sqlite3_bind_text16(lookup, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(lookup, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        const bool already_completed = sqlite3_step(lookup) == SQLITE_ROW;
        sqlite3_finalize(lookup);

        sqlite3_stmt* statement = nullptr;
        const char* sql = already_completed
                              ? "DELETE FROM streak_completions WHERE StreakId = ? AND CompletedDate = ?;"
                              : "INSERT INTO streak_completions (StreakId, CompletedDate, CreatedAtUtc) "
                                "VALUES (?, ?, ?);";
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        sqlite3_bind_text16(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        if (!already_completed) {
            const std::wstring now = NowUtcIso();
            sqlite3_bind_text16(statement, 3, now.c_str(), -1, SQLITE_TRANSIENT);
        }
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    bool DeleteStreak(const std::wstring& id) {
        return ExecuteIdStatement("DELETE FROM streaks WHERE Id = ?;", id);
    }

    std::vector<ChatThread> LoadChatThreads() {
        std::vector<ChatThread> threads;
        if (!IsOpen()) {
            return threads;
        }

        sqlite3_stmt* thread_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, Title, CreatedAtUtc, UpdatedAtUtc "
                               "FROM chat_threads ORDER BY UpdatedAtUtc DESC;",
                               -1, &thread_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return threads;
        }
        while (sqlite3_step(thread_statement) == SQLITE_ROW) {
            threads.push_back(ChatThread{
                WideFromSqlite16(sqlite3_column_text16(thread_statement, 0),
                                 sqlite3_column_bytes16(thread_statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(thread_statement, 1),
                                 sqlite3_column_bytes16(thread_statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(thread_statement, 2),
                                 sqlite3_column_bytes16(thread_statement, 2)),
                WideFromSqlite16(sqlite3_column_text16(thread_statement, 3),
                                 sqlite3_column_bytes16(thread_statement, 3)),
                {},
            });
        }
        sqlite3_finalize(thread_statement);

        sqlite3_stmt* message_statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, ThreadId, Role, Body, CreatedAtUtc, IsError, AttachmentPath "
                               "FROM chat_messages ORDER BY ThreadId, Sequence;",
                               -1, &message_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return threads;
        }
        while (sqlite3_step(message_statement) == SQLITE_ROW) {
            const std::wstring thread_id = WideFromSqlite16(sqlite3_column_text16(message_statement, 1),
                                                             sqlite3_column_bytes16(message_statement, 1));
            const auto thread = std::find_if(threads.begin(), threads.end(), [&](const ChatThread& candidate) {
                return candidate.id == thread_id;
            });
            if (thread != threads.end()) {
                thread->messages.push_back(ChatMessage{
                    WideFromSqlite16(sqlite3_column_text16(message_statement, 0),
                                     sqlite3_column_bytes16(message_statement, 0)),
                    thread_id,
                    WideFromSqlite16(sqlite3_column_text16(message_statement, 2),
                                     sqlite3_column_bytes16(message_statement, 2)),
                    WideFromSqlite16(sqlite3_column_text16(message_statement, 3),
                                     sqlite3_column_bytes16(message_statement, 3)),
                    WideFromSqlite16(sqlite3_column_text16(message_statement, 4),
                                     sqlite3_column_bytes16(message_statement, 4)),
                    sqlite3_column_int(message_statement, 5) != 0,
                    WideFromSqlite16(sqlite3_column_text16(message_statement, 6),
                                     sqlite3_column_bytes16(message_statement, 6)),
                });
            }
        }
        sqlite3_finalize(message_statement);
        return threads;
    }

    bool SaveChatThread(const ChatThread& thread) {
        if (!Execute("BEGIN TRANSACTION;")) {
            return false;
        }

        const std::wstring created = thread.created_at_utc.empty() ? NowUtcIso() : thread.created_at_utc;
        const std::wstring updated = thread.updated_at_utc.empty() ? NowUtcIso() : thread.updated_at_utc;
        sqlite3_stmt* thread_statement = nullptr;
        const char* thread_sql =
            "INSERT INTO chat_threads (Id, Title, CreatedAtUtc, UpdatedAtUtc) VALUES (?, ?, ?, ?) "
            "ON CONFLICT(Id) DO UPDATE SET Title = excluded.Title, UpdatedAtUtc = excluded.UpdatedAtUtc;";
        if (sqlite3_prepare_v2(database_, thread_sql, -1, &thread_statement, nullptr) != SQLITE_OK) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_bind_text16(thread_statement, 1, thread.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(thread_statement, 2, thread.title.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(thread_statement, 3, created.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(thread_statement, 4, updated.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(thread_statement) != SQLITE_DONE) {
            SetLastError();
            sqlite3_finalize(thread_statement);
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_finalize(thread_statement);

        if (!ExecuteIdStatement("DELETE FROM chat_messages WHERE ThreadId = ?;", thread.id)) {
            Execute("ROLLBACK;");
            return false;
        }

        const char* message_sql =
            "INSERT INTO chat_messages (Id, ThreadId, Sequence, Role, Body, CreatedAtUtc, IsError, AttachmentPath) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        for (size_t index = 0; index < thread.messages.size(); ++index) {
            const ChatMessage& message = thread.messages[index];
            sqlite3_stmt* message_statement = nullptr;
            if (sqlite3_prepare_v2(database_, message_sql, -1, &message_statement, nullptr) != SQLITE_OK) {
                SetLastError();
                Execute("ROLLBACK;");
                return false;
            }
            const std::wstring message_created = message.created_at_utc.empty() ? updated : message.created_at_utc;
            sqlite3_bind_text16(message_statement, 1, message.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(message_statement, 2, thread.id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(message_statement, 3, static_cast<int>(index));
            sqlite3_bind_text16(message_statement, 4, message.role.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(message_statement, 5, message.body.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(message_statement, 6, message_created.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(message_statement, 7, message.is_error ? 1 : 0);
            if (message.attachment_path.empty()) {
                sqlite3_bind_null(message_statement, 8);
            } else {
                sqlite3_bind_text16(message_statement, 8, message.attachment_path.c_str(), -1, SQLITE_TRANSIENT);
            }
            const bool inserted = sqlite3_step(message_statement) == SQLITE_DONE;
            sqlite3_finalize(message_statement);
            if (!inserted) {
                SetLastError();
                Execute("ROLLBACK;");
                return false;
            }
        }
        return Execute("COMMIT;");
    }

    bool DeleteChatThread(const std::wstring& id) {
        return ExecuteIdStatement("DELETE FROM chat_threads WHERE Id = ?;", id);
    }

    std::optional<AiModelProfile> LoadAiModel() {
        const std::vector<AiModelProfile> profiles = LoadAiModels();
        const auto profile = std::find_if(profiles.begin(), profiles.end(), [](const AiModelProfile& candidate) {
            return candidate.enabled;
        });
        return profile == profiles.end() ? std::nullopt : std::optional<AiModelProfile>(*profile);
    }

    std::vector<AiModelProfile> LoadAiModels() {
        std::vector<AiModelProfile> profiles;
        if (!IsOpen()) {
            return profiles;
        }

        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_,
                               "SELECT Id, Provider, Name, ModelId, EncryptedApiKey, Enabled, IsDefault "
                               "FROM ai_models ORDER BY IsDefault DESC, Name;",
                               -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return profiles;
        }
        while (sqlite3_step(statement) == SQLITE_ROW) {
            profiles.push_back(AiModelProfile{
                WideFromSqlite16(sqlite3_column_text16(statement, 0),
                                 sqlite3_column_bytes16(statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(statement, 1),
                                 sqlite3_column_bytes16(statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(statement, 2),
                                 sqlite3_column_bytes16(statement, 2)),
                WideFromSqlite16(sqlite3_column_text16(statement, 3),
                                 sqlite3_column_bytes16(statement, 3)),
                WideFromSqlite16(sqlite3_column_text16(statement, 4),
                                 sqlite3_column_bytes16(statement, 4)),
                sqlite3_column_int(statement, 5) != 0,
                sqlite3_column_int(statement, 6) != 0,
            });
        }
        sqlite3_finalize(statement);
        return profiles;
    }

    bool SaveAiModel(const AiModelProfile& profile) {
        if (!Execute("BEGIN TRANSACTION;")) {
            return false;
        }
        if (profile.is_default && !Execute("UPDATE ai_models SET IsDefault = 0;")) {
            Execute("ROLLBACK;");
            return false;
        }

        sqlite3_stmt* statement = nullptr;
        const char* sql =
            "INSERT INTO ai_models (Id, Provider, Name, ModelId, EncryptedApiKey, Enabled, IsDefault) "
            "VALUES (?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(Id) DO UPDATE SET Provider = excluded.Provider, Name = excluded.Name, "
            "ModelId = excluded.ModelId, EncryptedApiKey = excluded.EncryptedApiKey, "
            "Enabled = excluded.Enabled, IsDefault = excluded.IsDefault;";
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_bind_text16(statement, 1, profile.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 2, profile.provider.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 3, profile.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 4, profile.model_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text16(statement, 5, profile.encrypted_api_key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 6, profile.enabled ? 1 : 0);
        sqlite3_bind_int(statement, 7, profile.is_default ? 1 : 0);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        if (!success) {
            Execute("ROLLBACK;");
            return false;
        }
        return Execute("COMMIT;");
    }

    bool SetDefaultAiModel(const std::wstring& id) {
        if (!Execute("BEGIN TRANSACTION;") ||
            !Execute("UPDATE ai_models SET IsDefault = 0;")) {
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, "UPDATE ai_models SET IsDefault = 1 WHERE Id = ? AND Enabled = 1;",
                               -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            Execute("ROLLBACK;");
            return false;
        }
        sqlite3_bind_text16(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success ? Execute("COMMIT;") : (Execute("ROLLBACK;"), false);
    }

    bool DeleteAiModel(const std::wstring& id) {
        return ExecuteIdStatement("DELETE FROM ai_models WHERE Id = ?;", id);
    }

private:
    bool ExecuteIdStatement(const char* sql, const std::wstring& id) {
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return false;
        }
        sqlite3_bind_text16(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        const bool success = sqlite3_step(statement) == SQLITE_DONE;
        if (!success) {
            SetLastError();
        }
        sqlite3_finalize(statement);
        return success;
    }

    bool HasTableColumn(const char* table_name, const wchar_t* expected_name) {
        const std::string sql = std::string("PRAGMA table_info(") + table_name + ");";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(database_, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) {
            return false;
        }

        bool found = false;
        while (sqlite3_step(statement) == SQLITE_ROW) {
            const std::wstring name = WideFromSqlite16(sqlite3_column_text16(statement, 1),
                                                       sqlite3_column_bytes16(statement, 1));
            if (_wcsicmp(name.c_str(), expected_name) == 0) {
                found = true;
                break;
            }
        }
        sqlite3_finalize(statement);
        return found;
    }

    bool HasColumn(const wchar_t* expected_name) {
        return HasTableColumn("notes", expected_name);
    }

    bool Execute(const char* sql) {
        char* error = nullptr;
        const int result = sqlite3_exec(database_, sql, nullptr, nullptr, &error);
        if (result != SQLITE_OK) {
            last_error_ = error != nullptr ? WideFromUtf8(error) : L"SQLite operation failed.";
            sqlite3_free(error);
            return false;
        }
        return true;
    }

    void SetLastError() {
        last_error_ = database_ != nullptr ? WideFromSqlite16(sqlite3_errmsg16(database_), -1)
                                            : L"SQLite operation failed.";
    }

    sqlite3* database_ = nullptr;
    std::wstring last_error_;
    bool legacy_schema_ = false;
    bool has_kind_ = false;
};

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

HFONT UiFont(int size, bool bold = false) {
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

LRESULT CALLBACK SelectAllEditProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kSelectAllOriginalProperty));
    if (message == WM_KEYDOWN && w_param == 'A' &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        SendMessageW(window, EM_SETSEL, 0, -1);
        return 0;
    }
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

void EnableSelectAll(HWND edit) {
    if (edit == nullptr) {
        return;
    }
    SetPropW(edit, kSelectAllOriginalProperty,
             reinterpret_cast<HANDLE>(SetWindowLongPtrW(
                 edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SelectAllEditProc))));
}

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

struct NoteDraft {
    std::wstring title;
    std::wstring body;
    Note::Kind kind = Note::Kind::Text;
    bool accepted = false;
    bool done = false;
    HWND title_label = nullptr;
    HWND title_edit = nullptr;
    HWND type_label = nullptr;
    HWND body_edit = nullptr;
    HWND body_label = nullptr;
    HWND kind_combo = nullptr;
    HWND save_button = nullptr;
    HFONT font = nullptr;
};

constexpr wchar_t kNoteTabDraftProperty[] = L"TwoSemi.Note.TabDraft";
constexpr wchar_t kNoteTabNextProperty[] = L"TwoSemi.Note.TabNext";
constexpr wchar_t kNoteTabPreviousProperty[] = L"TwoSemi.Note.TabPrevious";
constexpr wchar_t kNoteTabOriginalProperty[] = L"TwoSemi.Note.TabOriginal";

LRESULT CALLBACK NoteTabProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    if (message == WM_KEYDOWN && w_param == VK_TAB) {
        const bool backwards = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const wchar_t* target_property = backwards ? kNoteTabPreviousProperty : kNoteTabNextProperty;
        HWND target = reinterpret_cast<HWND>(GetPropW(window, target_property));
        if (target != nullptr && IsWindow(target)) {
            SetFocus(target);
            return 0;
        }
    }
    if (message == WM_KEYDOWN && w_param == VK_ESCAPE) {
        HWND parent = GetParent(window);
        if (parent != nullptr) {
            SendMessageW(parent, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    if (message == WM_KEYDOWN && w_param == VK_RETURN &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        HWND parent = GetParent(window);
        if (parent != nullptr) {
            SendMessageW(parent, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            return 0;
        }
    }
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kNoteTabOriginalProperty));
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

void EnableNoteTab(HWND control, NoteDraft& draft, HWND next, HWND previous) {
    if (control == nullptr) {
        return;
    }
    SetPropW(control, kNoteTabDraftProperty, reinterpret_cast<HANDLE>(&draft));
    SetPropW(control, kNoteTabNextProperty, reinterpret_cast<HANDLE>(next));
    SetPropW(control, kNoteTabPreviousProperty, reinterpret_cast<HANDLE>(previous));
    SetPropW(control, kNoteTabOriginalProperty,
             reinterpret_cast<HANDLE>(SetWindowLongPtrW(
                 control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(NoteTabProc))));
}

void LayoutNoteEditor(NoteDraft& draft) {
    if (draft.body_edit == nullptr) {
        return;
    }

    HWND window = GetParent(draft.body_edit);
    RECT client{};
    GetClientRect(window, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int top_bar = 30;
    const int bar_height = 28;
    const int bar_y = height - bar_height;
    const bool is_secret = SendMessageW(draft.kind_combo, CB_GETCURSEL, 0, 0) == 1;
    const int body_height = is_secret ? 24 : std::max(60, bar_y - top_bar - 4);

    ShowWindow(draft.title_label, SW_HIDE);
    ShowWindow(draft.type_label, SW_HIDE);
    ShowWindow(draft.body_label, SW_HIDE);

    SetWindowPos(draft.body_edit, nullptr, 0, top_bar, width, body_height,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    const int save_width = 60;
    const int combo_width = 80;
    const int gap = 4;
    const int right_block = save_width;
    const int title_width = std::max(100, width - right_block - combo_width - gap * 2);

    SetWindowPos(draft.title_edit, nullptr, 0, bar_y, title_width, bar_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(draft.kind_combo, nullptr, title_width + gap, bar_y, combo_width, 150,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(draft.save_button, nullptr, width - right_block, bar_y, save_width, bar_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void UpdateSecretEditor(NoteDraft& draft) {
    const bool is_secret = SendMessageW(draft.kind_combo, CB_GETCURSEL, 0, 0) == 1;
    LONG_PTR style = GetWindowLongPtrW(draft.body_edit, GWL_STYLE);
    if (is_secret) {
        style &= ~(ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL);
        style |= ES_AUTOHSCROLL | ES_PASSWORD;
    } else {
        style &= ~(ES_AUTOHSCROLL | ES_PASSWORD);
        style |= ES_MULTILINE | ES_AUTOVSCROLL;
    }
    SetWindowLongPtrW(draft.body_edit, GWL_STYLE, style);
    LayoutNoteEditor(draft);
    InvalidateRect(draft.body_edit, nullptr, TRUE);
}

bool SaveNoteDraft(NoteDraft& draft, HWND window) {
    draft.title = Trim(WindowText(draft.title_edit));
    draft.body = WindowText(draft.body_edit);
    draft.kind = SendMessageW(draft.kind_combo, CB_GETCURSEL, 0, 0) == 1
                     ? Note::Kind::Secret
                     : Note::Kind::Text;
    if (Trim(draft.body).empty()) {
        MessageBoxW(window, L"Write something in the note first.", L"TwoSemi",
                    MB_OK | MB_ICONINFORMATION);
        SetFocus(draft.body_edit);
        return false;
    }
    draft.accepted = true;
    draft.done = true;
    DestroyWindow(window);
    return true;
}

LRESULT CALLBACK NoteEditorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* draft = reinterpret_cast<NoteDraft*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        draft = static_cast<NoteDraft*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(draft));
    }

    switch (message) {
    case WM_CREATE: {
        const HFONT font = UiFont(10);
        draft->font = font;
        draft->body_label = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                        0, 0, 0, 0, window, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
        draft->body_edit = CreateWindowExW(
            0, L"EDIT", draft->body.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
            0, 0, 544, 148, window, reinterpret_cast<HMENU>(102), GetModuleHandleW(nullptr), nullptr);

        draft->title_label = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                         0, 0, 0, 0, window, nullptr,
                                         GetModuleHandleW(nullptr), nullptr);
        draft->title_edit = CreateWindowExW(0, L"EDIT", draft->title.c_str(),
                                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                                            0, 0, 200, 28,
                                            window, reinterpret_cast<HMENU>(101), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(draft->title_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"title"));
        draft->type_label = CreateWindowW(L"STATIC", L"", WS_CHILD,
                                        0, 0, 0, 0, window, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
        draft->kind_combo = CreateWindowExW(
            0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            0, 0, 80, 150, window, reinterpret_cast<HMENU>(103),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(draft->kind_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Text"));
        SendMessageW(draft->kind_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Secret"));
        SendMessageW(draft->kind_combo, CB_SETCURSEL, draft->kind == Note::Kind::Secret ? 1 : 0, 0);
        draft->save_button = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                  0, 0, 60, 28, window, reinterpret_cast<HMENU>(IDOK),
                                  GetModuleHandleW(nullptr), nullptr);

        EnableSelectAll(draft->title_edit);
        EnableSelectAll(draft->body_edit);
        EnableNoteTab(draft->body_edit, *draft, draft->title_edit, draft->save_button);
        EnableNoteTab(draft->title_edit, *draft, draft->kind_combo, draft->body_edit);
        EnableNoteTab(draft->kind_combo, *draft, draft->save_button, draft->title_edit);
        EnableNoteTab(draft->save_button, *draft, draft->body_edit, draft->kind_combo);

        for (HWND child : {draft->kind_combo, draft->title_edit, draft->body_edit,
                           draft->save_button}) {
            SetFont(child, font);
        }
        SendMessageW(draft->title_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        UpdateSecretEditor(*draft);
        SetFocus(draft->body_edit);
        return 0;
    }
    case WM_SIZE:
        LayoutNoteEditor(*draft);
        return 0;
    case WM_COMMAND:
        if (LOWORD(w_param) == 103 && HIWORD(w_param) == CBN_SELCHANGE) {
            UpdateSecretEditor(*draft);
            SetFocus(draft->body_edit);
            return 0;
        }
        if (LOWORD(w_param) == IDOK) {
            SaveNoteDraft(*draft, window);
            return 0;
        }
        break;
    case WM_CLOSE:
        SaveNoteDraft(*draft, window);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedText);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, draft->font));
        const std::wstring title = L"twosemi :: note  |  Esc save";
        TextOutW(dc, 4, 5, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        const bool static_control = message == WM_CTLCOLORSTATIC;
        SetTextColor(dc, static_control ? kMutedText : kText);
        SetBkColor(dc, static_control ? kBackground : kInputBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(static_control ? background_brush : input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool save_button = draw->CtlID == IDOK;
        HBRUSH fill = CreateSolidBrush(save_button ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(save_button ? kAccent : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, kAccent);
        DrawTextW(draw->hDC, L"Save", -1,
                  &draw->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_DESTROY:
        draft->done = true;
        return 0;
    default:
        break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

bool EditNote(HWND owner, NoteDraft& draft) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = NoteEditorProc;
        window_class.lpszClassName = kNoteEditorClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    int width = kNoteEditorWidth;
    int height = kNoteEditorHeight;
    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info)) {
        const RECT& work = monitor_info.rcWork;
        const int work_width = static_cast<int>(work.right - work.left);
        const int work_height = static_cast<int>(work.bottom - work.top);
        width = std::max(560, std::min(width, work_width - 40));
        height = std::max(440, std::min(height, work_height - 40));
    }

    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kNoteEditorClassName,
        L"New note",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &draft);
    if (dialog == nullptr) {
        return false;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, GetDlgItem(dialog, 102));

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(draft.font);
    return draft.accepted;
}

enum class QuickEditorKind {
    Reminder,
    TodoGroup,
    TodoTask,
    Streak,
    ChatThread,
};

struct QuickDraft {
    QuickEditorKind kind;
    std::wstring first;
    std::wstring second;
    int frequency = 1;
    bool accepted = false;
    bool done = false;
    HWND first_edit = nullptr;
    HWND second_edit = nullptr;
    HWND frequency_combo = nullptr;
    HFONT font = nullptr;
};

const wchar_t* QuickEditorTitle(QuickEditorKind kind) {
    switch (kind) {
    case QuickEditorKind::Reminder:
        return L"twosemi :: reminder";
    case QuickEditorKind::TodoGroup:
        return L"twosemi :: todo group";
    case QuickEditorKind::TodoTask:
        return L"twosemi :: task";
    case QuickEditorKind::Streak:
        return L"twosemi :: streak";
    case QuickEditorKind::ChatThread:
        return L"twosemi :: chat title";
    }
    return L"twosemi";
}

LRESULT CALLBACK QuickEditorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* draft = reinterpret_cast<QuickDraft*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        draft = static_cast<QuickDraft*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(draft));
    }

    switch (message) {
    case WM_CREATE: {
        draft->font = UiFont(10);
        const wchar_t* placeholder = draft->kind == QuickEditorKind::Reminder
                                         ? L"reminder text"
                                         : draft->kind == QuickEditorKind::TodoGroup
                                               ? L"group name"
                                               : draft->kind == QuickEditorKind::Streak
                                                     ? L"habit"
                                                     : draft->kind == QuickEditorKind::ChatThread ? L"chat title" : L"task";
        draft->first_edit = CreateWindowExW(
            0, L"EDIT", draft->first.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            0, 30, 580, 28, window, reinterpret_cast<HMENU>(101),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(draft->first_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(placeholder));
        if (draft->kind == QuickEditorKind::Reminder) {
            draft->second_edit = CreateWindowExW(
                0, L"EDIT", draft->second.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                0, 0, 200, 28, window, reinterpret_cast<HMENU>(102),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(draft->second_edit, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"yyyy-mm-dd hh:mm"));
        } else if (draft->kind == QuickEditorKind::Streak) {
            draft->frequency_combo = CreateWindowExW(
                0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                0, 0, 100, 150, window, reinterpret_cast<HMENU>(102),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(draft->frequency_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Daily"));
            SendMessageW(draft->frequency_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Weekdays"));
            SendMessageW(draft->frequency_combo, CB_SETCURSEL, draft->frequency == 2 ? 1 : 0, 0);
        }
        EnableSelectAll(draft->first_edit);
        EnableSelectAll(draft->second_edit);
        HWND save = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                  0, 0, 60, 28, window, reinterpret_cast<HMENU>(IDOK),
                                  GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                    0, 0, 60, 28, window, reinterpret_cast<HMENU>(IDCANCEL),
                                    GetModuleHandleW(nullptr), nullptr);
        SetFont(draft->first_edit, draft->font);
        SetFont(draft->second_edit, draft->font);
        SetFont(draft->frequency_combo, draft->font);
        SetFont(save, draft->font);
        SetFont(cancel, draft->font);
        SendMessageW(draft->first_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        if (draft->second_edit != nullptr) {
            SendMessageW(draft->second_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        }

        RECT client{};
        GetClientRect(window, &client);
        const int width = client.right;
        const int height = client.bottom;
        const int bar_h = 28;
        const int bar_y = height - bar_h;
        const int gap = 4;
        const int save_w = 60;
        const int cancel_w = 60;
        const int right_block = save_w + gap + cancel_w;
        if (draft->kind == QuickEditorKind::Reminder) {
            const int when_w = 180;
            SetWindowPos(draft->first_edit, nullptr, 0, 30, width, bar_y - 30 - gap, SWP_NOZORDER);
            SetWindowPos(draft->second_edit, nullptr, 0, bar_y, when_w, bar_h, SWP_NOZORDER);
            SetWindowPos(save, nullptr, width - right_block, bar_y, save_w, bar_h, SWP_NOZORDER);
            SetWindowPos(cancel, nullptr, width - cancel_w, bar_y, cancel_w, bar_h, SWP_NOZORDER);
        } else if (draft->kind == QuickEditorKind::Streak) {
            const int combo_w = 100;
            SetWindowPos(draft->first_edit, nullptr, 0, 30, width, bar_y - 30 - gap, SWP_NOZORDER);
            SetWindowPos(draft->frequency_combo, nullptr, 0, bar_y, combo_w, 150, SWP_NOZORDER);
            SetWindowPos(save, nullptr, width - right_block, bar_y, save_w, bar_h, SWP_NOZORDER);
            SetWindowPos(cancel, nullptr, width - cancel_w, bar_y, cancel_w, bar_h, SWP_NOZORDER);
        } else {
            SetWindowPos(draft->first_edit, nullptr, 0, 30, width, bar_y - 30 - gap, SWP_NOZORDER);
            SetWindowPos(save, nullptr, width - right_block, bar_y, save_w, bar_h, SWP_NOZORDER);
            SetWindowPos(cancel, nullptr, width - cancel_w, bar_y, cancel_w, bar_h, SWP_NOZORDER);
        }
        SetFocus(draft->first_edit);
        SendMessageW(draft->first_edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == IDOK) {
            draft->first = Trim(WindowText(draft->first_edit));
            if (draft->first.empty()) {
                MessageBoxW(window, L"Enter a value first.", L"TwoSemi", MB_OK | MB_ICONINFORMATION);
                SetFocus(draft->first_edit);
                return 0;
            }
            if (draft->kind == QuickEditorKind::Reminder) {
                draft->second = Trim(WindowText(draft->second_edit));
                std::wstring utc_value;
                if (!ParseLocalReminderTime(draft->second, utc_value)) {
                    MessageBoxW(window, L"Use the format yyyy-mm-dd hh:mm.", L"TwoSemi",
                                MB_OK | MB_ICONINFORMATION);
                    SetFocus(draft->second_edit);
                    return 0;
                }
                draft->second = utc_value;
            } else if (draft->kind == QuickEditorKind::Streak) {
                draft->frequency = SendMessageW(draft->frequency_combo, CB_GETCURSEL, 0, 0) == 1 ? 2 : 1;
            }
            draft->accepted = true;
            draft->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(w_param) == IDCANCEL) {
            draft->done = true;
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        draft->done = true;
        DestroyWindow(window);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedText);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, draft->font));
        const std::wstring title = std::wstring(QuickEditorTitle(draft->kind)) + L"  |  Esc cancel";
        TextOutW(dc, 4, 5, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetTextColor(dc, kText);
        SetBkColor(dc, kInputBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool save_button = draw->CtlID == IDOK;
        HBRUSH fill = CreateSolidBrush(save_button ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(save_button ? kAccent : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, save_button ? kAccent : kText);
        DrawTextW(draw->hDC, save_button ? L"Save" : L"Cancel", -1, &draw->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_DESTROY:
        draft->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool EditQuick(HWND owner, QuickDraft& draft) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = QuickEditorProc;
        window_class.lpszClassName = kQuickEditorClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    const int height = 90;
    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kQuickEditorClassName,
        QuickEditorTitle(draft.kind),
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        580,
        height,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &draft);
    if (dialog == nullptr) {
        return false;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, GetDlgItem(dialog, 101));

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(draft.font);
    return draft.accepted;
}

struct FocusDraft {
    int minutes = 25;
    std::wstring domains;
    bool accepted = false;
    bool done = false;
    HWND minutes_edit = nullptr;
    HWND domains_edit = nullptr;
    HFONT font = nullptr;
};

LRESULT CALLBACK FocusEditorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* draft = reinterpret_cast<FocusDraft*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        draft = static_cast<FocusDraft*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(draft));
    }

    switch (message) {
    case WM_CREATE: {
        draft->font = UiFont(10);
        HWND minutes_label = CreateWindowW(L"STATIC", L"Duration (minutes)", WS_CHILD | WS_VISIBLE,
                                           18, 42, 180, 22, window, nullptr,
                                           GetModuleHandleW(nullptr), nullptr);
        draft->minutes_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", std::to_wstring(draft->minutes).c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER | WS_TABSTOP,
            18, 64, 140, 30, window, reinterpret_cast<HMENU>(101),
            GetModuleHandleW(nullptr), nullptr);
        HWND domains_label = CreateWindowW(L"STATIC", L"Blocked websites (one domain per line)",
                                           WS_CHILD | WS_VISIBLE, 18, 108, 360, 22, window, nullptr,
                                           GetModuleHandleW(nullptr), nullptr);
        draft->domains_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", draft->domains.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN | WS_TABSTOP,
            18, 132, 604, 230, window, reinterpret_cast<HMENU>(102),
            GetModuleHandleW(nullptr), nullptr);
        EnableSelectAll(draft->minutes_edit);
        EnableSelectAll(draft->domains_edit);
        SendMessageW(draft->domains_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"youtube.com\r\nreddit.com"));
        HWND start = CreateWindowW(L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                   458, 378, 78, 32, window, reinterpret_cast<HMENU>(IDOK),
                                   GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                    546, 378, 76, 32, window, reinterpret_cast<HMENU>(IDCANCEL),
                                    GetModuleHandleW(nullptr), nullptr);
        for (HWND child : {minutes_label, draft->minutes_edit, domains_label, draft->domains_edit, start, cancel}) {
            SetFont(child, draft->font);
        }
        SetFocus(draft->minutes_edit);
        SendMessageW(draft->minutes_edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == IDOK) {
            const std::wstring minutes_text = Trim(WindowText(draft->minutes_edit));
            wchar_t* end = nullptr;
            const long minutes = wcstol(minutes_text.c_str(), &end, 10);
            if (minutes_text.empty() || end == minutes_text.c_str() || *end != L'\0' || minutes < 1 || minutes > 480) {
                MessageBoxW(window, L"Choose a duration between 1 and 480 minutes.", L"TwoSemi",
                            MB_OK | MB_ICONINFORMATION);
                SetFocus(draft->minutes_edit);
                return 0;
            }
            draft->minutes = static_cast<int>(minutes);
            draft->domains = WindowText(draft->domains_edit);
            draft->accepted = true;
            draft->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(w_param) == IDCANCEL) {
            draft->done = true;
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        draft->done = true;
        DestroyWindow(window);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kAccent);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, draft->font));
        const std::wstring title = L"twosemi :: focus mode  |  Esc cancel";
        TextOutW(dc, 18, 8, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        const bool static_control = message == WM_CTLCOLORSTATIC;
        SetTextColor(dc, static_control ? kMutedText : kText);
        SetBkColor(dc, static_control ? kBackground : kInputBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(static_control ? background_brush : input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool start_button = draw->CtlID == IDOK;
        HBRUSH fill = CreateSolidBrush(start_button ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(start_button ? kAccent : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, start_button ? kAccent : kText);
        DrawTextW(draw->hDC, start_button ? L"Start" : L"Cancel", -1, &draw->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_DESTROY:
        draft->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool EditFocus(HWND owner, FocusDraft& draft) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = FocusEditorProc;
        window_class.lpszClassName = kFocusEditorClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kFocusEditorClassName,
        L"Focus mode",
        WS_POPUP | WS_BORDER,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        640,
        430,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &draft);
    if (dialog == nullptr) {
        return false;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, draft.minutes_edit);

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(draft.font);
    return draft.accepted;
}

std::string JsonEscape(std::wstring_view value) {
    const std::string utf8 = Utf8FromWide(value);
    std::string escaped;
    escaped.reserve(utf8.size() + 16);
    for (unsigned char character : utf8) {
        switch (character) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(static_cast<char>(character)); break;
        }
    }
    return escaped;
}

void AppendJsonCodePoint(std::string& output, unsigned int code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::optional<std::string> ReadJsonString(const std::string& json, size_t quote) {
    if (quote >= json.size() || json[quote] != '"') {
        return std::nullopt;
    }

    std::string value;
    for (size_t index = quote + 1; index < json.size(); ++index) {
        const char character = json[index];
        if (character == '"') {
            return value;
        }
        if (character != '\\') {
            value.push_back(character);
            continue;
        }
        if (++index >= json.size()) {
            return std::nullopt;
        }
        switch (json[index]) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        case 'u': {
            if (index + 4 >= json.size()) {
                return std::nullopt;
            }
            unsigned int code_point = 0;
            for (size_t digit = 1; digit <= 4; ++digit) {
                const char hex = json[index + digit];
                code_point <<= 4;
                if (hex >= '0' && hex <= '9') code_point += hex - '0';
                else if (hex >= 'a' && hex <= 'f') code_point += hex - 'a' + 10;
                else if (hex >= 'A' && hex <= 'F') code_point += hex - 'A' + 10;
                else return std::nullopt;
            }
            AppendJsonCodePoint(value, code_point);
            index += 4;
            break;
        }
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::string> ReadJsonField(const std::string& json, std::string_view field) {
    const std::string key = "\"" + std::string(field) + "\"";
    size_t search_from = 0;
    while (true) {
        const size_t key_position = json.find(key, search_from);
        if (key_position == std::string::npos) {
            return std::nullopt;
        }
        const size_t colon = json.find(':', key_position + key.size());
        if (colon == std::string::npos) {
            return std::nullopt;
        }
        const size_t quote = json.find('"', colon + 1);
        if (quote != std::string::npos) {
            return ReadJsonString(json, quote);
        }
        search_from = key_position + key.size();
    }
}

std::string UrlEncodePathSegment(std::wstring_view value) {
    const std::string utf8 = Utf8FromWide(value);
    constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char character : utf8) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') ||
            character == '-' || character == '_' || character == '.' || character == '~') {
            encoded.push_back(static_cast<char>(character));
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(character >> 4) & 0x0F]);
            encoded.push_back(hex[character & 0x0F]);
        }
    }
    return encoded;
}

std::wstring EnvironmentValue(const wchar_t* name) {
    const DWORD required = GetEnvironmentVariableW(name, nullptr, 0);
    if (required == 0) {
        return {};
    }
    std::wstring value(static_cast<size_t>(required), L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), required);
    value.resize(static_cast<size_t>(length));
    return Trim(value);
}

std::wstring ChatAttachmentDirectory() {
    const std::wstring database_path = LocalDatabasePath();
    const size_t separator = database_path.find_last_of(L"\\/");
    const std::wstring directory = (separator == std::wstring::npos ? L"." : database_path.substr(0, separator)) +
                                   L"\\attachments";
    CreateDirectoryW(directory.c_str(), nullptr);
    return directory;
}

std::wstring FileExtension(const std::wstring& path) {
    const size_t separator = path.find_last_of(L"\\/");
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (separator != std::wstring::npos && dot < separator)) {
        return L".png";
    }
    return Lowercase(path.substr(dot));
}

std::wstring StoreChatAttachment(const std::wstring& source_path) {
    if (source_path.empty() || GetFileAttributesW(source_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return {};
    }
    Gdiplus::Bitmap image(source_path.c_str(), FALSE);
    if (image.GetLastStatus() != Gdiplus::Ok) {
        return {};
    }
    std::wstring id = NewId();
    id.erase(std::remove(id.begin(), id.end(), L'{'), id.end());
    id.erase(std::remove(id.begin(), id.end(), L'}'), id.end());
    const std::wstring destination = ChatAttachmentDirectory() + L"\\chat-" + id + FileExtension(source_path);
    return CopyFileW(source_path.c_str(), destination.c_str(), TRUE) != FALSE ? destination : L"";
}

bool GetPngEncoder(CLSID& encoder) {
    UINT count = 0;
    UINT bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) {
        return false;
    }
    std::vector<BYTE> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) {
        return false;
    }
    for (UINT index = 0; index < count; ++index) {
        if (wcscmp(encoders[index].MimeType, L"image/png") == 0) {
            encoder = encoders[index].Clsid;
            return true;
        }
    }
    return false;
}

bool SaveBitmapAsPng(HBITMAP bitmap, const std::wstring& path) {
    if (bitmap == nullptr) {
        return false;
    }
    Gdiplus::Bitmap image(bitmap, nullptr);
    CLSID encoder{};
    return image.GetLastStatus() == Gdiplus::Ok && GetPngEncoder(encoder) &&
           image.Save(path.c_str(), &encoder, nullptr) == Gdiplus::Ok;
}

HBITMAP CreateImageThumbnail(const std::wstring& path, int size = 32) {
    Gdiplus::Bitmap source(path.c_str(), FALSE);
    if (source.GetLastStatus() != Gdiplus::Ok || source.GetWidth() == 0 || source.GetHeight() == 0) {
        return nullptr;
    }
    Gdiplus::Bitmap canvas(size, size, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&canvas);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.Clear(Gdiplus::Color::MakeARGB(255, 17, 21, 20));
    const double scale = std::min(static_cast<double>(size) / source.GetWidth(),
                                  static_cast<double>(size) / source.GetHeight());
    const int width = std::max(1, static_cast<int>(source.GetWidth() * scale));
    const int height = std::max(1, static_cast<int>(source.GetHeight() * scale));
    const int x = (size - width) / 2;
    const int y = (size - height) / 2;
    graphics.DrawImage(&source, Gdiplus::Rect(x, y, width, height), 0, 0,
                       static_cast<int>(source.GetWidth()), static_cast<int>(source.GetHeight()),
                       Gdiplus::UnitPixel);
    HBITMAP thumbnail = nullptr;
    canvas.GetHBITMAP(Gdiplus::Color::MakeARGB(255, 17, 21, 20), &thumbnail);
    return thumbnail;
}

std::string Base64Encode(const std::string& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(((bytes.size() + 2) / 3) * 4);
    for (size_t index = 0; index < bytes.size(); index += 3) {
        const unsigned int first = static_cast<unsigned char>(bytes[index]);
        const unsigned int second = index + 1 < bytes.size() ? static_cast<unsigned char>(bytes[index + 1]) : 0;
        const unsigned int third = index + 2 < bytes.size() ? static_cast<unsigned char>(bytes[index + 2]) : 0;
        const unsigned int value = (first << 16) | (second << 8) | third;
        encoded.push_back(alphabet[(value >> 18) & 63]);
        encoded.push_back(alphabet[(value >> 12) & 63]);
        encoded.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        encoded.push_back(index + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return encoded;
}

bool ReadAttachmentBase64(const std::wstring& path, std::string& encoded, std::wstring& mime_type) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return false;
    }
    ULARGE_INTEGER size{};
    size.LowPart = attributes.nFileSizeLow;
    size.HighPart = attributes.nFileSizeHigh;
    if (size.QuadPart == 0 || size.QuadPart > 10ull * 1024ull * 1024ull) {
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const std::wstring extension = FileExtension(path);
    mime_type = extension == L".jpg" || extension == L".jpeg" ? L"image/jpeg"
               : extension == L".bmp" ? L"image/bmp"
                                      : L"image/png";
    encoded = Base64Encode(bytes);
    return !encoded.empty();
}

HBITMAP CaptureScreenBitmap(int& width, int& height) {
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    HDC screen = GetDC(nullptr);
    HDC memory = screen == nullptr ? nullptr : CreateCompatibleDC(screen);
    HBITMAP bitmap = screen == nullptr || memory == nullptr ? nullptr : CreateCompatibleBitmap(screen, width, height);
    if (bitmap != nullptr) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        BitBlt(memory, 0, 0, width, height, screen, left, top, SRCCOPY | CAPTUREBLT);
        SelectObject(memory, previous);
    }
    if (memory != nullptr) DeleteDC(memory);
    if (screen != nullptr) ReleaseDC(nullptr, screen);
    return bitmap;
}

struct ScreenshotSelectorState {
    HBITMAP bitmap = nullptr;
    int width = 0;
    int height = 0;
    POINT start{};
    RECT selection{};
    bool dragging = false;
    bool cancelled = false;
};

RECT NormalizeRect(POINT first, POINT second) {
    return RECT{std::min(first.x, second.x), std::min(first.y, second.y),
                std::max(first.x, second.x), std::max(first.y, second.y)};
}

LRESULT CALLBACK ScreenshotSelectorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ScreenshotSelectorState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<ScreenshotSelectorState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    switch (message) {
    case WM_LBUTTONDOWN:
        state->start = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        state->selection = RECT{state->start.x, state->start.y, state->start.x, state->start.y};
        state->dragging = true;
        SetCapture(window);
        return 0;
    case WM_MOUSEMOVE:
        if (state->dragging) {
            const POINT current{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            state->selection = NormalizeRect(state->start, current);
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (state->dragging) {
            const POINT current{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            state->selection = NormalizeRect(state->start, current);
            state->dragging = false;
            ReleaseCapture();
            if (state->selection.right - state->selection.left < 4 ||
                state->selection.bottom - state->selection.top < 4) {
                state->cancelled = true;
            }
            DestroyWindow(window);
        }
        return 0;
    case WM_RBUTTONDOWN:
    case WM_KEYDOWN:
        if (message == WM_RBUTTONDOWN || w_param == VK_ESCAPE) {
            state->cancelled = true;
            state->dragging = false;
            ReleaseCapture();
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        HDC source = CreateCompatibleDC(dc);
        HGDIOBJ previous = SelectObject(source, state->bitmap);
        BitBlt(dc, 0, 0, state->width, state->height, source, 0, 0, SRCCOPY);
        SelectObject(source, previous);
        DeleteDC(source);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT banner{12, 12, 330, 36};
        HBRUSH banner_brush = CreateSolidBrush(RGB(10, 12, 14));
        FillRect(dc, &banner, banner_brush);
        DeleteObject(banner_brush);
        TextOutW(dc, 18, 17, L"Drag to select  |  Esc cancels", 28);
        if (state->dragging || state->selection.right > state->selection.left) {
            HPEN pen = CreatePen(PS_SOLID, 2, kAccent);
            HGDIOBJ previous_pen = SelectObject(dc, pen);
            HGDIOBJ previous_brush = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(dc, state->selection.left, state->selection.top,
                      state->selection.right, state->selection.bottom);
            SelectObject(dc, previous_brush);
            SelectObject(dc, previous_pen);
            DeleteObject(pen);
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        state->dragging = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

HBITMAP CaptureScreenRegion(HWND owner) {
    int width = 0;
    int height = 0;
    HBITMAP screen_bitmap = CaptureScreenBitmap(width, height);
    if (screen_bitmap == nullptr) {
        return nullptr;
    }
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = ScreenshotSelectorProc;
        window_class.lpszClassName = kScreenshotSelectorClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_CROSS);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    ScreenshotSelectorState state{screen_bitmap, width, height};
    const int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    HWND selector = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kScreenshotSelectorClassName,
        L"Select screenshot region",
        WS_POPUP,
        left, top, width, height,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (selector == nullptr) {
        DeleteObject(screen_bitmap);
        return nullptr;
    }
    ShowWindow(selector, SW_SHOW);
    UpdateWindow(selector);
    SetForegroundWindow(selector);
    MSG message{};
    while (IsWindow(selector) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    HBITMAP result = nullptr;
    const int selection_width = state.selection.right - state.selection.left;
    const int selection_height = state.selection.bottom - state.selection.top;
    if (!state.cancelled && selection_width >= 4 && selection_height >= 4) {
        HDC screen = GetDC(nullptr);
        HDC source = CreateCompatibleDC(screen);
        HDC target = CreateCompatibleDC(screen);
        HBITMAP crop = screen == nullptr
                           ? nullptr
                           : CreateCompatibleBitmap(screen, selection_width, selection_height);
        if (screen != nullptr) {
            ReleaseDC(nullptr, screen);
        }
        if (source != nullptr && target != nullptr && crop != nullptr) {
            HGDIOBJ old_source = SelectObject(source, screen_bitmap);
            HGDIOBJ old_target = SelectObject(target, crop);
            BitBlt(target, 0, 0, selection_width, selection_height, source,
                   state.selection.left, state.selection.top, SRCCOPY);
            SelectObject(source, old_source);
            SelectObject(target, old_target);
            result = crop;
        } else if (crop != nullptr) {
            DeleteObject(crop);
        }
        if (source != nullptr) DeleteDC(source);
        if (target != nullptr) DeleteDC(target);
    }
    DeleteObject(screen_bitmap);
    return result;
}

std::wstring CaptureChatScreenshot(HWND owner) {
    HBITMAP bitmap = CaptureScreenRegion(owner);
    if (bitmap == nullptr) {
        return {};
    }
    std::wstring id = NewId();
    id.erase(std::remove(id.begin(), id.end(), L'{'), id.end());
    id.erase(std::remove(id.begin(), id.end(), L'}'), id.end());
    const std::wstring path = ChatAttachmentDirectory() + L"\\chat-" + id + L".png";
    const bool saved = SaveBitmapAsPng(bitmap, path);
    DeleteObject(bitmap);
    return saved ? path : L"";
}

std::wstring ChooseChatImage(HWND owner) {
    wchar_t path[4096]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.bmp\0All files\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&dialog) == FALSE) {
        return {};
    }
    return StoreChatAttachment(path);
}

bool GenerateGeminiResponse(const std::vector<ChatMessage>& messages,
                            const std::wstring& model,
                            const std::wstring& api_key,
                            std::wstring& response,
                            std::wstring& error) {
    std::string body = "{\"contents\":[";
    bool first_message = true;
    for (const ChatMessage& message : messages) {
        if (message.is_error || (message.role != L"user" && message.role != L"assistant")) {
            continue;
        }
        if (!first_message) {
            body += ',';
        }
        first_message = false;
        body += "{\"role\":\"";
        body += message.role == L"assistant" ? "model" : "user";
        body += "\",\"parts\":[{\"text\":\"";
        body += JsonEscape(message.body);
        body += "\"}";
        if (!message.attachment_path.empty()) {
            std::string image_base64;
            std::wstring mime_type;
            if (!ReadAttachmentBase64(message.attachment_path, image_base64, mime_type)) {
                error = L"Could not read the attached image.";
                return false;
            }
            body += ", {\"inline_data\":{\"mime_type\":\"";
            body += JsonEscape(mime_type);
            body += "\",\"data\":\"";
            body += image_base64;
            body += "\"}}";
        }
        body += "]}";
    }
    body += "]}";
    if (first_message) {
        error = L"Write a message before sending.";
        return false;
    }

    const std::wstring path = L"/v1beta/models/" +
                              WideFromUtf8(UrlEncodePathSegment(model).c_str()) +
                              L":generateContent";
    const std::string body_utf8 = body;
    const std::wstring headers = L"Content-Type: application/json\r\nx-goog-api-key: " + api_key + L"\r\n";

    HINTERNET session = WinHttpOpen(L"TwoSemi/0.1",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
    if (session == nullptr) {
        error = L"Could not start the Windows HTTP client.";
        return false;
    }
    WinHttpSetTimeouts(session, 10000, 10000, 10000, 90000);
    HINTERNET connection = WinHttpConnect(session, L"generativelanguage.googleapis.com",
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        error = L"Could not connect to Gemini.";
        WinHttpCloseHandle(session);
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(connection, L"POST", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        error = L"Could not create the Gemini request.";
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    const bool sent = WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1L),
                                         const_cast<char*>(body_utf8.data()),
                                         static_cast<DWORD>(body_utf8.size()),
                                         static_cast<DWORD>(body_utf8.size()), 0) != FALSE &&
                      WinHttpReceiveResponse(request, nullptr) != FALSE;
    if (!sent) {
        error = L"Gemini request failed (Windows error " + std::to_wstring(GetLastError()) + L").";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(request,
                         WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                         WINHTTP_HEADER_NAME_BY_INDEX,
                         &status_code,
                         &status_size,
                         WINHTTP_NO_HEADER_INDEX);

    std::string response_json;
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) != FALSE && available > 0) {
        std::string buffer(static_cast<size_t>(available), '\0');
        DWORD read = 0;
        if (WinHttpReadData(request, buffer.data(), available, &read) == FALSE) {
            break;
        }
        response_json.append(buffer.data(), read);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (status_code < 200 || status_code >= 300) {
        const auto message = ReadJsonField(response_json, "message");
        error = message.has_value()
                    ? L"Gemini request failed: " + WideFromUtf8(message->c_str(), static_cast<int>(message->size()))
                    : L"Gemini request failed with HTTP " + std::to_wstring(status_code) + L".";
        return false;
    }

    const auto text = ReadJsonField(response_json, "text");
    if (!text.has_value() || text->empty()) {
        error = L"Gemini returned an empty response.";
        return false;
    }
    response = WideFromUtf8(text->c_str(), static_cast<int>(text->size()));
    if (response.empty()) {
        error = L"Gemini returned text that TwoSemi could not decode.";
        return false;
    }
    return true;
}

constexpr wchar_t kChatStateProperty[] = L"TwoSemi.Chat.State";
constexpr wchar_t kChatOriginalProperty[] = L"TwoSemi.Chat.Original";

struct ChatDialogState {
    Database* database = nullptr;
    ChatThread thread;
    std::wstring initial_text;
    HWND window = nullptr;
    HWND transcript = nullptr;
    HWND input = nullptr;
    HWND send = nullptr;
    HWND status = nullptr;
    HWND screenshot = nullptr;
    HWND upload = nullptr;
    HWND clear_attachment = nullptr;
    HWND attachment_preview = nullptr;
    HBITMAP attachment_thumbnail = nullptr;
    std::wstring attachment_path;
    HFONT font = nullptr;
    bool busy = false;
    std::mutex result_mutex;
    bool result_success = false;
    std::wstring result_body;
    std::thread worker;
};

void UpdateChatAttachmentUi(ChatDialogState& state) {
    if (state.attachment_preview != nullptr) {
        SendMessageW(state.attachment_preview, STM_SETIMAGE, IMAGE_BITMAP, 0);
    }
    if (state.attachment_thumbnail != nullptr) {
        DeleteObject(state.attachment_thumbnail);
        state.attachment_thumbnail = nullptr;
    }

    if (state.attachment_path.empty()) {
        ShowWindow(state.attachment_preview, SW_HIDE);
        ShowWindow(state.clear_attachment, SW_HIDE);
        EnableWindow(state.clear_attachment, FALSE);
        SetWindowTextW(state.status, L"Ctrl+Enter sends");
        return;
    }

    state.attachment_thumbnail = CreateImageThumbnail(state.attachment_path, 30);
    if (state.attachment_thumbnail != nullptr && state.attachment_preview != nullptr) {
        SendMessageW(state.attachment_preview, STM_SETIMAGE, IMAGE_BITMAP,
                     reinterpret_cast<LPARAM>(state.attachment_thumbnail));
        ShowWindow(state.attachment_preview, SW_SHOW);
    }
    ShowWindow(state.clear_attachment, SW_SHOW);
    EnableWindow(state.clear_attachment, TRUE);
    SetWindowTextW(state.status, L"Ctrl+Enter sends");
}

void AttachChatImage(ChatDialogState& state, const std::wstring& path) {
    if (path.empty()) {
        return;
    }
    state.attachment_path = path;
    UpdateChatAttachmentUi(state);
    SetFocus(state.input);
}

void SetChatButtonsEnabled(ChatDialogState& state, bool enabled) {
    EnableWindow(state.send, enabled);
    EnableWindow(state.screenshot, enabled);
    EnableWindow(state.upload, enabled);
    EnableWindow(state.clear_attachment, enabled && !state.attachment_path.empty());
}

void TakeChatScreenshot(ChatDialogState& state) {
    ShowWindow(state.window, SW_HIDE);
    Sleep(120);
    const std::wstring path = CaptureChatScreenshot(state.window);
    ShowWindow(state.window, SW_SHOW);
    UpdateWindow(state.window);
    ActivateAndFocus(state.window, state.input);
    if (path.empty()) {
        SetWindowTextW(state.status, L"Screenshot cancelled | Ctrl+Enter sends");
        return;
    }
    AttachChatImage(state, path);
}

void AppendRich(HWND rich, const std::wstring& text, COLORREF color, bool bold, int size_delta = 0) {
    const int length = GetWindowTextLengthW(rich);
    SendMessageW(rich, EM_SETSEL, length, length);
    CHARFORMAT2W fmt{};
    fmt.cbSize = sizeof(fmt);
    fmt.dwMask = CFM_COLOR | CFM_BOLD;
    fmt.crTextColor = color;
    fmt.dwEffects = bold ? CFE_BOLD : 0;
    if (size_delta != 0) {
        fmt.dwMask |= CFM_SIZE;
        fmt.yHeight = (10 + size_delta) * 20;
    }
    SendMessageW(rich, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&fmt));
    SendMessageW(rich, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text.c_str()));
}

void AppendMarkdown(HWND rich, const std::wstring& body, COLORREF text_color) {
    size_t pos = 0;
    while (pos < body.size()) {
        size_t line_end = body.find(L'\n', pos);
        if (line_end == std::wstring::npos) line_end = body.size();
        std::wstring line = body.substr(pos, line_end - pos);
        pos = line_end + 1;

        if (!line.empty() && line.back() == L'\r') line.pop_back();

        if (line.starts_with(L"### ")) {
            AppendRich(rich, line.substr(4) + L"\r\n", kAccent, true, 1);
            continue;
        }
        if (line.starts_with(L"## ")) {
            AppendRich(rich, line.substr(3) + L"\r\n", kAccent, true, 2);
            continue;
        }
        if (line.starts_with(L"# ")) {
            AppendRich(rich, line.substr(2) + L"\r\n", kAccent, true, 4);
            continue;
        }

        size_t i = 0;
        while (i < line.size()) {
            size_t bold_start = line.find(L"**", i);
            if (bold_start == std::wstring::npos) {
                AppendRich(rich, line.substr(i), text_color, false);
                break;
            }
            if (bold_start > i) {
                AppendRich(rich, line.substr(i, bold_start - i), text_color, false);
            }
            size_t bold_end = line.find(L"**", bold_start + 2);
            if (bold_end == std::wstring::npos) {
                AppendRich(rich, line.substr(bold_start), text_color, false);
                break;
            }
            AppendRich(rich, line.substr(bold_start + 2, bold_end - bold_start - 2), text_color, true);
            i = bold_end + 2;
        }
        AppendRich(rich, L"\r\n", text_color, false);
    }
}

void RenderChatTranscript(ChatDialogState& state) {
    SendMessageW(state.transcript, WM_SETREDRAW, FALSE, 0);
    SetWindowTextW(state.transcript, L"");
    for (const ChatMessage& message : state.thread.messages) {
        const bool is_user = message.role == L"user";
        const bool is_error = message.is_error;
        const wchar_t* label = is_error ? L"twosemi" : is_user ? L"you" : L"gemini";
        COLORREF label_color = is_error ? RGB(220, 80, 80) : is_user ? kAccent : kMutedText;
        AppendRich(state.transcript, label, label_color, true);
        AppendRich(state.transcript, L"\r\n", kText, false);
        if (is_user) {
            if (!message.body.empty()) {
                AppendRich(state.transcript, message.body, kText, false);
            }
            if (!message.attachment_path.empty()) {
                AppendRich(state.transcript, L"[image attached]", kMutedText, false);
            }
            AppendRich(state.transcript, L"\r\n\r\n", kText, false);
        } else {
            AppendMarkdown(state.transcript, message.body, kText);
            AppendRich(state.transcript, L"\r\n", kText, false);
        }
    }
    SendMessageW(state.transcript, WM_SETREDRAW, TRUE, 0);
    const int length = GetWindowTextLengthW(state.transcript);
    SendMessageW(state.transcript, EM_SETSEL, length, length);
    SendMessageW(state.transcript, EM_SCROLLCARET, 0, 0);
    InvalidateRect(state.transcript, nullptr, TRUE);
}

void AppendChatResult(ChatDialogState& state, bool success, const std::wstring& body) {
    const std::wstring now = NowUtcIso();
    state.thread.messages.push_back(ChatMessage{
        NewId(),
        state.thread.id,
        success ? L"assistant" : L"error",
        body,
        now,
        !success,
    });
    state.thread.updated_at_utc = now;
    state.database->SaveChatThread(state.thread);
    RenderChatTranscript(state);
}

void SendChatMessage(ChatDialogState& state) {
    if (state.busy) {
        return;
    }
    const std::wstring text = Trim(WindowText(state.input));
    if (text.empty() && state.attachment_path.empty()) {
        return;
    }

    std::wstring model = EnvironmentValue(L"TWOSEMI_GEMINI_MODEL");
    std::wstring api_key = EnvironmentValue(L"TWOSEMI_GEMINI_API_KEY");
    const auto profile = state.database->LoadAiModel();
    if (profile.has_value()) {
        if (model.empty()) {
            model = profile->model_id;
        }
        if (api_key.empty() && !profile->encrypted_api_key.empty()) {
            bool decrypted = false;
            api_key = UnprotectSecret(profile->encrypted_api_key, decrypted);
            if (!decrypted) {
                api_key.clear();
            }
        }
    }
    if (model.empty()) {
        model = L"gemini-3.6-flash";
    }

    const std::wstring now = NowUtcIso();
    const std::wstring attachment_path = state.attachment_path;
    state.thread.messages.push_back(ChatMessage{
        NewId(), state.thread.id, L"user", text, now, false, attachment_path});
    if (state.thread.messages.size() == 1) {
        state.thread.title = text.empty() ? L"Image message" : MakeChatTitle(text);
        InvalidateRect(state.window, nullptr, FALSE);
    }
    state.thread.updated_at_utc = now;
    if (!state.database->SaveChatThread(state.thread)) {
        state.thread.messages.pop_back();
        SetWindowTextW(state.status, state.database->LastError().c_str());
        return;
    }
    SetWindowTextW(state.input, L"");
    state.attachment_path.clear();
    UpdateChatAttachmentUi(state);
    RenderChatTranscript(state);

    if (api_key.empty()) {
        AppendChatResult(state, false,
                         L"Gemini is not configured. Add a Gemini model in the previous TwoSemi settings "
                         L"database or set TWOSEMI_GEMINI_API_KEY.");
        return;
    }

    state.busy = true;
    SetChatButtonsEnabled(state, false);
    SetWindowTextW(state.status, L"Thinking...");
    const std::vector<ChatMessage> messages = state.thread.messages;
    state.worker = std::thread([&state, messages, model, api_key] {
        std::wstring response;
        std::wstring error;
        const bool success = GenerateGeminiResponse(messages, model, api_key, response, error);
        {
            std::lock_guard<std::mutex> lock(state.result_mutex);
            state.result_success = success;
            state.result_body = success ? response : error;
        }
        PostMessageW(state.window, kChatResponseMessage, 0, 0);
    });
}

LRESULT CALLBACK ChatEditProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ChatDialogState*>(GetPropW(window, kChatStateProperty));
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kChatOriginalProperty));
    if (state != nullptr && message == WM_KEYDOWN) {
        if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            SendMessageW(window, EM_SETSEL, 0, -1);
            return 0;
        }
        if (w_param == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            SendChatMessage(*state);
            return 0;
        }
        if (w_param == VK_ESCAPE) {
            SendMessageW(state->window, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK ChatDialogProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ChatDialogState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<ChatDialogState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE: {
        state->font = UiFont(10);
        LoadLibraryW(L"Msftedit.dll");
        state->transcript = CreateWindowExW(
            0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
            0, 30, kChatWidth, kChatHeight - 120, window,
            reinterpret_cast<HMENU>(301), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->transcript, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(kBackground));
        state->input = CreateWindowExW(
            0, L"EDIT", state->initial_text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
            0, kChatHeight - 90, kChatWidth - 64, 60, window,
            reinterpret_cast<HMENU>(302), GetModuleHandleW(nullptr), nullptr);
        state->send = CreateWindowW(
            L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            kChatWidth - 64, kChatHeight - 90, 64, 60, window,
            reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
        state->screenshot = CreateWindowW(
            L"BUTTON", L"Screenshot", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            4, kChatHeight - 150, 104, 30, window,
            reinterpret_cast<HMENU>(kChatScreenshotCommand), GetModuleHandleW(nullptr), nullptr);
        state->upload = CreateWindowW(
            L"BUTTON", L"Upload", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            112, kChatHeight - 150, 84, 30, window,
            reinterpret_cast<HMENU>(kChatUploadCommand), GetModuleHandleW(nullptr), nullptr);
        state->clear_attachment = CreateWindowW(
            L"BUTTON", L"X", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            200, kChatHeight - 150, 30, 30, window,
            reinterpret_cast<HMENU>(kChatClearAttachmentCommand), GetModuleHandleW(nullptr), nullptr);
        state->attachment_preview = CreateWindowW(
            L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            280, kChatHeight - 150, 30, 30, window,
            reinterpret_cast<HMENU>(kChatAttachmentPreview), GetModuleHandleW(nullptr), nullptr);
        state->status = CreateWindowW(
            L"STATIC", L"Ctrl+Enter sends", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            318, kChatHeight - 150, kChatWidth - 322, 30, window,
            reinterpret_cast<HMENU>(303), GetModuleHandleW(nullptr), nullptr);
        for (HWND control : {state->transcript, state->input, state->send, state->screenshot,
                             state->upload, state->clear_attachment, state->attachment_preview,
                             state->status}) {
            SetFont(control, state->font);
        }
        SendMessageW(state->input, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SendMessageW(state->transcript, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));
        SetPropW(state->input, kChatStateProperty, reinterpret_cast<HANDLE>(state));
        SetPropW(state->input, kChatOriginalProperty,
                 reinterpret_cast<HANDLE>(SetWindowLongPtrW(
                     state->input, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ChatEditProc))));
        UpdateChatAttachmentUi(*state);
        RenderChatTranscript(*state);
        SetFocus(state->input);
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(l_param);
        const int height = HIWORD(l_param);
        const int input_h = 60;
        const int action_h = 30;
        const int gap = 4;
        const int send_w = 64;
        const int input_y = std::max(60, height - input_h - gap);
        const int actions_y = std::max(30, input_y - gap - action_h);
        MoveWindow(state->transcript, 0, 30, width,
                   std::max(60, actions_y - 30 - gap), TRUE);
        MoveWindow(state->screenshot, 4, actions_y, 104, action_h, TRUE);
        MoveWindow(state->upload, 112, actions_y, 84, action_h, TRUE);
        MoveWindow(state->clear_attachment, 200, actions_y, 30, action_h, TRUE);
        MoveWindow(state->attachment_preview, 238, actions_y, 30, action_h, TRUE);
        MoveWindow(state->status, 276, actions_y, std::max(60, width - 280), action_h, TRUE);
        MoveWindow(state->input, 0, input_y,
                   std::max(60, width - send_w), input_h, TRUE);
        MoveWindow(state->send, std::max(0, width - send_w), input_y,
                   send_w, input_h, TRUE);
        return 0;
    }
    case WM_COMMAND: {
        if (HIWORD(w_param) == BN_CLICKED) {
            switch (LOWORD(w_param)) {
            case kChatScreenshotCommand:
                TakeChatScreenshot(*state);
                return 0;
            case kChatUploadCommand:
                AttachChatImage(*state, ChooseChatImage(state->window));
                return 0;
            case kChatClearAttachmentCommand:
                state->attachment_path.clear();
                UpdateChatAttachmentUi(*state);
                SetFocus(state->input);
                return 0;
            default:
                break;
            }
        }
        if (LOWORD(w_param) == IDOK && HIWORD(w_param) == BN_CLICKED) {
            SendChatMessage(*state);
            return 0;
        }
        break;
    }
    case kChatResponseMessage: {
        if (state->worker.joinable()) {
            state->worker.join();
        }
        std::wstring body;
        bool success = false;
        {
            std::lock_guard<std::mutex> lock(state->result_mutex);
            success = state->result_success;
            body = state->result_body;
        }
        state->busy = false;
        SetChatButtonsEnabled(*state, true);
        UpdateChatAttachmentUi(*state);
        AppendChatResult(*state, success, body);
        SetFocus(state->input);
        return 0;
    }
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kMutedText);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, state->font));
        const std::wstring title = L"twosemi :: chat // " + state->thread.title + L"  |  Esc close";
        TextOutW(dc, 4, 5, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetTextColor(dc, kMutedText);
        SetBkColor(dc, kBackground);
        static HBRUSH bg_brush = CreateSolidBrush(kBackground);
        return reinterpret_cast<LRESULT>(bg_brush);
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        HWND control = reinterpret_cast<HWND>(l_param);
        const bool is_transcript = control == state->transcript;
        SetTextColor(dc, kText);
        SetBkColor(dc, is_transcript ? kBackground : kInputBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(is_transcript ? background_brush : input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool is_send = draw->CtlID == IDOK;
        const bool is_clear = draw->CtlID == kChatClearAttachmentCommand;
        const wchar_t* label = is_send ? L"Send"
                              : draw->CtlID == kChatScreenshotCommand ? L"Screenshot"
                              : draw->CtlID == kChatUploadCommand ? L"Upload"
                              : is_clear ? L"X" : L"Clear";
        const bool enabled = (draw->itemState & ODS_DISABLED) == 0;
        HBRUSH fill = CreateSolidBrush(is_send ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, enabled
                                  ? is_clear ? RGB(230, 90, 90) : (is_send ? kAccent : kText)
                                  : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_3DSHADOW));
        DrawTextW(draw->hDC, label, -1, &draw->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_CLOSE:
        if (state->busy) {
            SetWindowTextW(state->status, L"Wait for the response before closing");
            return 0;
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        RemovePropW(state->input, kChatStateProperty);
        RemovePropW(state->input, kChatOriginalProperty);
        if (state->attachment_thumbnail != nullptr) {
            DeleteObject(state->attachment_thumbnail);
            state->attachment_thumbnail = nullptr;
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool RunChatDialog(HWND owner, Database& database, ChatThread thread, const std::wstring& initial_text) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = ChatDialogProc;
        window_class.lpszClassName = kChatDialogClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    ChatDialogState state;
    state.database = &database;
    state.thread = std::move(thread);
    state.initial_text = initial_text;
    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kChatDialogClassName,
        L"TwoSemi chat",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kChatWidth,
        kChatHeight,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (dialog == nullptr) {
        return false;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, GetDlgItem(dialog, 302));

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (state.worker.joinable()) {
        state.worker.join();
    }
    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(state.font);
    return true;
}

struct ModelEditorState {
    AiModelProfile profile;
    bool accepted = false;
    bool done = false;
    HWND window = nullptr;
    HWND provider_combo = nullptr;
    HWND name_edit = nullptr;
    HWND model_edit = nullptr;
    HWND key_edit = nullptr;
    HWND enabled_check = nullptr;
    HWND default_check = nullptr;
    HFONT font = nullptr;
};

LRESULT CALLBACK ModelEditorProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ModelEditorState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<ModelEditorState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE: {
        state->font = UiFont(10);
        HWND provider_label = CreateWindowW(L"STATIC", L"Provider", WS_CHILD | WS_VISIBLE,
                                            18, 42, 120, 22, window, nullptr,
                                            GetModuleHandleW(nullptr), nullptr);
        state->provider_combo = CreateWindowExW(
            0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
            150, 38, 220, 150, window, reinterpret_cast<HMENU>(401),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->provider_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gemini"));
        SendMessageW(state->provider_combo, CB_SETCURSEL, 0, 0);

        HWND name_label = CreateWindowW(L"STATIC", L"Display name", WS_CHILD | WS_VISIBLE,
                                        18, 86, 120, 22, window, nullptr,
                                        GetModuleHandleW(nullptr), nullptr);
        state->name_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->profile.name.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            150, 82, 420, 30, window, reinterpret_cast<HMENU>(402),
            GetModuleHandleW(nullptr), nullptr);

        HWND model_label = CreateWindowW(L"STATIC", L"Model ID", WS_CHILD | WS_VISIBLE,
                                         18, 130, 120, 22, window, nullptr,
                                         GetModuleHandleW(nullptr), nullptr);
        state->model_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", state->profile.model_id.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
            150, 126, 420, 30, window, reinterpret_cast<HMENU>(403),
            GetModuleHandleW(nullptr), nullptr);

        HWND key_label = CreateWindowW(L"STATIC", L"API key", WS_CHILD | WS_VISIBLE,
                                       18, 174, 120, 22, window, nullptr,
                                       GetModuleHandleW(nullptr), nullptr);
        state->key_edit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_PASSWORD | WS_TABSTOP,
            150, 170, 420, 30, window, reinterpret_cast<HMENU>(404),
            GetModuleHandleW(nullptr), nullptr);
        EnableSelectAll(state->name_edit);
        EnableSelectAll(state->model_edit);
        EnableSelectAll(state->key_edit);
        HWND key_hint = CreateWindowW(
            L"STATIC", L"Leave blank to keep the saved key", WS_CHILD | WS_VISIBLE,
            150, 202, 420, 20, window, nullptr, GetModuleHandleW(nullptr), nullptr);

        state->enabled_check = CreateWindowW(
            L"BUTTON", L"Enabled", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            18, 242, 120, 26, window, reinterpret_cast<HMENU>(405),
            GetModuleHandleW(nullptr), nullptr);
        state->default_check = CreateWindowW(
            L"BUTTON", L"Default model", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
            150, 242, 160, 26, window, reinterpret_cast<HMENU>(406),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->enabled_check, BM_SETCHECK, state->profile.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->default_check, BM_SETCHECK, state->profile.is_default ? BST_CHECKED : BST_UNCHECKED, 0);

        HWND save = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE |
                                                      BS_OWNERDRAW | BS_DEFPUSHBUTTON | WS_TABSTOP,
                                  390, 292, 84, 32, window, reinterpret_cast<HMENU>(IDOK),
                                  GetModuleHandleW(nullptr), nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                    486, 292, 84, 32, window, reinterpret_cast<HMENU>(IDCANCEL),
                                    GetModuleHandleW(nullptr), nullptr);

        for (HWND control : {provider_label, state->provider_combo, name_label, state->name_edit,
                             model_label, state->model_edit, key_label, state->key_edit, key_hint,
                             state->enabled_check, state->default_check, save, cancel}) {
            SetFont(control, state->font);
        }
        SetFocus(state->name_edit);
        SendMessageW(state->name_edit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w_param) == IDOK) {
            state->profile.provider = L"Gemini";
            state->profile.name = Trim(WindowText(state->name_edit));
            state->profile.model_id = Trim(WindowText(state->model_edit));
            state->profile.enabled = SendMessageW(state->enabled_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->profile.is_default = SendMessageW(state->default_check, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (state->profile.name.empty() || state->profile.model_id.empty()) {
                MessageBoxW(window, L"Enter a display name and model ID.", L"TwoSemi",
                            MB_OK | MB_ICONINFORMATION);
                SetFocus(state->name_edit);
                return 0;
            }
            const std::wstring entered_key = Trim(WindowText(state->key_edit));
            if (!entered_key.empty() && !ProtectSecret(entered_key, state->profile.encrypted_api_key)) {
                MessageBoxW(window, L"Windows could not protect this API key.", L"TwoSemi",
                            MB_OK | MB_ICONERROR);
                return 0;
            }
            if (state->profile.id.empty()) {
                state->profile.id = NewId();
            }
            if (state->profile.is_default) {
                state->profile.enabled = true;
            }
            state->accepted = true;
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(w_param) == IDCANCEL) {
            state->done = true;
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        state->done = true;
        DestroyWindow(window);
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kAccent);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, state->font));
        const std::wstring title = L"twosemi :: model  |  Esc cancel";
        TextOutW(dc, 18, 8, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        const bool static_control = message == WM_CTLCOLORSTATIC;
        SetTextColor(dc, static_control ? kMutedText : kText);
        SetBkColor(dc, static_control ? kBackground : kInputBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(static_control ? background_brush : input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool save_button = draw->CtlID == IDOK;
        HBRUSH fill = CreateSolidBrush(save_button ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(save_button ? kAccent : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, save_button ? kAccent : kText);
        DrawTextW(draw->hDC, save_button ? L"Save" : L"Cancel", -1,
                  &draw->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_DESTROY:
        state->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

bool RunModelEditor(HWND owner, AiModelProfile profile, AiModelProfile& result) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = ModelEditorProc;
        window_class.lpszClassName = kModelEditorClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    ModelEditorState state;
    state.profile = std::move(profile);
    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kModelEditorClassName,
        L"TwoSemi model",
        WS_POPUP | WS_BORDER,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        620,
        360,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (dialog == nullptr) {
        return false;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, GetDlgItem(dialog, 402));

    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(state.font);
    if (state.accepted) {
        result = std::move(state.profile);
    }
    return state.accepted;
}

constexpr wchar_t kModelManagerStateProperty[] = L"TwoSemi.ModelManager.State";
constexpr wchar_t kModelManagerOriginalProperty[] = L"TwoSemi.ModelManager.Original";

struct ModelManagerState {
    Database* database = nullptr;
    HWND window = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HFONT font = nullptr;
    std::vector<AiModelProfile> profiles;
};

std::wstring ModelProfileDisplay(const AiModelProfile& profile) {
    std::wstring display = profile.name.empty() ? L"Unnamed model" : profile.name;
    display += L"  |  " + profile.provider + L" / " + profile.model_id;
    display += profile.is_default ? L"  |  default" : L"";
    display += profile.enabled ? L"  |  enabled" : L"  |  disabled";
    display += profile.encrypted_api_key.empty() ? L"  |  no key" : L"  |  key saved";
    return display;
}

void RefreshModelManager(ModelManagerState& state) {
    state.profiles = state.database->LoadAiModels();
    SendMessageW(state.list, LB_RESETCONTENT, 0, 0);
    for (const AiModelProfile& profile : state.profiles) {
        const std::wstring display = ModelProfileDisplay(profile);
        SendMessageW(state.list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
    if (!state.profiles.empty()) {
        SendMessageW(state.list, LB_SETCURSEL, 0, 0);
    }
    SetWindowTextW(state.status, L"Enter edit  |  N add  |  D set default  |  Delete remove");
}

int SelectedModelIndex(ModelManagerState& state) {
    const LRESULT selected = SendMessageW(state.list, LB_GETCURSEL, 0, 0);
    return selected == LB_ERR ? -1 : static_cast<int>(selected);
}

void AddModelProfile(ModelManagerState& state) {
    AiModelProfile profile;
    profile.provider = L"Gemini";
    profile.name = L"Gemini Flash";
    profile.model_id = L"gemini-3.6-flash";
    profile.enabled = true;
    profile.is_default = state.profiles.empty();
    AiModelProfile edited;
    if (RunModelEditor(state.window, profile, edited)) {
        if (!state.database->SaveAiModel(edited)) {
            MessageBoxW(state.window, state.database->LastError().c_str(), L"TwoSemi",
                        MB_OK | MB_ICONERROR);
        }
        RefreshModelManager(state);
    }
}

void EditModelProfile(ModelManagerState& state) {
    const int index = SelectedModelIndex(state);
    if (index < 0 || static_cast<size_t>(index) >= state.profiles.size()) {
        return;
    }
    AiModelProfile edited;
    if (RunModelEditor(state.window, state.profiles[static_cast<size_t>(index)], edited)) {
        if (!state.database->SaveAiModel(edited)) {
            MessageBoxW(state.window, state.database->LastError().c_str(), L"TwoSemi",
                        MB_OK | MB_ICONERROR);
        }
        RefreshModelManager(state);
    }
}

void SetDefaultModelProfile(ModelManagerState& state) {
    const int index = SelectedModelIndex(state);
    if (index < 0 || static_cast<size_t>(index) >= state.profiles.size()) {
        return;
    }
    if (!state.profiles[static_cast<size_t>(index)].enabled) {
        MessageBoxW(state.window, L"Enable this model before making it the default.", L"TwoSemi",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (!state.database->SetDefaultAiModel(state.profiles[static_cast<size_t>(index)].id)) {
        MessageBoxW(state.window, state.database->LastError().c_str(), L"TwoSemi",
                    MB_OK | MB_ICONERROR);
    }
    RefreshModelManager(state);
}

void DeleteModelProfile(ModelManagerState& state) {
    const int index = SelectedModelIndex(state);
    if (index < 0 || static_cast<size_t>(index) >= state.profiles.size()) {
        return;
    }
    if (MessageBoxW(state.window, L"Delete this model profile?", L"TwoSemi",
                    MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    if (!state.database->DeleteAiModel(state.profiles[static_cast<size_t>(index)].id)) {
        MessageBoxW(state.window, state.database->LastError().c_str(), L"TwoSemi",
                    MB_OK | MB_ICONERROR);
    }
    RefreshModelManager(state);
}

LRESULT CALLBACK ModelManagerListProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ModelManagerState*>(GetPropW(window, kModelManagerStateProperty));
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kModelManagerOriginalProperty));
    if (state != nullptr && message == WM_KEYDOWN) {
        if (w_param == VK_RETURN || w_param == VK_F2) {
            EditModelProfile(*state);
            return 0;
        }
        if (w_param == 'N' || w_param == VK_INSERT) {
            AddModelProfile(*state);
            return 0;
        }
        if (w_param == 'D') {
            SetDefaultModelProfile(*state);
            return 0;
        }
        if (w_param == VK_DELETE) {
            DeleteModelProfile(*state);
            return 0;
        }
        if (w_param == VK_ESCAPE) {
            SendMessageW(state->window, WM_CLOSE, 0, 0);
            return 0;
        }
    }
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK ModelManagerProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* state = reinterpret_cast<ModelManagerState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        state = static_cast<ModelManagerState*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE: {
        state->font = UiFont(10);
        state->list = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_TABSTOP,
            18, 40, 684, 258, window, reinterpret_cast<HMENU>(501),
            GetModuleHandleW(nullptr), nullptr);
        state->status = CreateWindowW(
            L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            20, 310, 684, 22, window, reinterpret_cast<HMENU>(502),
            GetModuleHandleW(nullptr), nullptr);
        HWND add = CreateWindowW(L"BUTTON", L"Add", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                 18, 346, 82, 32, window, reinterpret_cast<HMENU>(503),
                                 GetModuleHandleW(nullptr), nullptr);
        HWND edit = CreateWindowW(L"BUTTON", L"Edit", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                  112, 346, 82, 32, window, reinterpret_cast<HMENU>(504),
                                  GetModuleHandleW(nullptr), nullptr);
        HWND make_default = CreateWindowW(L"BUTTON", L"Set default", WS_CHILD | WS_VISIBLE |
                                                           BS_OWNERDRAW | WS_TABSTOP,
                                          206, 346, 112, 32, window, reinterpret_cast<HMENU>(505),
                                          GetModuleHandleW(nullptr), nullptr);
        HWND remove = CreateWindowW(L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                    330, 346, 88, 32, window, reinterpret_cast<HMENU>(506),
                                    GetModuleHandleW(nullptr), nullptr);
        HWND close = CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                                   618, 346, 84, 32, window, reinterpret_cast<HMENU>(IDCANCEL),
                                   GetModuleHandleW(nullptr), nullptr);
        for (HWND control : {state->list, state->status, add, edit, make_default, remove, close}) {
            SetFont(control, state->font);
        }
        SetPropW(state->list, kModelManagerStateProperty, reinterpret_cast<HANDLE>(state));
        SetPropW(state->list, kModelManagerOriginalProperty,
                 reinterpret_cast<HANDLE>(SetWindowLongPtrW(
                     state->list, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ModelManagerListProc))));
        RefreshModelManager(*state);
        SetFocus(state->list);
        return 0;
    }
    case WM_COMMAND:
        if (HIWORD(w_param) == LBN_DBLCLK && LOWORD(w_param) == 501) {
            EditModelProfile(*state);
            return 0;
        }
        if (HIWORD(w_param) == BN_CLICKED) {
            switch (LOWORD(w_param)) {
            case 503: AddModelProfile(*state); return 0;
            case 504: EditModelProfile(*state); return 0;
            case 505: SetDefaultModelProfile(*state); return 0;
            case 506: DeleteModelProfile(*state); return 0;
            case IDCANCEL: DestroyWindow(window); return 0;
            default: break;
            }
        }
        break;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kAccent);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, state->font));
        const std::wstring title = L"twosemi :: models  |  Esc close";
        TextOutW(dc, 18, 8, title.c_str(), static_cast<int>(title.size()));
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        SetTextColor(dc, message == WM_CTLCOLORSTATIC ? kMutedText : kText);
        SetBkColor(dc, message == WM_CTLCOLORSTATIC ? kBackground : kInputBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(message == WM_CTLCOLORSTATIC ? background_brush : input_brush);
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw == nullptr) {
            break;
        }
        const bool accent = draw->CtlID == 503 || draw->CtlID == 505;
        HBRUSH fill = CreateSolidBrush(accent ? kSelection : kInputBackground);
        FillRect(draw->hDC, &draw->rcItem, fill);
        DeleteObject(fill);
        HBRUSH border = CreateSolidBrush(accent ? kAccent : kMutedText);
        FrameRect(draw->hDC, &draw->rcItem, border);
        DeleteObject(border);
        const wchar_t* label = draw->CtlID == 503 ? L"Add"
                                 : draw->CtlID == 504 ? L"Edit"
                                 : draw->CtlID == 505 ? L"Set default"
                                 : draw->CtlID == 506 ? L"Delete" : L"Close";
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, accent ? kAccent : kText);
        DrawTextW(draw->hDC, label, -1, &draw->rcItem,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return TRUE;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        RemovePropW(state->list, kModelManagerStateProperty);
        RemovePropW(state->list, kModelManagerOriginalProperty);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

void RunModelManager(HWND owner, Database& database) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = ModelManagerProc;
        window_class.lpszClassName = kModelManagerClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    ModelManagerState state;
    state.database = &database;
    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT,
        kModelManagerClassName,
        L"TwoSemi models",
        WS_POPUP | WS_BORDER,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        720,
        410,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);
    if (dialog == nullptr) {
        return;
    }

    ShowWindow(owner, SW_HIDE);
    CenterOver(dialog, owner);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    ActivateAndFocus(dialog, GetDlgItem(dialog, 501));
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    ShowWindow(owner, SW_SHOW);
    RestoreLauncherFocus(owner);
    DeleteObject(state.font);
}

class TwoSemiApp;

enum class LauncherItemKind {
    NewNote,
    NewReminder,
    StartFocus,
    FocusStatus,
    FocusPause,
    FocusStop,
    NewTodoGroup,
    ViewTodoGroups,
    NewStreak,
    NewChat,
    ViewNotes,
    ViewReminders,
    ViewStreaks,
    ViewChats,
    ConfigureModels,
    Note,
    Reminder,
    TodoGroup,
    Streak,
    ChatThread,
    NewTodoTask,
    TodoTask,
};

struct LauncherItem {
    LauncherItemKind kind;
    std::wstring title;
    std::wstring detail;
    size_t index = 0;
};

constexpr wchar_t kLauncherThisProperty[] = L"TwoSemi.Launcher.This";
constexpr wchar_t kLauncherOriginalProperty[] = L"TwoSemi.Launcher.Original";

class LauncherWindow {
public:
    explicit LauncherWindow(TwoSemiApp* app);
    ~LauncherWindow();

    HWND Window() const { return window_; }
    void Show(const std::wstring& captured_text);
    void Hide();
    bool IsVisible() const { return window_ != nullptr && IsWindowVisible(window_) != FALSE; }
    void Refresh();
    void OpenTodoGroup(const std::wstring& group_id);
    void OpenTodoGroups();
    void OpenNotes();
    void OpenReminders();
    void OpenStreaks();
    void OpenChats();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK EditProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    static LRESULT CALLBACK ListProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    void CreateControls();
    void MoveSelection(int delta);
    void UpdateHint();
    void ExecuteSelected();
    void HandleKey(WPARAM key);
    void GoBack();
    void EditSelected();
    void DeleteSelected();
    void Position();
    LauncherItem* SelectedItem();

    TwoSemiApp* app_ = nullptr;
    HWND window_ = nullptr;
    HWND search_ = nullptr;
    HWND results_ = nullptr;
    HWND hint_ = nullptr;
    HFONT regular_font_ = nullptr;
    HFONT bold_font_ = nullptr;
    HFONT small_font_ = nullptr;
    HFONT footer_font_ = nullptr;
    std::wstring captured_text_;
    std::vector<Note> notes_;
    std::vector<Reminder> reminders_;
    std::vector<TodoGroup> todo_groups_;
    std::vector<Streak> streaks_;
    std::vector<ChatThread> chat_threads_;
    std::vector<LauncherItem> items_;
    bool todo_group_view_ = false;
    bool todo_groups_view_ = false;
    bool note_view_ = false;
    bool reminder_view_ = false;
    bool streak_view_ = false;
    bool chat_view_ = false;
    size_t active_todo_group_ = 0;
    std::wstring view_title_ = L"twosemi :: launcher";
};

class TwoSemiApp {
public:
    TwoSemiApp()
        : database_(LocalDatabasePath()) {}

    ~TwoSemiApp() {
        if (focus_active_) {
            StopFocus();
        }
        if (host_ != nullptr) {
            KillTimer(host_, kReminderTimer);
        }
        RemoveTrayIcon();
        if (keyboard_hook_ != nullptr) {
            UnhookWindowsHookEx(keyboard_hook_);
            keyboard_hook_ = nullptr;
        }
        g_app_ = nullptr;
        if (host_ != nullptr) {
            DestroyWindow(host_);
            host_ = nullptr;
        }
    }

    bool Initialize() {
        g_app_ = this;
        RegisterHostClass();
        host_ = CreateWindowExW(0, kHostClassName, L"TwoSemi", 0,
                                0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), this);
        if (host_ == nullptr) {
            return false;
        }

        launcher_ = std::make_unique<LauncherWindow>(this);
        AddTrayIcon();
        SetTimer(host_, kReminderTimer, 1000, nullptr);
        keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc,
                                           GetModuleHandleW(nullptr), 0);
        if (keyboard_hook_ == nullptr) {
            MessageBoxW(nullptr, L"TwoSemi could not install its global keyboard hook.",
                        L"TwoSemi", MB_OK | MB_ICONWARNING);
        }
        return database_.IsOpen();
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

    HWND HostWindow() const { return host_; }

    void ShowLauncher(bool capture_selection) {
        if (launcher_ == nullptr) {
            return;
        }

        if (capture_selection) {
            target_window_ = GetForegroundWindow();
            if (target_window_ == host_ || target_window_ == launcher_->Window()) {
                target_window_ = nullptr;
            }
            captured_text_ = CaptureSelection(target_window_);
        } else {
            target_window_ = nullptr;
            captured_text_.clear();
        }

        launcher_->Show(captured_text_);
    }

    void HideLauncher() {
        if (launcher_ != nullptr) {
            launcher_->Hide();
        }
    }

    bool FocusActive() const { return focus_active_; }
    bool FocusPaused() const { return focus_paused_; }
    std::wstring FocusRemaining() const { return FormatFocusRemaining(focus_remaining_seconds_); }
    const std::vector<std::wstring>& FocusDomains() const { return focus_domains_; }

    void StartFocus();
    void ToggleFocusPause();
    void StopFocus();

    void CreateNote(const std::wstring& captured_text) {
        NoteDraft draft;
        draft.body = captured_text;
        if (!EditNote(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }

        Note note;
        note.id = NewId();
        note.kind = draft.kind;
        note.title = draft.title.empty()
                         ? (note.kind == Note::Kind::Secret ? L"Secret note" : MakeNoteTitle(draft.body))
                         : draft.title;
        note.body = draft.body;
        if (!database_.SaveNote(note)) {
            MessageBoxW(launcher_->Window(), database_.LastError().c_str(), L"TwoSemi", MB_OK | MB_ICONERROR);
        }
        launcher_->Refresh();
    }

    void EditNoteItem(const Note& existing) {
        if (existing.kind == Note::Kind::Secret && !existing.secret_available) {
            MessageBoxW(launcher_->Window(),
                        L"This secret note could not be unlocked for the current Windows user.",
                        L"TwoSemi", MB_OK | MB_ICONWARNING);
            return;
        }

        NoteDraft draft;
        draft.title = existing.title;
        draft.body = existing.body;
        draft.kind = existing.kind;
        if (!EditNote(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }

        Note note = existing;
        note.kind = draft.kind;
        note.title = draft.title.empty()
                         ? (note.kind == Note::Kind::Secret ? L"Secret note" : MakeNoteTitle(draft.body))
                         : draft.title;
        note.body = draft.body;
        if (!database_.SaveNote(note)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteNote(const Note& existing) {
        if (MessageBoxW(launcher_->Window(), L"Delete this note?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        if (!database_.DeleteNote(existing.id)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void CreateReminder(const std::wstring& captured_text) {
        QuickDraft draft{QuickEditorKind::Reminder, captured_text, DefaultReminderTime()};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        Reminder reminder{NewId(), draft.first, draft.second, NowUtcIso(), {}, {}};
        if (!database_.SaveReminder(reminder)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void EditReminder(const Reminder& existing) {
        QuickDraft draft{QuickEditorKind::Reminder, existing.text, FormatReminderTime(existing.due_at_utc)};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        Reminder reminder = existing;
        reminder.text = draft.first;
        reminder.due_at_utc = draft.second;
        reminder.snoozed_until_utc.clear();
        if (!database_.SaveReminder(reminder)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void ToggleReminder(const Reminder& existing) {
        Reminder reminder = existing;
        reminder.completed_at_utc = reminder.completed_at_utc.empty() ? NowUtcIso() : L"";
        if (!database_.SaveReminder(reminder)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void SnoozeReminder(const Reminder& existing) {
        Reminder reminder = existing;
        reminder.due_at_utc = UtcAfterMinutes(60);
        reminder.snoozed_until_utc = reminder.due_at_utc;
        if (!database_.SaveReminder(reminder)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteReminder(const Reminder& existing) {
        if (MessageBoxW(launcher_->Window(), L"Delete this reminder?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        if (!database_.DeleteReminder(existing.id)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void CreateStreak() {
        QuickDraft draft{QuickEditorKind::Streak, {}, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        const std::wstring now = NowUtcIso();
        Streak streak{NewId(), draft.first, draft.frequency, now, now, {}};
        if (!database_.SaveStreak(streak)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void EditStreak(const Streak& existing) {
        QuickDraft draft{QuickEditorKind::Streak, existing.title, {}};
        draft.frequency = existing.frequency;
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        Streak streak = existing;
        streak.title = draft.first;
        streak.frequency = draft.frequency;
        streak.updated_at_utc = NowUtcIso();
        if (!database_.SaveStreak(streak)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void ToggleStreak(const Streak& existing) {
        if (!database_.ToggleStreakCompletion(existing.id, TodayLocalDate())) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteStreak(const Streak& existing) {
        if (MessageBoxW(launcher_->Window(), L"Delete this streak and its history?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        if (!database_.DeleteStreak(existing.id)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void CreateChat(const std::wstring& captured_text) {
        const std::wstring now = NowUtcIso();
        ChatThread thread{NewId(), L"New chat", now, now, {}};
        if (!database_.SaveChatThread(thread)) {
            ShowDatabaseError();
            launcher_->Refresh();
            return;
        }
        RunChatDialog(launcher_->Window(), database_, std::move(thread), captured_text);
        launcher_->Refresh();
    }

    void OpenChatThread(const std::wstring& thread_id) {
        const auto threads = database_.LoadChatThreads();
        const auto thread = std::find_if(threads.begin(), threads.end(), [&](const ChatThread& candidate) {
            return candidate.id == thread_id;
        });
        if (thread == threads.end()) {
            launcher_->Refresh();
            return;
        }
        RunChatDialog(launcher_->Window(), database_, *thread, {});
        launcher_->Refresh();
    }

    void EditChatThread(const ChatThread& existing) {
        QuickDraft draft{QuickEditorKind::ChatThread, existing.title, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        ChatThread thread = existing;
        thread.title = draft.first;
        thread.updated_at_utc = NowUtcIso();
        if (!database_.SaveChatThread(thread)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteChatThread(const ChatThread& existing) {
        if (MessageBoxW(launcher_->Window(), L"Delete this chat and its messages?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        if (!database_.DeleteChatThread(existing.id)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void ConfigureModels() {
        RunModelManager(launcher_->Window(), database_);
        launcher_->Refresh();
    }

    void CreateTodoGroup() {
        QuickDraft draft{QuickEditorKind::TodoGroup, {}, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        TodoGroup group{NewId(), draft.first, NowUtcIso(), NowUtcIso(), {}};
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
            launcher_->Refresh();
            return;
        }
        launcher_->OpenTodoGroup(group.id);
    }

    void EditTodoGroup(const TodoGroup& existing) {
        QuickDraft draft{QuickEditorKind::TodoGroup, existing.title, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        TodoGroup group = existing;
        group.title = draft.first;
        group.updated_at_utc = NowUtcIso();
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void CreateTodoTask(const TodoGroup& existing) {
        QuickDraft draft{QuickEditorKind::TodoTask, {}, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        TodoGroup group = existing;
        const std::wstring now = NowUtcIso();
        group.items.push_back(TodoItem{NewId(), group.id, draft.first, false, now, now});
        group.updated_at_utc = now;
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void EditTodoTask(const TodoGroup& existing, const TodoItem& existing_item) {
        QuickDraft draft{QuickEditorKind::TodoTask, existing_item.text, {}};
        if (!EditQuick(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }
        TodoGroup group = existing;
        const auto item = std::find_if(group.items.begin(), group.items.end(), [&](const TodoItem& candidate) {
            return candidate.id == existing_item.id;
        });
        if (item == group.items.end()) {
            return;
        }
        item->text = draft.first;
        item->updated_at_utc = NowUtcIso();
        group.updated_at_utc = item->updated_at_utc;
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void ToggleTodoTask(const TodoGroup& existing, const TodoItem& existing_item) {
        TodoGroup group = existing;
        const auto item = std::find_if(group.items.begin(), group.items.end(), [&](const TodoItem& candidate) {
            return candidate.id == existing_item.id;
        });
        if (item == group.items.end()) {
            return;
        }
        item->completed = !item->completed;
        item->updated_at_utc = NowUtcIso();
        group.updated_at_utc = item->updated_at_utc;
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteTodoTask(const TodoGroup& existing, const TodoItem& existing_item) {
        if (MessageBoxW(launcher_->Window(), L"Delete this task?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        TodoGroup group = existing;
        group.items.erase(std::remove_if(group.items.begin(), group.items.end(), [&](const TodoItem& candidate) {
                              return candidate.id == existing_item.id;
                          }), group.items.end());
        group.updated_at_utc = NowUtcIso();
        if (!database_.SaveTodoGroup(group)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void DeleteTodoGroup(const TodoGroup& existing) {
        if (MessageBoxW(launcher_->Window(), L"Delete this todo group and its tasks?", L"TwoSemi",
                        MB_YESNO | MB_ICONQUESTION) != IDYES) {
            return;
        }
        if (!database_.DeleteTodoGroup(existing.id)) {
            ShowDatabaseError();
        }
        launcher_->Refresh();
    }

    void UseNote(const Note& note) {
        if (note.kind == Note::Kind::Secret && !note.secret_available) {
            MessageBoxW(launcher_->Window(),
                        L"This secret note could not be unlocked for the current Windows user.",
                        L"TwoSemi", MB_OK | MB_ICONWARNING);
            return;
        }
        const HWND target = target_window_;
        HideLauncher();
        if (target != nullptr && IsWindow(target)) {
            SetForegroundWindow(target);
            Sleep(40);
            SendUnicodeText(note.body);
        }
        target_window_ = nullptr;
    }

private:
    friend class LauncherWindow;

    void AdvanceFocusClock() {
        if (!focus_active_ || focus_paused_) {
            return;
        }
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG elapsed = (now - focus_last_tick_) / 1000;
        if (elapsed == 0) {
            return;
        }
        focus_last_tick_ += elapsed * 1000;
        focus_remaining_seconds_ = elapsed >= focus_remaining_seconds_
                                       ? 0
                                       : focus_remaining_seconds_ - elapsed;
    }

    void TickFocus() {
        if (!focus_active_) {
            return;
        }
        AdvanceFocusClock();
        if (focus_remaining_seconds_ == 0) {
            KillTimer(host_, kFocusTimer);
            const bool cleared = focus_domains_.empty() || RunElevatedFocusCommand(false, {});
            if (!cleared && launcher_ != nullptr && launcher_->Window() != nullptr) {
                MessageBoxW(launcher_->Window(),
                            L"The focus timer ended, but the website block could not be removed.",
                            L"TwoSemi", MB_OK | MB_ICONWARNING);
            }
            focus_active_ = false;
            focus_paused_ = false;
            focus_domains_.clear();
            UpdateTrayTip();
            if (launcher_ != nullptr && launcher_->IsVisible()) {
                launcher_->Refresh();
            }
            return;
        }
        UpdateTrayTip();
        if (launcher_ != nullptr && launcher_->IsVisible()) {
            launcher_->Refresh();
        }
    }

    void NotifyReminder(const Reminder& reminder) {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = host_;
        data.uID = 1;
        data.uFlags = NIF_INFO;
        wcsncpy_s(data.szInfoTitle, L"TwoSemi reminder", _TRUNCATE);
        const std::wstring text = reminder.text.empty() ? L"Reminder due" : reminder.text;
        wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
        data.dwInfoFlags = NIIF_INFO;
        data.uTimeout = 10000;
        Shell_NotifyIconW(NIM_MODIFY, &data);
        MessageBeep(MB_ICONEXCLAMATION);
    }

    void TickReminders() {
        const std::wstring now = NowUtcIso();
        const std::vector<Reminder> reminders = database_.LoadReminders();
        std::set<std::wstring> active_ids;
        for (const Reminder& reminder : reminders) {
            if (reminder.completed_at_utc.empty() && !reminder.due_at_utc.empty()) {
                active_ids.insert(reminder.id);
                if (reminder.due_at_utc <= now && notified_reminder_ids_.insert(reminder.id).second) {
                    NotifyReminder(reminder);
                } else if (reminder.due_at_utc > now) {
                    notified_reminder_ids_.erase(reminder.id);
                }
            }
        }
        for (auto iterator = notified_reminder_ids_.begin();
             iterator != notified_reminder_ids_.end();) {
            if (!active_ids.contains(*iterator)) {
                iterator = notified_reminder_ids_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    void UpdateTrayTip() {
        if (host_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = host_;
        data.uID = 1;
        data.uFlags = NIF_TIP;
        std::wstring tip = L"TwoSemi";
        if (focus_active_) {
            tip += focus_paused_ ? L" - Focus paused" : L" - Focus " + FocusRemaining();
        }
        wcscpy_s(data.szTip, tip.c_str());
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void ShowDatabaseError() {
        MessageBoxW(launcher_->Window(), database_.LastError().c_str(), L"TwoSemi", MB_OK | MB_ICONERROR);
    }

    static LRESULT CALLBACK HostWindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
        auto* app = reinterpret_cast<TwoSemiApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
            app = static_cast<TwoSemiApp*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        if (app != nullptr) {
            switch (message) {
            case kShowLauncherMessage:
                app->ShowLauncher(true);
                return 0;
            case WM_TIMER:
                if (w_param == kReplayActivationTimer) {
                    app->ReplayActivationKey();
                    return 0;
                }
                if (w_param == kFocusTimer) {
                    app->TickFocus();
                    return 0;
                }
                if (w_param == kReminderTimer) {
                    app->TickReminders();
                    return 0;
                }
                break;
            case kTrayMessage:
                app->HandleTrayMessage(l_param);
                return 0;
            case WM_COMMAND:
                if (LOWORD(w_param) == kTrayOpenCommand) {
                    app->ShowLauncher(false);
                    return 0;
                }
                if (LOWORD(w_param) == kTrayExitCommand) {
                    PostQuitMessage(0);
                    return 0;
                }
                break;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
            }
        }

        return DefWindowProcW(window, message, w_param, l_param);
    }

    static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM w_param, LPARAM l_param) {
        if (code < 0 || g_app_ == nullptr) {
            return CallNextHookEx(nullptr, code, w_param, l_param);
        }
        return g_app_->HandleKeyboard(code, w_param, l_param);
    }

    LRESULT HandleKeyboard(int code, WPARAM w_param, LPARAM l_param) {
        if (code < 0) {
            return CallNextHookEx(keyboard_hook_, code, w_param, l_param);
        }

        const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
        if ((keyboard->flags & LLKHF_INJECTED) != 0) {
            return CallNextHookEx(keyboard_hook_, code, w_param, l_param);
        }

        const bool key_down = w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN;
        const bool key_up = w_param == WM_KEYUP || w_param == WM_SYSKEYUP;
        const bool control_key = keyboard->vkCode == VK_CONTROL ||
                                 keyboard->vkCode == VK_LCONTROL ||
                                 keyboard->vkCode == VK_RCONTROL;
        if (key_down && control_key) {
            control_held_ = true;
        }

        if (key_down && control_held_ && keyboard->vkCode == VK_OEM_1) {
            const ULONGLONG now = GetTickCount64();
            if (activation_armed_ && now - first_activation_tick_ <= kActivationIntervalMs) {
                activation_armed_ = false;
                KillTimer(host_, kReplayActivationTimer);
                PostMessageW(host_, kShowLauncherMessage, 0, 0);
            } else {
                activation_armed_ = true;
                first_activation_tick_ = now;
                SetTimer(host_, kReplayActivationTimer, kActivationIntervalMs, nullptr);
            }
            consume_semicolon_keyup_ = true;
            return 1;
        }

        if (key_up && keyboard->vkCode == VK_OEM_1 && consume_semicolon_keyup_) {
            consume_semicolon_keyup_ = false;
            return 1;
        }
        if (key_up && control_key) {
            control_held_ = false;
        }
        return CallNextHookEx(keyboard_hook_, code, w_param, l_param);
    }

    void ReplayActivationKey() {
        if (!activation_armed_) {
            return;
        }
        activation_armed_ = false;
        KillTimer(host_, kReplayActivationTimer);

        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_OEM_1;
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }

    void RegisterHostClass() {
        static std::once_flag registered;
        std::call_once(registered, [] {
            WNDCLASSW window_class{};
            window_class.hInstance = GetModuleHandleW(nullptr);
            window_class.lpfnWndProc = HostWindowProc;
            window_class.lpszClassName = kHostClassName;
            SetAppWindowClassIcon(window_class);
            RegisterClassW(&window_class);
        });
    }

    void AddTrayIcon() {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = host_;
        data.uID = 1;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = g_app_icon_small != nullptr ? g_app_icon_small : LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(data.szTip, L"TwoSemi");
        Shell_NotifyIconW(NIM_ADD, &data);
    }

    void RemoveTrayIcon() {
        if (host_ == nullptr) {
            return;
        }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = host_;
        data.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    void HandleTrayMessage(LPARAM message) {
        if (message == WM_LBUTTONUP) {
            ShowLauncher(false);
            return;
        }
        if (message == NIN_BALLOONUSERCLICK) {
            ShowLauncher(false);
            if (launcher_ != nullptr) {
                launcher_->OpenReminders();
            }
            return;
        }
        if (message != WM_RBUTTONUP) {
            return;
        }

        POINT point{};
        GetCursorPos(&point);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kTrayOpenCommand, L"Open launcher");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTrayExitCommand, L"Exit TwoSemi");
        SetForegroundWindow(host_);
        TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, point.x, point.y, 0, host_, nullptr);
        DestroyMenu(menu);
        PostMessageW(host_, WM_NULL, 0, 0);
    }

    static std::wstring CaptureSelection(HWND target) {
        if (target == nullptr || !IsWindow(target)) {
            return {};
        }

        std::optional<std::wstring> previous_clipboard;
        if (OpenClipboard(nullptr)) {
            if (HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr) {
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)); text != nullptr) {
                    previous_clipboard = text;
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
        }

        SetForegroundWindow(target);
        INPUT copy_inputs[4]{};
        copy_inputs[0].type = INPUT_KEYBOARD;
        copy_inputs[0].ki.wVk = VK_CONTROL;
        copy_inputs[1] = copy_inputs[0];
        copy_inputs[1].ki.wVk = 'C';
        copy_inputs[2] = copy_inputs[1];
        copy_inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        copy_inputs[3] = copy_inputs[0];
        copy_inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, copy_inputs, sizeof(INPUT));
        Sleep(80);

        std::wstring selected;
        if (OpenClipboard(nullptr)) {
            if (HANDLE data = GetClipboardData(CF_UNICODETEXT); data != nullptr) {
                if (const auto* text = static_cast<const wchar_t*>(GlobalLock(data)); text != nullptr) {
                    selected = text;
                    GlobalUnlock(data);
                }
            }
            CloseClipboard();
        }

        if (previous_clipboard.has_value() && OpenClipboard(nullptr)) {
            EmptyClipboard();
            const size_t bytes = (previous_clipboard->size() + 1) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (memory != nullptr) {
                if (auto* destination = static_cast<wchar_t*>(GlobalLock(memory)); destination != nullptr) {
                    memcpy(destination, previous_clipboard->c_str(), bytes);
                    GlobalUnlock(memory);
                    SetClipboardData(CF_UNICODETEXT, memory);
                } else {
                    GlobalFree(memory);
                }
            }
            CloseClipboard();
        }
        return selected;
    }

    static void SendUnicodeText(const std::wstring& text) {
        if (text.empty()) {
            return;
        }

        std::vector<INPUT> inputs;
        inputs.reserve(text.size() * 2);
        for (wchar_t character : text) {
            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wScan = static_cast<WORD>(character);
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            inputs.push_back(down);
            INPUT up = down;
            up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            inputs.push_back(up);
        }
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    }

    HWND host_ = nullptr;
    std::unique_ptr<LauncherWindow> launcher_;
    Database database_;
    HHOOK keyboard_hook_ = nullptr;
    HWND target_window_ = nullptr;
    std::wstring captured_text_;
    bool focus_active_ = false;
    bool focus_paused_ = false;
    ULONGLONG focus_remaining_seconds_ = 0;
    ULONGLONG focus_last_tick_ = 0;
    std::vector<std::wstring> focus_domains_;
    std::set<std::wstring> notified_reminder_ids_;
    bool control_held_ = false;
    bool activation_armed_ = false;
    bool consume_semicolon_keyup_ = false;
    ULONGLONG first_activation_tick_ = 0;

    static inline TwoSemiApp* g_app_ = nullptr;
};

void TwoSemiApp::StartFocus() {
    if (focus_active_) {
        return;
    }

    const FocusSettings settings = database_.LoadFocusSettings();
    FocusDraft draft;
    draft.minutes = settings.minutes;
    draft.domains = JoinLines(settings.domains);
    if (!EditFocus(launcher_->Window(), draft)) {
        launcher_->Refresh();
        return;
    }

    FocusSettings next_settings;
    next_settings.minutes = draft.minutes;
    next_settings.domains = NormalizeFocusDomains(draft.domains);
    if (!next_settings.domains.empty() && !RunElevatedFocusCommand(true, next_settings.domains)) {
        MessageBoxW(launcher_->Window(),
                    L"TwoSemi could not enable website blocking. Focus was not started.",
                    L"TwoSemi", MB_OK | MB_ICONWARNING);
        launcher_->Refresh();
        return;
    }
    if (!database_.SaveFocusSettings(next_settings)) {
        ShowDatabaseError();
    }

    focus_active_ = true;
    focus_paused_ = false;
    focus_remaining_seconds_ = static_cast<ULONGLONG>(next_settings.minutes) * 60;
    focus_last_tick_ = GetTickCount64();
    focus_domains_ = next_settings.domains;
    SetTimer(host_, kFocusTimer, 1000, nullptr);
    UpdateTrayTip();
    launcher_->Refresh();
}

void TwoSemiApp::ToggleFocusPause() {
    if (!focus_active_) {
        return;
    }
    AdvanceFocusClock();
    if (focus_remaining_seconds_ == 0) {
        TickFocus();
        return;
    }
    focus_paused_ = !focus_paused_;
    focus_last_tick_ = GetTickCount64();
    UpdateTrayTip();
    launcher_->Refresh();
}

void TwoSemiApp::StopFocus() {
    if (!focus_active_) {
        return;
    }
    KillTimer(host_, kFocusTimer);
    const bool cleared = focus_domains_.empty() || RunElevatedFocusCommand(false, {});
    focus_active_ = false;
    focus_paused_ = false;
    focus_remaining_seconds_ = 0;
    focus_last_tick_ = 0;
    focus_domains_.clear();
    UpdateTrayTip();
    if (!cleared && launcher_ != nullptr && launcher_->Window() != nullptr) {
        MessageBoxW(launcher_->Window(),
                    L"The website block could not be removed. Run TwoSemi again to retry cleanup.",
                    L"TwoSemi", MB_OK | MB_ICONWARNING);
    }
    if (launcher_ != nullptr) {
        launcher_->Refresh();
    }
}

LauncherWindow::LauncherWindow(TwoSemiApp* app) : app_(app) {
    static std::once_flag registered;
    std::call_once(registered, [] {
        WNDCLASSW window_class{};
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.lpfnWndProc = WindowProc;
        window_class.lpszClassName = kLauncherClassName;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground = nullptr;
        SetAppWindowClassIcon(window_class);
        RegisterClassW(&window_class);
    });

    window_ = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kLauncherClassName,
        L"TwoSemi",
        WS_POPUP,
        0,
        0,
        kLauncherWidth,
        kLauncherHeight,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        this);
    SetAppWindowIcons(window_);
}

LauncherWindow::~LauncherWindow() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
    DeleteObject(regular_font_);
    DeleteObject(bold_font_);
    DeleteObject(small_font_);
    DeleteObject(footer_font_);
}

void LauncherWindow::CreateControls() {
    regular_font_ = UiFont(10);
    bold_font_ = UiFont(11);
    small_font_ = UiFont(8);
    footer_font_ = UiFont(11);
    search_ = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              0, 32, kLauncherWidth, 22,
                              window_, reinterpret_cast<HMENU>(201), GetModuleHandleW(nullptr), nullptr);
    results_ = CreateWindowExW(0, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT |
                                   LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
                               0, 66, kLauncherWidth, 280,
                               window_, reinterpret_cast<HMENU>(202), GetModuleHandleW(nullptr), nullptr);
    hint_ = CreateWindowW(L"STATIC", L"",
                          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
                          4, 348, kLauncherWidth - 8, 22,
                          window_, reinterpret_cast<HMENU>(203), GetModuleHandleW(nullptr), nullptr);

    for (HWND control : {search_, results_}) {
        SetFont(control, regular_font_);
    }
    SetFont(hint_, footer_font_);

    SetPropW(search_, kLauncherThisProperty, reinterpret_cast<HANDLE>(this));
    SetPropW(results_, kLauncherThisProperty, reinterpret_cast<HANDLE>(this));
    SetPropW(search_, kLauncherOriginalProperty,
             reinterpret_cast<HANDLE>(SetWindowLongPtrW(search_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc))));
    SetPropW(results_, kLauncherOriginalProperty,
             reinterpret_cast<HANDLE>(SetWindowLongPtrW(results_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(ListProc))));
}

void LauncherWindow::Show(const std::wstring& captured_text) {
    captured_text_ = captured_text;
    Position();
    Refresh();
    ShowWindow(window_, SW_SHOW);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    ActivateAndFocus(window_, search_);
    SetForegroundWindow(window_);
    SetFocus(search_);
    SendMessageW(search_, EM_SETSEL, 0, -1);
}

void LauncherWindow::Hide() {
    ShowWindow(window_, SW_HIDE);
}

void LauncherWindow::OpenTodoGroup(const std::wstring& group_id) {
    todo_groups_ = app_->database_.LoadTodoGroups();
    const auto group = std::find_if(todo_groups_.begin(), todo_groups_.end(), [&](const TodoGroup& candidate) {
        return candidate.id == group_id;
    });
    if (group == todo_groups_.end()) {
        Refresh();
        return;
    }
    active_todo_group_ = static_cast<size_t>(std::distance(todo_groups_.begin(), group));
    todo_group_view_ = true;
    todo_groups_view_ = false;
    note_view_ = false;
    reminder_view_ = false;
    streak_view_ = false;
    chat_view_ = false;
    view_title_ = L"twosemi :: todo // " + group->title;
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::OpenTodoGroups() {
    todo_group_view_ = false;
    todo_groups_view_ = true;
    note_view_ = false;
    reminder_view_ = false;
    streak_view_ = false;
    chat_view_ = false;
    view_title_ = L"twosemi :: todo groups";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::OpenNotes() {
    todo_group_view_ = false;
    todo_groups_view_ = false;
    note_view_ = true;
    reminder_view_ = false;
    streak_view_ = false;
    chat_view_ = false;
    view_title_ = L"twosemi :: notes";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::OpenReminders() {
    todo_group_view_ = false;
    todo_groups_view_ = false;
    note_view_ = false;
    reminder_view_ = true;
    streak_view_ = false;
    chat_view_ = false;
    view_title_ = L"twosemi :: reminders";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::OpenStreaks() {
    todo_group_view_ = false;
    todo_groups_view_ = false;
    note_view_ = false;
    reminder_view_ = false;
    streak_view_ = true;
    chat_view_ = false;
    view_title_ = L"twosemi :: streaks";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::OpenChats() {
    todo_group_view_ = false;
    todo_groups_view_ = false;
    note_view_ = false;
    reminder_view_ = false;
    streak_view_ = false;
    chat_view_ = true;
    view_title_ = L"twosemi :: chats";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::Position() {
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const RECT work = info.rcWork;
    int x = cursor.x - 200;
    int y = cursor.y + 22;
    x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right) - kLauncherWidth);
    y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom) - kLauncherHeight);
    SetWindowPos(window_, HWND_TOPMOST, x, y, kLauncherWidth, kLauncherHeight, SWP_NOACTIVATE);
}

void LauncherWindow::Refresh() {
    if (search_ == nullptr || results_ == nullptr) {
        return;
    }

    std::optional<std::pair<LauncherItemKind, size_t>> selected_item;
    const LRESULT previous_selection = SendMessageW(results_, LB_GETCURSEL, 0, 0);
    if (previous_selection != LB_ERR && static_cast<size_t>(previous_selection) < items_.size()) {
        const LauncherItem& item = items_[static_cast<size_t>(previous_selection)];
        selected_item = std::make_pair(item.kind, item.index);
    }

    const std::wstring query = Lowercase(Trim(WindowText(search_)));
    notes_ = app_->database_.LoadNotes();
    reminders_ = app_->database_.LoadReminders();
    todo_groups_ = app_->database_.LoadTodoGroups();
    streaks_ = app_->database_.LoadStreaks();
    chat_threads_ = app_->database_.LoadChatThreads();

    std::vector<LauncherItem> all_items;
    if (todo_group_view_ && active_todo_group_ < todo_groups_.size()) {
        all_items.push_back({LauncherItemKind::NewTodoTask, L"New task", L"Add a task to this group", 0});
        const TodoGroup& group = todo_groups_[active_todo_group_];
        for (size_t index = 0; index < group.items.size(); ++index) {
            const TodoItem& task = group.items[index];
            all_items.push_back({LauncherItemKind::TodoTask,
                                 task.completed ? L"[x] " + task.text : L"[ ] " + task.text,
                                 task.completed ? L"Completed" : L"Open",
                                 index});
        }
    } else if (todo_groups_view_) {
        all_items.push_back({LauncherItemKind::NewTodoGroup, L"New todo group", L"Create a group of tasks", 0});
        for (size_t index = 0; index < todo_groups_.size(); ++index) {
            const TodoGroup& group = todo_groups_[index];
            const size_t completed = static_cast<size_t>(std::count_if(group.items.begin(), group.items.end(),
                                                                         [](const TodoItem& item) { return item.completed; }));
            all_items.push_back({LauncherItemKind::TodoGroup, group.title,
                                 std::to_wstring(completed) + L"/" + std::to_wstring(group.items.size()) + L" complete",
                                 index});
        }
    } else if (note_view_) {
        all_items.push_back({LauncherItemKind::NewNote, L"New note", L"Create a note from the current selection or typed text", 0});
        for (size_t index = 0; index < notes_.size(); ++index) {
            const Note& note = notes_[index];
            const std::wstring detail = note.kind == Note::Kind::Secret
                                            ? (note.secret_available ? L"Secret note" : L"Secret note unavailable")
                                            : NotePreview(note.body);
            all_items.push_back({LauncherItemKind::Note, note.title, detail, index});
        }
    } else if (reminder_view_) {
        all_items.push_back({LauncherItemKind::NewReminder, L"New reminder", L"Schedule a reminder", 0});
        for (size_t index = 0; index < reminders_.size(); ++index) {
            const Reminder& reminder = reminders_[index];
            const std::wstring status = !reminder.completed_at_utc.empty()
                                            ? L"Completed"
                                            : reminder.snoozed_until_utc.empty()
                                                  ? L"Due " + FormatReminderTime(reminder.due_at_utc)
                                                  : L"Snoozed until " + FormatReminderTime(reminder.snoozed_until_utc);
            all_items.push_back({LauncherItemKind::Reminder, reminder.text, status, index});
        }
    } else if (streak_view_) {
        all_items.push_back({LauncherItemKind::NewStreak, L"New streak", L"Create a daily or weekday habit", 0});
        for (size_t index = 0; index < streaks_.size(); ++index) {
            const Streak& streak = streaks_[index];
            all_items.push_back({LauncherItemKind::Streak, streak.title, StreakSummary(streak), index});
        }
    } else if (chat_view_) {
        all_items.push_back({LauncherItemKind::NewChat, L"New chat", L"Start a saved AI conversation", 0});
        for (size_t index = 0; index < chat_threads_.size(); ++index) {
            const ChatThread& thread = chat_threads_[index];
            const size_t message_count = thread.messages.size();
            all_items.push_back({LauncherItemKind::ChatThread, thread.title,
                                 std::to_wstring(message_count) + L" message" +
                                     (message_count == 1 ? L"" : L"s"),
                                 index});
        }
    } else {
        todo_group_view_ = false;
        todo_groups_view_ = false;
        note_view_ = false;
        reminder_view_ = false;
        streak_view_ = false;
        chat_view_ = false;
        all_items.push_back({LauncherItemKind::NewNote, L"New note", L"Create a note from the current selection or typed text", 0});
        all_items.push_back({LauncherItemKind::ViewNotes, L"View notes", L"Open saved notes", 0});
        all_items.push_back({LauncherItemKind::NewReminder, L"New reminder", L"Schedule a reminder", 0});
        all_items.push_back({LauncherItemKind::ViewReminders, L"View reminders", L"Open scheduled reminders", 0});
        if (app_->FocusActive()) {
            const std::wstring domain_detail = app_->FocusDomains().empty()
                                                   ? L"No websites blocked"
                                                   : std::to_wstring(app_->FocusDomains().size()) + L" websites blocked";
            all_items.push_back({LauncherItemKind::FocusStatus,
                                 L"Focus " + app_->FocusRemaining(),
                                 app_->FocusPaused() ? L"Paused | " + domain_detail : domain_detail,
                                 0});
            all_items.push_back({LauncherItemKind::FocusPause,
                                 app_->FocusPaused() ? L"Resume focus" : L"Pause focus",
                                 L"Keep the session and website block in place", 0});
            all_items.push_back({LauncherItemKind::FocusStop, L"Stop focus", L"End the session and restore websites", 0});
        } else {
            all_items.push_back({LauncherItemKind::StartFocus, L"Start focus", L"Start a timed session with optional website blocking", 0});
        }
        all_items.push_back({LauncherItemKind::NewTodoGroup, L"New todo group", L"Create a group of tasks", 0});
        all_items.push_back({LauncherItemKind::ViewTodoGroups, L"View todo groups", L"Open saved todo groups", 0});
        all_items.push_back({LauncherItemKind::NewStreak, L"New streak", L"Create a daily or weekday habit", 0});
        all_items.push_back({LauncherItemKind::ViewStreaks, L"View streaks", L"See today's check-ins and best runs", 0});
        all_items.push_back({LauncherItemKind::NewChat, L"New chat", L"Start a saved AI conversation", 0});
        all_items.push_back({LauncherItemKind::ViewChats, L"View chats", L"Open saved AI conversations", 0});
        all_items.push_back({LauncherItemKind::ConfigureModels, L"Configure models", L"Manage AI providers and API keys", 0});

        for (size_t index = 0; index < notes_.size(); ++index) {
            const Note& note = notes_[index];
            const std::wstring detail = note.kind == Note::Kind::Secret
                                            ? (note.secret_available ? L"Secret note" : L"Secret note unavailable")
                                            : NotePreview(note.body);
            all_items.push_back({LauncherItemKind::Note, note.title, detail, index});
        }
        for (size_t index = 0; index < reminders_.size(); ++index) {
            const Reminder& reminder = reminders_[index];
            const std::wstring status = !reminder.completed_at_utc.empty()
                                            ? L"Completed"
                                            : reminder.snoozed_until_utc.empty()
                                                  ? L"Due " + FormatReminderTime(reminder.due_at_utc)
                                                  : L"Snoozed until " + FormatReminderTime(reminder.snoozed_until_utc);
            all_items.push_back({LauncherItemKind::Reminder, reminder.text, status, index});
        }
        for (size_t index = 0; index < todo_groups_.size(); ++index) {
            const TodoGroup& group = todo_groups_[index];
            const size_t completed = static_cast<size_t>(std::count_if(group.items.begin(), group.items.end(),
                                                                         [](const TodoItem& item) { return item.completed; }));
            all_items.push_back({LauncherItemKind::TodoGroup, group.title,
                                 std::to_wstring(completed) + L"/" + std::to_wstring(group.items.size()) + L" complete",
                                 index});
        }
        for (size_t index = 0; index < streaks_.size(); ++index) {
            const Streak& streak = streaks_[index];
            all_items.push_back({LauncherItemKind::Streak, streak.title, StreakSummary(streak), index});
        }
        for (size_t index = 0; index < chat_threads_.size(); ++index) {
            const ChatThread& thread = chat_threads_[index];
            const size_t message_count = thread.messages.size();
            all_items.push_back({LauncherItemKind::ChatThread, thread.title,
                                 std::to_wstring(message_count) + L" message" +
                                     (message_count == 1 ? L"" : L"s"),
                                 index});
        }
    }

    items_.clear();
    for (const LauncherItem& item : all_items) {
        if (query.empty() || Lowercase(item.title).find(query) != std::wstring::npos ||
            Lowercase(item.detail).find(query) != std::wstring::npos) {
            items_.push_back(item);
        }
    }

    SendMessageW(results_, LB_RESETCONTENT, 0, 0);
    for (size_t index = 0; index < items_.size(); ++index) {
        SendMessageW(results_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(items_[index].title.c_str()));
        SendMessageW(results_, LB_SETITEMDATA, index, static_cast<LPARAM>(index));
    }
    if (!items_.empty()) {
        size_t selection = 0;
        if (selected_item.has_value()) {
            const auto matching_item = std::find_if(items_.begin(), items_.end(), [&](const LauncherItem& item) {
                return item.kind == selected_item->first && item.index == selected_item->second;
            });
            if (matching_item != items_.end()) {
                selection = static_cast<size_t>(std::distance(items_.begin(), matching_item));
            }
        }
        SendMessageW(results_, LB_SETCURSEL, static_cast<WPARAM>(selection), 0);
    }
    UpdateHint();
    InvalidateRect(results_, nullptr, FALSE);
}

void LauncherWindow::UpdateHint() {
    std::wstring hint;
    if (const LauncherItem* item = SelectedItem(); item != nullptr) {
        switch (item->kind) {
        case LauncherItemKind::NewNote:
        case LauncherItemKind::NewReminder:
        case LauncherItemKind::NewTodoGroup:
        case LauncherItemKind::NewStreak:
        case LauncherItemKind::NewChat:
        case LauncherItemKind::NewTodoTask:
            hint = L"Enter create";
            break;
        case LauncherItemKind::ViewNotes:
        case LauncherItemKind::ViewReminders:
        case LauncherItemKind::ViewTodoGroups:
        case LauncherItemKind::ViewStreaks:
        case LauncherItemKind::ViewChats:
        case LauncherItemKind::ConfigureModels:
            hint = L"Enter open";
            break;
        case LauncherItemKind::StartFocus:
            hint = L"Enter start";
            break;
        case LauncherItemKind::FocusStatus:
            hint = L"Enter view";
            break;
        case LauncherItemKind::FocusPause:
            hint = L"Enter pause/resume";
            break;
        case LauncherItemKind::FocusStop:
            hint = L"Enter stop";
            break;
        case LauncherItemKind::Note:
            hint = L"Enter fill  |  Ctrl+Enter edit  |  Del remove";
            break;
        case LauncherItemKind::Reminder:
            hint = L"Enter edit  |  Del remove";
            break;
        case LauncherItemKind::TodoGroup:
            hint = L"Enter open  |  Ctrl+Enter edit  |  Del remove";
            break;
        case LauncherItemKind::Streak:
            hint = L"Enter check  |  Ctrl+Enter edit  |  Del remove";
            break;
        case LauncherItemKind::ChatThread:
            hint = L"Enter open  |  Ctrl+Enter edit  |  Del remove";
            break;
        case LauncherItemKind::TodoTask:
            hint = L"Enter check  |  Ctrl+Enter edit  |  Del remove";
            break;
        }
    }
    if (todo_group_view_ || todo_groups_view_ || note_view_ || reminder_view_ || streak_view_ || chat_view_) {
        hint += L"  |  Alt+Left back";
    }
    SetWindowTextW(hint_, hint.c_str());
}

LauncherItem* LauncherWindow::SelectedItem() {
    const LRESULT selected = SendMessageW(results_, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<size_t>(selected) >= items_.size()) {
        return nullptr;
    }
    return &items_[static_cast<size_t>(selected)];
}

void LauncherWindow::MoveSelection(int delta) {
    const LRESULT count = SendMessageW(results_, LB_GETCOUNT, 0, 0);
    if (count <= 0) {
        return;
    }
    const LRESULT current = SendMessageW(results_, LB_GETCURSEL, 0, 0);
    const int next = std::clamp(static_cast<int>(current == LB_ERR ? 0 : current) + delta,
                                0, static_cast<int>(count - 1));
    SendMessageW(results_, LB_SETCURSEL, next, 0);
    UpdateHint();
    InvalidateRect(results_, nullptr, FALSE);
}

void LauncherWindow::ExecuteSelected() {
    LauncherItem* item = SelectedItem();
    if (item == nullptr) {
        return;
    }
    switch (item->kind) {
    case LauncherItemKind::NewNote:
        app_->CreateNote(captured_text_);
        return;
    case LauncherItemKind::NewReminder:
        app_->CreateReminder(captured_text_);
        return;
    case LauncherItemKind::StartFocus:
        app_->StartFocus();
        return;
    case LauncherItemKind::FocusStatus:
        return;
    case LauncherItemKind::FocusPause:
        app_->ToggleFocusPause();
        return;
    case LauncherItemKind::FocusStop:
        app_->StopFocus();
        return;
    case LauncherItemKind::NewTodoGroup:
        app_->CreateTodoGroup();
        return;
    case LauncherItemKind::ViewTodoGroups:
        OpenTodoGroups();
        return;
    case LauncherItemKind::NewStreak:
        app_->CreateStreak();
        return;
    case LauncherItemKind::NewChat:
        app_->CreateChat(captured_text_);
        return;
    case LauncherItemKind::ViewNotes:
        OpenNotes();
        return;
    case LauncherItemKind::ViewReminders:
        OpenReminders();
        return;
    case LauncherItemKind::ViewStreaks:
        OpenStreaks();
        return;
    case LauncherItemKind::ViewChats:
        OpenChats();
        return;
    case LauncherItemKind::ConfigureModels:
        app_->ConfigureModels();
        return;
    case LauncherItemKind::Note:
        if (item->index < notes_.size()) {
            app_->UseNote(notes_[item->index]);
        }
        return;
    case LauncherItemKind::Reminder:
        if (item->index < reminders_.size()) {
            app_->EditReminder(reminders_[item->index]);
        }
        return;
    case LauncherItemKind::TodoGroup:
        if (item->index < todo_groups_.size()) {
            todo_group_view_ = true;
            todo_groups_view_ = false;
            note_view_ = false;
            reminder_view_ = false;
            streak_view_ = false;
            chat_view_ = false;
            active_todo_group_ = item->index;
            view_title_ = L"twosemi :: todo // " + todo_groups_[item->index].title;
            SetWindowTextW(search_, L"");
            Refresh();
        }
        return;
    case LauncherItemKind::Streak:
        if (item->index < streaks_.size()) {
            app_->ToggleStreak(streaks_[item->index]);
        }
        return;
    case LauncherItemKind::ChatThread:
        if (item->index < chat_threads_.size()) {
            app_->OpenChatThread(chat_threads_[item->index].id);
        }
        return;
    case LauncherItemKind::NewTodoTask:
        if (active_todo_group_ < todo_groups_.size()) {
            app_->CreateTodoTask(todo_groups_[active_todo_group_]);
        }
        return;
    case LauncherItemKind::TodoTask:
        if (todo_group_view_ && active_todo_group_ < todo_groups_.size() &&
            item->index < todo_groups_[active_todo_group_].items.size()) {
            app_->ToggleTodoTask(todo_groups_[active_todo_group_],
                                 todo_groups_[active_todo_group_].items[item->index]);
        }
        return;
    }
}

void LauncherWindow::HandleKey(WPARAM key) {
    switch (key) {
    case VK_UP:
        MoveSelection(-1);
        break;
    case VK_DOWN:
        MoveSelection(1);
        break;
    case VK_RETURN:
        ExecuteSelected();
        break;
    case VK_F2:
        EditSelected();
        break;
    case VK_DELETE:
        DeleteSelected();
        break;
    case VK_ESCAPE:
        app_->HideLauncher();
        break;
    default:
        break;
    }
}

void LauncherWindow::GoBack() {
    if (!todo_group_view_ && !todo_groups_view_ && !note_view_ && !reminder_view_ &&
        !streak_view_ && !chat_view_) {
        return;
    }
    todo_group_view_ = false;
    todo_groups_view_ = false;
    note_view_ = false;
    reminder_view_ = false;
    streak_view_ = false;
    chat_view_ = false;
    active_todo_group_ = 0;
    view_title_ = L"twosemi :: launcher";
    SetWindowTextW(search_, L"");
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::EditSelected() {
    const LauncherItem* item = SelectedItem();
    if (item == nullptr) {
        return;
    }
    switch (item->kind) {
    case LauncherItemKind::Note:
        if (item->index < notes_.size()) app_->EditNoteItem(notes_[item->index]);
        break;
    case LauncherItemKind::Reminder:
        if (item->index < reminders_.size()) app_->EditReminder(reminders_[item->index]);
        break;
    case LauncherItemKind::TodoGroup:
        if (item->index < todo_groups_.size()) app_->EditTodoGroup(todo_groups_[item->index]);
        break;
    case LauncherItemKind::Streak:
        if (item->index < streaks_.size()) app_->EditStreak(streaks_[item->index]);
        break;
    case LauncherItemKind::ChatThread:
        if (item->index < chat_threads_.size()) app_->EditChatThread(chat_threads_[item->index]);
        break;
    case LauncherItemKind::TodoTask:
        if (todo_group_view_ && active_todo_group_ < todo_groups_.size() &&
            item->index < todo_groups_[active_todo_group_].items.size()) {
            app_->EditTodoTask(todo_groups_[active_todo_group_],
                               todo_groups_[active_todo_group_].items[item->index]);
        }
        break;
    default:
        break;
    }
}

void LauncherWindow::DeleteSelected() {
    const LauncherItem* item = SelectedItem();
    if (item == nullptr) {
        return;
    }
    switch (item->kind) {
    case LauncherItemKind::Note:
        if (item->index < notes_.size()) app_->DeleteNote(notes_[item->index]);
        break;
    case LauncherItemKind::Reminder:
        if (item->index < reminders_.size()) app_->DeleteReminder(reminders_[item->index]);
        break;
    case LauncherItemKind::TodoGroup:
        if (item->index < todo_groups_.size()) app_->DeleteTodoGroup(todo_groups_[item->index]);
        break;
    case LauncherItemKind::Streak:
        if (item->index < streaks_.size()) app_->DeleteStreak(streaks_[item->index]);
        break;
    case LauncherItemKind::ChatThread:
        if (item->index < chat_threads_.size()) app_->DeleteChatThread(chat_threads_[item->index]);
        break;
    case LauncherItemKind::TodoTask:
        if (todo_group_view_ && active_todo_group_ < todo_groups_.size() &&
            item->index < todo_groups_[active_todo_group_].items.size()) {
            app_->DeleteTodoTask(todo_groups_[active_todo_group_],
                                 todo_groups_[active_todo_group_].items[item->index]);
        }
        break;
    default:
        break;
    }
    ActivateAndFocus(window_, results_);
}

LRESULT CALLBACK LauncherWindow::EditProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* launcher = reinterpret_cast<LauncherWindow*>(GetPropW(window, kLauncherThisProperty));
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kLauncherOriginalProperty));
    if (launcher != nullptr && message == WM_KEYDOWN) {
        if (w_param == 'A' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            SendMessageW(window, EM_SETSEL, 0, -1);
            return 0;
        }
        if (w_param == VK_TAB) {
            SetFocus(launcher->results_);
            return 0;
        }
        if (w_param == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            launcher->EditSelected();
            return 0;
        }
        if (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_RETURN || w_param == VK_ESCAPE) {
            launcher->HandleKey(w_param);
            return 0;
        }
    }
    if (launcher != nullptr && message == WM_SYSKEYDOWN && w_param == VK_LEFT) {
        launcher->GoBack();
        return 0;
    }
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK LauncherWindow::ListProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* launcher = reinterpret_cast<LauncherWindow*>(GetPropW(window, kLauncherThisProperty));
    auto original = reinterpret_cast<WNDPROC>(GetPropW(window, kLauncherOriginalProperty));
    if (launcher != nullptr && message == WM_KEYDOWN) {
        if (w_param == VK_TAB) {
            SetFocus(launcher->search_);
            return 0;
        }
        if (w_param == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            launcher->EditSelected();
            return 0;
        }
        if (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_RETURN || w_param == VK_ESCAPE ||
            w_param == VK_F2 || w_param == VK_DELETE) {
            launcher->HandleKey(w_param);
            return 0;
        }
    }
    if (launcher != nullptr && message == WM_SYSKEYDOWN && w_param == VK_LEFT) {
        launcher->GoBack();
        return 0;
    }
    return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT CALLBACK LauncherWindow::WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param) {
    auto* launcher = reinterpret_cast<LauncherWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        launcher = static_cast<LauncherWindow*>(create->lpCreateParams);
        launcher->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(launcher));
    }

    if (launcher == nullptr) {
        return DefWindowProcW(window, message, w_param, l_param);
    }

    switch (message) {
    case WM_CREATE:
        launcher->CreateControls();
        return 0;
    case WM_COMMAND:
        if (HIWORD(w_param) == EN_CHANGE && LOWORD(w_param) == 201) {
            launcher->Refresh();
            return 0;
        }
        if (HIWORD(w_param) == LBN_SELCHANGE && LOWORD(w_param) == 202) {
            launcher->UpdateHint();
            return 0;
        }
        if (HIWORD(w_param) == LBN_DBLCLK && LOWORD(w_param) == 202) {
            launcher->ExecuteSelected();
            return 0;
        }
        break;
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(l_param);
        if (measure != nullptr && measure->CtlID == 202) {
            measure->itemHeight = 46;
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(l_param);
        if (draw != nullptr && draw->CtlID == 202 && draw->itemID != static_cast<UINT>(-1)) {
            const size_t index = static_cast<size_t>(draw->itemID);
            if (index < launcher->items_.size()) {
                const bool selected = (draw->itemState & ODS_SELECTED) != 0;
                HBRUSH background = CreateSolidBrush(selected ? kSelection : kBackground);
                FillRect(draw->hDC, &draw->rcItem, background);
                DeleteObject(background);
                SetBkMode(draw->hDC, TRANSPARENT);
                const LauncherItemKind kind = launcher->items_[index].kind;
                const bool is_action = kind == LauncherItemKind::NewNote ||
                                       kind == LauncherItemKind::NewReminder ||
                                       kind == LauncherItemKind::StartFocus ||
                                       kind == LauncherItemKind::FocusStatus ||
                                       kind == LauncherItemKind::FocusPause ||
                                       kind == LauncherItemKind::FocusStop ||
                                       kind == LauncherItemKind::NewTodoGroup ||
                                       kind == LauncherItemKind::ViewTodoGroups ||
                                       kind == LauncherItemKind::NewStreak ||
                                       kind == LauncherItemKind::NewChat ||
                                       kind == LauncherItemKind::ViewNotes ||
                                       kind == LauncherItemKind::ViewReminders ||
                                       kind == LauncherItemKind::ViewStreaks ||
                                       kind == LauncherItemKind::ViewChats ||
                                       kind == LauncherItemKind::ConfigureModels ||
                                       kind == LauncherItemKind::NewTodoTask;
                wchar_t marker = L'-';
                if (is_action) marker = L'+';
                if (kind == LauncherItemKind::Note) marker = L'N';
                if (kind == LauncherItemKind::Reminder) marker = L'R';
                if (kind == LauncherItemKind::TodoGroup || kind == LauncherItemKind::TodoTask) marker = L'T';
                if (kind == LauncherItemKind::Streak) marker = L'S';
                if (kind == LauncherItemKind::ChatThread) marker = L'A';
                HFONT prev_font = static_cast<HFONT>(SelectObject(draw->hDC, launcher->bold_font_));
                SetTextColor(draw->hDC, selected ? RGB(232, 244, 234) : (is_action ? kAccent : kMutedText));
                TextOutW(draw->hDC, draw->rcItem.left + 4, draw->rcItem.top + 4, &marker, 1);
                RECT title_rect = draw->rcItem;
                title_rect.left += 22;
                title_rect.top += 4;
                title_rect.right -= 4;
                SetTextColor(draw->hDC, selected ? RGB(232, 244, 234) : kText);
                DrawTextW(draw->hDC, launcher->items_[index].title.c_str(), -1, &title_rect,
                          DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                if (!launcher->items_[index].detail.empty()) {
                    SelectObject(draw->hDC, launcher->small_font_);
                    RECT detail_rect = draw->rcItem;
                    detail_rect.left += 22;
                    detail_rect.top += 28;
                    detail_rect.right -= 4;
                    SetTextColor(draw->hDC, selected ? RGB(218, 229, 246) : kMutedText);
                    DrawTextW(draw->hDC, launcher->items_[index].detail.c_str(), -1, &detail_rect,
                              DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
                SelectObject(draw->hDC, prev_font);
                return TRUE;
            }
        }
        break;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(w_param);
        const bool is_edit = message == WM_CTLCOLOREDIT;
        SetTextColor(dc, is_edit ? kText : kMutedText);
        SetBkColor(dc, is_edit ? kInputBackground : kBackground);
        static HBRUSH background_brush = CreateSolidBrush(kBackground);
        static HBRUSH input_brush = CreateSolidBrush(kInputBackground);
        return reinterpret_cast<LRESULT>(is_edit ? input_brush : background_brush);
    }
    case WM_ERASEBKGND: {
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(reinterpret_cast<HDC>(w_param), &client, background);
        DeleteObject(background);
        return 1;
    }
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window, message, w_param, l_param);
        POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        ScreenToClient(window, &point);
        if (hit == HTCLIENT && point.y < 30) {
            return HTCAPTION;
        }
        return hit;
    }
    case WM_MOUSEACTIVATE:
        SetForegroundWindow(window);
        SetFocus(launcher->search_);
        return MA_ACTIVATE;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        HBRUSH background = CreateSolidBrush(kBackground);
        FillRect(dc, &client, background);
        DeleteObject(background);
        SetBkMode(dc, TRANSPARENT);
        HFONT previous_font = static_cast<HFONT>(SelectObject(dc, launcher->regular_font_));
        SetTextColor(dc, kMutedText);
        constexpr wchar_t close_hint[] = L"Esc close";
        SIZE close_size{};
        GetTextExtentPoint32W(dc, close_hint, static_cast<int>(std::size(close_hint) - 1), &close_size);
        const int close_left = std::max(4, static_cast<int>(client.right - close_size.cx - 4));
        RECT title_rect{4, 3, close_left - 8, 27};
        DrawTextW(dc, launcher->view_title_.c_str(), -1, &title_rect,
                  DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT close_rect{close_left, 3, client.right - 4, 27};
        DrawTextW(dc, close_hint, -1, &close_rect, DT_SINGLELINE | DT_LEFT | DT_NOPREFIX);
        SelectObject(dc, previous_font);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CLOSE:
        launcher->Hide();
        return 0;
    case WM_DESTROY:
        RemovePropW(launcher->search_, kLauncherThisProperty);
        RemovePropW(launcher->results_, kLauncherThisProperty);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

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
