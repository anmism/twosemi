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
                         ? (note.kind == Note::Kind::Secret
                                ? L"Secret note"
                                : note.kind == Note::Kind::Link ? LinkHost(draft.url)
                                                                : MakeNoteTitle(draft.body))
                         : draft.title;
        note.body = draft.body;
        note.url = note.kind == Note::Kind::Link ? draft.url : L"";
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
        draft.url = existing.url;
        draft.kind = existing.kind;
        if (!EditNote(launcher_->Window(), draft)) {
            launcher_->Refresh();
            return;
        }

        Note note = existing;
        note.kind = draft.kind;
        note.title = draft.title.empty()
                         ? (note.kind == Note::Kind::Secret
                                ? L"Secret note"
                                : note.kind == Note::Kind::Link ? LinkHost(draft.url)
                                                                : MakeNoteTitle(draft.body))
                         : draft.title;
        note.body = draft.body;
        note.url = note.kind == Note::Kind::Link ? draft.url : L"";
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
        if (note.kind == Note::Kind::Link) {
            if (!OpenHttpLink(note.url)) {
                MessageBoxW(launcher_->Window(), L"This note does not contain a valid web link.",
                            L"TwoSemi", MB_OK | MB_ICONWARNING);
                return;
            }
            HideLauncher();
            target_window_ = nullptr;
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
