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
    HWND save_button = nullptr;
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
