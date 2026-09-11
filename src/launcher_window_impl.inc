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

#include "launcher_editors.inc"

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
    if (editor_kind_ != LauncherEditorKind::None) {
        UpdateEditorHint();
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
        BeginNewNoteEditor();
        return;
    case LauncherItemKind::NewReminder:
        BeginNewReminderEditor();
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
        BeginNewTodoGroupEditor();
        return;
    case LauncherItemKind::ViewTodoGroups:
        OpenTodoGroups();
        return;
    case LauncherItemKind::NewStreak:
        BeginNewStreakEditor();
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
            BeginEditReminderEditor(reminders_[item->index]);
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
        BeginNewTodoTaskEditor();
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
    if (editor_kind_ != LauncherEditorKind::None) {
        if (key == VK_ESCAPE) {
            CloseEditor();
        }
        return;
    }
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
    if (editor_kind_ != LauncherEditorKind::None) {
        CancelEditor();
        return;
    }
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
        if (item->index < notes_.size()) BeginEditNoteEditor(notes_[item->index]);
        break;
    case LauncherItemKind::Reminder:
        if (item->index < reminders_.size()) BeginEditReminderEditor(reminders_[item->index]);
        break;
    case LauncherItemKind::TodoGroup:
        if (item->index < todo_groups_.size()) BeginEditTodoGroupEditor(todo_groups_[item->index]);
        break;
    case LauncherItemKind::Streak:
        if (item->index < streaks_.size()) BeginEditStreakEditor(streaks_[item->index]);
        break;
    case LauncherItemKind::ChatThread:
        if (item->index < chat_threads_.size()) app_->EditChatThread(chat_threads_[item->index]);
        break;
    case LauncherItemKind::TodoTask:
        if (todo_group_view_ && active_todo_group_ < todo_groups_.size() &&
            item->index < todo_groups_[active_todo_group_].items.size()) {
            BeginEditTodoTaskEditor(todo_groups_[active_todo_group_],
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
    if (launcher != nullptr && GetPropW(window, kLauncherInlineFieldProperty) != nullptr) {
        if (message == WM_CHAR) {
            if (w_param == VK_ESCAPE) {
                return 0;
            }
            if (w_param == L'\r') {
                const bool control_held = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                const bool multiline = (GetWindowLongPtrW(window, GWL_STYLE) & ES_MULTILINE) != 0;
                if (!multiline || control_held) {
                    return 0;
                }
            }
        }
        if (message == WM_KEYDOWN) {
            if (w_param == VK_TAB) {
                launcher->FocusNextEditorField((GetKeyState(VK_SHIFT) & 0x8000) != 0);
                return 0;
            }
            if (w_param == VK_ESCAPE) {
                launcher->CloseEditor();
                return 0;
            }
            if (w_param == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000) != 0) {
                launcher->CommitEditor();
                return 0;
            }
        }
        if (message == WM_SYSKEYDOWN && w_param == VK_LEFT) {
            launcher->CancelEditor();
            return 0;
        }
        return original != nullptr ? CallWindowProcW(original, window, message, w_param, l_param)
                                    : DefWindowProcW(window, message, w_param, l_param);
    }
    if (launcher != nullptr && message == WM_CHAR &&
        (w_param == L'\r' || w_param == VK_ESCAPE)) {
        return 0;
    }
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
    if (launcher != nullptr && message == WM_CHAR &&
        (w_param == L'\r' || w_param == VK_ESCAPE)) {
        return 0;
    }
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
        if (launcher->editor_kind_ != LauncherEditorKind::None && LOWORD(w_param) == IDOK) {
            launcher->CommitEditor();
            return 0;
        }
        if (launcher->editor_kind_ != LauncherEditorKind::None && LOWORD(w_param) == IDCANCEL) {
            launcher->CancelEditor();
            return 0;
        }
        if (launcher->editor_kind_ == LauncherEditorKind::Note &&
            HIWORD(w_param) == CBN_SELCHANGE && LOWORD(w_param) == 603) {
            launcher->UpdateInlineNoteKind();
            return 0;
        }
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
        if (draw != nullptr && launcher->editor_kind_ != LauncherEditorKind::None &&
            (draw->CtlID == IDOK || draw->CtlID == IDCANCEL)) {
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
        if (launcher->editor_kind_ != LauncherEditorKind::None) {
            launcher->CloseEditor();
            return 0;
        }
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
