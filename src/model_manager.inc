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
        SetTextColor(draw->hDC, save_button ? kSelectionText : kText);
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
        SetTextColor(draw->hDC, accent ? kSelectionText : kText);
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
