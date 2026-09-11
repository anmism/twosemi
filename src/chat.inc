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
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(301)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(state->transcript, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(kBackground));
        state->input = CreateWindowExW(
            0, L"EDIT", state->initial_text.c_str(),
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
            0, kChatHeight - 90, kChatWidth - 64, 60, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(302)), GetModuleHandleW(nullptr), nullptr);
        state->send = CreateWindowW(
            L"BUTTON", L"Send", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            kChatWidth - 64, kChatHeight - 90, 64, 60, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), GetModuleHandleW(nullptr), nullptr);
        state->screenshot = CreateWindowW(
            L"BUTTON", L"Screenshot", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            4, kChatHeight - 150, 104, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChatScreenshotCommand)), GetModuleHandleW(nullptr), nullptr);
        state->upload = CreateWindowW(
            L"BUTTON", L"Upload", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            112, kChatHeight - 150, 84, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChatUploadCommand)), GetModuleHandleW(nullptr), nullptr);
        state->clear_attachment = CreateWindowW(
            L"BUTTON", L"X", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
            200, kChatHeight - 150, 30, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChatClearAttachmentCommand)), GetModuleHandleW(nullptr), nullptr);
        state->attachment_preview = CreateWindowW(
            L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
            280, kChatHeight - 150, 30, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kChatAttachmentPreview)), GetModuleHandleW(nullptr), nullptr);
        state->status = CreateWindowW(
            L"STATIC", L"Ctrl+Enter sends", WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
            318, kChatHeight - 150, kChatWidth - 322, 30, window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(303)), GetModuleHandleW(nullptr), nullptr);
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
