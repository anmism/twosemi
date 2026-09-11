void LauncherWindow::EnableEditorField(HWND control) {
    if (control == nullptr) {
        return;
    }
    SetPropW(control, kLauncherThisProperty, reinterpret_cast<HANDLE>(this));
    SetPropW(control, kLauncherInlineFieldProperty, reinterpret_cast<HANDLE>(1));
    SetPropW(control, kLauncherOriginalProperty,
             reinterpret_cast<HANDLE>(SetWindowLongPtrW(
                 control, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc))));
    editor_focus_order_.push_back(control);
}

void LauncherWindow::DestroyEditorControls() {
    for (HWND control : editor_focus_order_) {
        if (control != nullptr && IsWindow(control)) {
            DestroyWindow(control);
        }
    }
    editor_focus_order_.clear();
    editor_cancel_button_ = nullptr;
    inline_note_ = NoteDraft{};
    inline_quick_ = QuickDraft{QuickEditorKind::Reminder, {}, {}};
    inline_note_existing_.reset();
    inline_reminder_existing_.reset();
    inline_todo_group_existing_.reset();
    inline_streak_existing_.reset();
    inline_todo_task_id_.clear();
}

void LauncherWindow::LayoutEditorControls() {
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int footer_y = client.bottom - 22;
    const int button_height = 28;
    const int button_width = 68;
    const int gap = 4;
    const int padding = 2;
    const int button_y = footer_y - padding - button_height;
    const int cancel_x = width - padding - button_width;
    const int save_x = cancel_x - gap - button_width;

    if (editor_kind_ == LauncherEditorKind::Note) {
        const int combo_width = 96;
        const int editor_top = 30 + padding;
        const int type_x = save_x - gap - combo_width;
        const int body_bottom = button_y - padding;
        const bool is_link = inline_note_.kind == Note::Kind::Link;
        const int body_top = is_link ? editor_top + button_height + padding : editor_top;
        SetWindowPos(inline_note_.url_edit, nullptr, padding, editor_top,
                     width - padding * 2, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(inline_note_.body_edit, nullptr, padding, body_top,
                     width - padding * 2, std::max(60, body_bottom - body_top),
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(inline_note_.title_edit, nullptr, padding, button_y,
                     type_x - gap - padding, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(inline_note_.kind_combo, nullptr, type_x, button_y,
                     combo_width, 150, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(inline_note_.save_button, nullptr, save_x, button_y,
                     button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(editor_cancel_button_, nullptr, cancel_x, button_y,
                     button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }

    SetWindowPos(inline_quick_.first_edit, nullptr, 4, 42, width - 8, 28,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    if (inline_quick_.second_picker != nullptr) {
        SetWindowPos(inline_quick_.second_picker, nullptr, 4, 78, 220, 28,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else if (inline_quick_.second_edit != nullptr) {
        SetWindowPos(inline_quick_.second_edit, nullptr, 4, 78, 220, 28,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (inline_quick_.frequency_combo != nullptr) {
        SetWindowPos(inline_quick_.frequency_combo, nullptr, 4, 78, 140, 150,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    SetWindowPos(inline_quick_.first_edit, nullptr, 4, 42, width - 8, 28,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(inline_quick_.save_button, nullptr, save_x, button_y,
                 button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(editor_cancel_button_, nullptr, cancel_x, button_y,
                 button_width, button_height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void LauncherWindow::UpdateEditorHint() {
    if (editor_kind_ == LauncherEditorKind::None) {
        return;
    }
    const std::wstring hint = editor_kind_ == LauncherEditorKind::Note
                                  ? L"Ctrl+Enter save  |  Esc save/close"
                                  : L"Ctrl+Enter save  |  Esc cancel";
    SetWindowTextW(hint_, hint.c_str());
}

void LauncherWindow::UpdateInlineNoteKind() {
    if (editor_kind_ != LauncherEditorKind::Note || inline_note_.body_edit == nullptr) {
        return;
    }
    const LRESULT selected = SendMessageW(inline_note_.kind_combo, CB_GETCURSEL, 0, 0);
    const bool link = selected == 1;
    const bool secret = selected == 2;
    inline_note_.kind = link ? Note::Kind::Link : secret ? Note::Kind::Secret : Note::Kind::Text;
    ShowWindow(inline_note_.url_edit, link ? SW_SHOW : SW_HIDE);
    LONG_PTR style = GetWindowLongPtrW(inline_note_.body_edit, GWL_STYLE);
    if (secret) {
        style &= ~(ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL);
        style |= ES_AUTOHSCROLL | ES_PASSWORD;
    } else {
        style &= ~(ES_AUTOHSCROLL | ES_PASSWORD);
        style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    }
    SetWindowLongPtrW(inline_note_.body_edit, GWL_STYLE, style);
    LayoutEditorControls();
    InvalidateRect(inline_note_.body_edit, nullptr, TRUE);
}

void LauncherWindow::CreateEditorControls() {
    ShowWindow(search_, SW_HIDE);
    ShowWindow(results_, SW_HIDE);
    ShowWindow(hint_, SW_SHOW);

    if (editor_kind_ == LauncherEditorKind::Note) {
        inline_note_.title_edit = CreateTextInput(window_, 601, inline_note_.title);
        SendMessageW(inline_note_.title_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"optional title"));
        inline_note_.body_edit = CreateMultilineInput(window_, 602, inline_note_.body);
        inline_note_.url_edit = CreateTextInput(window_, 604, inline_note_.url);
        SendMessageW(inline_note_.url_edit, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(L"https://example.com"));
        inline_note_.kind_combo = CreateChoiceInput(window_, 603);
        SendMessageW(inline_note_.kind_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Text"));
        SendMessageW(inline_note_.kind_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Link"));
        SendMessageW(inline_note_.kind_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Secret"));
        SendMessageW(inline_note_.kind_combo, CB_SETCURSEL,
                     static_cast<WPARAM>(inline_note_.kind == Note::Kind::Link
                                             ? 1
                                             : inline_note_.kind == Note::Kind::Secret ? 2 : 0),
                     0);
        inline_note_.save_button = CreateActionButton(window_, IDOK, L"Save");
        editor_cancel_button_ = CreateActionButton(window_, IDCANCEL, L"Cancel");

        EnableSelectAll(inline_note_.title_edit);
        EnableSelectAll(inline_note_.body_edit);
        EnableSelectAll(inline_note_.url_edit);
        EnableEditorField(inline_note_.body_edit);
        EnableEditorField(inline_note_.title_edit);
        EnableEditorField(inline_note_.url_edit);
        EnableEditorField(inline_note_.kind_combo);
        EnableEditorField(inline_note_.save_button);
        EnableEditorField(editor_cancel_button_);
        for (HWND control : editor_focus_order_) {
            SetFont(control, regular_font_);
        }
        LayoutEditorControls();
        UpdateInlineNoteKind();
        UpdateEditorHint();
        const HWND initial_focus = inline_note_.kind == Note::Kind::Link
                                       ? inline_note_.url_edit
                                       : inline_note_.body_edit;
        ActivateAndFocus(window_, initial_focus);
        SendMessageW(initial_focus, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        return;
    }

    const wchar_t* cue = editor_kind_ == LauncherEditorKind::Reminder
                             ? L"reminder"
                             : editor_kind_ == LauncherEditorKind::TodoGroup
                                   ? L"todo group name"
                                   : editor_kind_ == LauncherEditorKind::Streak ? L"habit" : L"task";
    inline_quick_.first_edit = CreateTextInput(window_, 611, inline_quick_.first);
    SendMessageW(inline_quick_.first_edit, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(cue));
    if (editor_kind_ == LauncherEditorKind::Reminder) {
        SYSTEMTIME local_time{};
        if (!ParseLocalReminderDisplay(inline_quick_.second, local_time)) {
            GetLocalTime(&local_time);
        }
        inline_quick_.second_picker = CreateDateTimeInput(window_, 612, local_time);
    }
    if (editor_kind_ == LauncherEditorKind::Streak) {
        inline_quick_.frequency_combo = CreateChoiceInput(window_, 613);
        SendMessageW(inline_quick_.frequency_combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Daily"));
        SendMessageW(inline_quick_.frequency_combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"Weekdays"));
        SendMessageW(inline_quick_.frequency_combo, CB_SETCURSEL,
                     static_cast<WPARAM>(inline_quick_.frequency == 2 ? 1 : 0), 0);
    }
    inline_quick_.save_button = CreateActionButton(window_, IDOK, L"Save");
    editor_cancel_button_ = CreateActionButton(window_, IDCANCEL, L"Cancel");

    EnableSelectAll(inline_quick_.first_edit);
    EnableEditorField(inline_quick_.first_edit);
    if (inline_quick_.second_edit != nullptr) {
        EnableSelectAll(inline_quick_.second_edit);
        EnableEditorField(inline_quick_.second_edit);
    }
    if (inline_quick_.second_picker != nullptr) {
        EnableEditorField(inline_quick_.second_picker);
    }
    if (inline_quick_.frequency_combo != nullptr) {
        EnableEditorField(inline_quick_.frequency_combo);
    }
    EnableEditorField(inline_quick_.save_button);
    EnableEditorField(editor_cancel_button_);
    for (HWND control : editor_focus_order_) {
        SetFont(control, regular_font_);
    }
    LayoutEditorControls();
    UpdateEditorHint();
    ActivateAndFocus(window_, inline_quick_.first_edit);
    SendMessageW(inline_quick_.first_edit, EM_SETSEL, 0, -1);
}

void LauncherWindow::FocusNextEditorField(bool backwards) {
    if (editor_focus_order_.empty()) {
        return;
    }
    const HWND current = GetFocus();
    auto it = std::find(editor_focus_order_.begin(), editor_focus_order_.end(), current);
    size_t index = it == editor_focus_order_.end()
                       ? 0
                       : static_cast<size_t>(std::distance(editor_focus_order_.begin(), it));
    if (backwards) {
        index = index == 0 ? editor_focus_order_.size() - 1 : index - 1;
    } else {
        index = (index + 1) % editor_focus_order_.size();
    }
    for (size_t attempt = 0; attempt < editor_focus_order_.size(); ++attempt) {
        HWND target = editor_focus_order_[index];
        if (target != nullptr && IsWindowVisible(target) && IsWindowEnabled(target)) {
            SetFocus(target);
            return;
        }
        if (backwards) {
            index = index == 0 ? editor_focus_order_.size() - 1 : index - 1;
        } else {
            index = (index + 1) % editor_focus_order_.size();
        }
    }
}

void LauncherWindow::BeginNewNoteEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Note;
    inline_note_ = NoteDraft{};
    inline_note_.body = captured_text_;
    view_title_ = L"twosemi :: note";
    CreateEditorControls();
}

void LauncherWindow::BeginNewSecretEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Note;
    inline_note_ = NoteDraft{};
    inline_note_.kind = Note::Kind::Secret;
    view_title_ = L"twosemi :: secret";
    CreateEditorControls();
}

void LauncherWindow::BeginNewLinkEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Note;
    inline_note_ = NoteDraft{};
    inline_note_.kind = Note::Kind::Link;
    if (IsHttpUrl(captured_text_)) {
        inline_note_.url = Trim(captured_text_);
    }
    view_title_ = L"twosemi :: link";
    CreateEditorControls();
}

void LauncherWindow::BeginEditNoteEditor(const Note& note) {
    if (note.kind == Note::Kind::Secret && !note.secret_available) {
        MessageBoxW(window_,
                    L"This secret note could not be unlocked for the current Windows user.",
                    L"TwoSemi", MB_OK | MB_ICONWARNING);
        return;
    }
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Note;
    inline_note_existing_ = note;
    inline_note_.title = note.title;
    inline_note_.body = note.body;
    inline_note_.url = note.url;
    inline_note_.kind = note.kind;
    view_title_ = L"twosemi :: note";
    CreateEditorControls();
}

void LauncherWindow::BeginNewReminderEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Reminder;
    inline_quick_ = QuickDraft{QuickEditorKind::Reminder, captured_text_, DefaultReminderTime()};
    view_title_ = L"twosemi :: reminder";
    CreateEditorControls();
}

void LauncherWindow::BeginEditReminderEditor(const Reminder& reminder) {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Reminder;
    inline_reminder_existing_ = reminder;
    inline_quick_ = QuickDraft{QuickEditorKind::Reminder, reminder.text,
                               FormatReminderTime(reminder.due_at_utc)};
    view_title_ = L"twosemi :: reminder";
    CreateEditorControls();
}

void LauncherWindow::BeginNewTodoGroupEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::TodoGroup;
    inline_quick_ = QuickDraft{QuickEditorKind::TodoGroup, {}, {}};
    view_title_ = L"twosemi :: todo group";
    CreateEditorControls();
}

void LauncherWindow::BeginEditTodoGroupEditor(const TodoGroup& group) {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::TodoGroup;
    inline_todo_group_existing_ = group;
    inline_quick_ = QuickDraft{QuickEditorKind::TodoGroup, group.title, {}};
    view_title_ = L"twosemi :: todo group";
    CreateEditorControls();
}

void LauncherWindow::BeginNewTodoTaskEditor() {
    if (!todo_group_view_ || active_todo_group_ >= todo_groups_.size()) {
        return;
    }
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::TodoTask;
    inline_todo_group_existing_ = todo_groups_[active_todo_group_];
    inline_quick_ = QuickDraft{QuickEditorKind::TodoTask, {}, {}};
    view_title_ = L"twosemi :: task";
    CreateEditorControls();
}

void LauncherWindow::BeginEditTodoTaskEditor(const TodoGroup& group, const TodoItem& task) {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::TodoTask;
    inline_todo_group_existing_ = group;
    inline_todo_task_id_ = task.id;
    inline_quick_ = QuickDraft{QuickEditorKind::TodoTask, task.text, {}};
    view_title_ = L"twosemi :: task";
    CreateEditorControls();
}

void LauncherWindow::BeginNewStreakEditor() {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Streak;
    inline_quick_ = QuickDraft{QuickEditorKind::Streak, {}, {}};
    view_title_ = L"twosemi :: streak";
    CreateEditorControls();
}

void LauncherWindow::BeginEditStreakEditor(const Streak& streak) {
    editor_return_title_ = view_title_;
    editor_kind_ = LauncherEditorKind::Streak;
    inline_streak_existing_ = streak;
    inline_quick_ = QuickDraft{QuickEditorKind::Streak, streak.title, {}};
    inline_quick_.frequency = streak.frequency;
    view_title_ = L"twosemi :: streak";
    CreateEditorControls();
}

void LauncherWindow::FinishEditor() {
    const std::wstring return_title = editor_return_title_;
    DestroyEditorControls();
    editor_kind_ = LauncherEditorKind::None;
    view_title_ = return_title;
    ShowWindow(search_, SW_SHOW);
    ShowWindow(results_, SW_SHOW);
    ShowWindow(hint_, SW_SHOW);
    Refresh();
    ActivateAndFocus(window_, search_);
}

void LauncherWindow::CancelEditor() {
    if (editor_kind_ != LauncherEditorKind::None) {
        FinishEditor();
    }
}

void LauncherWindow::CloseEditor() {
    if (editor_kind_ == LauncherEditorKind::Note) {
        const bool link = inline_note_.kind == Note::Kind::Link ||
                          (inline_note_.kind_combo != nullptr &&
                           SendMessageW(inline_note_.kind_combo, CB_GETCURSEL, 0, 0) == 1);
        const bool body_empty = inline_note_.body_edit != nullptr &&
                                Trim(WindowText(inline_note_.body_edit)).empty();
        const bool url_empty = !link || inline_note_.url_edit == nullptr ||
                               Trim(WindowText(inline_note_.url_edit)).empty();
        if (body_empty && url_empty) {
            CancelEditor();
        } else {
            CommitEditor();
        }
    } else {
        CancelEditor();
    }
}

void LauncherWindow::CommitEditor() {
    if (editor_kind_ == LauncherEditorKind::None) {
        return;
    }

    if (editor_kind_ == LauncherEditorKind::Note) {
        inline_note_.title = Trim(WindowText(inline_note_.title_edit));
        inline_note_.body = WindowText(inline_note_.body_edit);
        inline_note_.url = Trim(WindowText(inline_note_.url_edit));
        const LRESULT selected = SendMessageW(inline_note_.kind_combo, CB_GETCURSEL, 0, 0);
        inline_note_.kind = selected == 1
                                ? Note::Kind::Link
                                : selected == 2 ? Note::Kind::Secret : Note::Kind::Text;
        if (inline_note_.kind == Note::Kind::Link && !IsHttpUrl(inline_note_.url)) {
            MessageBoxW(window_, L"Enter a valid http:// or https:// URL.", L"TwoSemi",
                        MB_OK | MB_ICONINFORMATION);
            SetFocus(inline_note_.url_edit);
            return;
        }
        if (inline_note_.kind != Note::Kind::Link && Trim(inline_note_.body).empty()) {
            MessageBoxW(window_, L"Write something in the note first.", L"TwoSemi",
                        MB_OK | MB_ICONINFORMATION);
            SetFocus(inline_note_.body_edit);
            return;
        }
        Note note = inline_note_existing_.value_or(Note{});
        if (note.id.empty()) {
            note.id = NewId();
        }
        note.kind = inline_note_.kind;
        note.title = inline_note_.title.empty()
                         ? (note.kind == Note::Kind::Secret
                                ? L"Secret note"
                                : note.kind == Note::Kind::Link ? LinkHost(inline_note_.url)
                                                                : MakeNoteTitle(inline_note_.body))
                         : inline_note_.title;
        note.body = inline_note_.body;
        note.url = note.kind == Note::Kind::Link ? inline_note_.url : L"";
        if (!app_->database_.SaveNote(note)) {
            app_->ShowDatabaseError();
        }
        FinishEditor();
        return;
    }

    inline_quick_.first = Trim(WindowText(inline_quick_.first_edit));
    if (inline_quick_.first.empty()) {
        MessageBoxW(window_, L"Enter a value first.", L"TwoSemi", MB_OK | MB_ICONINFORMATION);
        SetFocus(inline_quick_.first_edit);
        return;
    }

    if (editor_kind_ == LauncherEditorKind::Reminder) {
        if (inline_quick_.second_picker != nullptr) {
            SYSTEMTIME local_time{};
            if (!ReadDateTimeInput(inline_quick_.second_picker, local_time)) {
                MessageBoxW(window_, L"Choose a valid reminder time.", L"TwoSemi",
                            MB_OK | MB_ICONINFORMATION);
                SetFocus(inline_quick_.second_picker);
                return;
            }
            inline_quick_.second = FormatLocalSystemTime(local_time);
        } else {
            inline_quick_.second = Trim(WindowText(inline_quick_.second_edit));
        }
        std::wstring utc_value;
        if (!ParseLocalReminderTime(inline_quick_.second, utc_value)) {
            MessageBoxW(window_, L"Use the format yyyy-mm-dd hh:mm.", L"TwoSemi",
                        MB_OK | MB_ICONINFORMATION);
            SetFocus(inline_quick_.second_edit);
            return;
        }
        Reminder reminder = inline_reminder_existing_.value_or(Reminder{});
        if (reminder.id.empty()) {
            reminder.id = NewId();
            reminder.created_at_utc = NowUtcIso();
        }
        reminder.text = inline_quick_.first;
        reminder.due_at_utc = utc_value;
        reminder.snoozed_until_utc.clear();
        if (!app_->database_.SaveReminder(reminder)) {
            app_->ShowDatabaseError();
        }
        FinishEditor();
        return;
    }

    if (editor_kind_ == LauncherEditorKind::Streak) {
        Streak streak = inline_streak_existing_.value_or(Streak{});
        const bool is_new = streak.id.empty();
        const std::wstring now = NowUtcIso();
        if (is_new) {
            streak.id = NewId();
            streak.created_at_utc = now;
        }
        streak.title = inline_quick_.first;
        streak.frequency = SendMessageW(inline_quick_.frequency_combo, CB_GETCURSEL, 0, 0) == 1 ? 2 : 1;
        streak.updated_at_utc = now;
        if (!app_->database_.SaveStreak(streak)) {
            app_->ShowDatabaseError();
        }
        FinishEditor();
        return;
    }

    if (editor_kind_ == LauncherEditorKind::TodoGroup) {
        TodoGroup group = inline_todo_group_existing_.value_or(TodoGroup{});
        const bool is_new = group.id.empty();
        if (is_new) {
            group.id = NewId();
            group.created_at_utc = NowUtcIso();
        }
        group.title = inline_quick_.first;
        group.updated_at_utc = NowUtcIso();
        if (!app_->database_.SaveTodoGroup(group)) {
            app_->ShowDatabaseError();
        }
        const std::wstring group_id = group.id;
        FinishEditor();
        if (is_new) {
            OpenTodoGroup(group_id);
        }
        return;
    }

    if (editor_kind_ == LauncherEditorKind::TodoTask && inline_todo_group_existing_.has_value()) {
        TodoGroup group = *inline_todo_group_existing_;
        const std::wstring now = NowUtcIso();
        if (inline_todo_task_id_.empty()) {
            group.items.push_back(TodoItem{NewId(), group.id, inline_quick_.first, false, now, now});
        } else {
            const auto item = std::find_if(group.items.begin(), group.items.end(), [&](const TodoItem& candidate) {
                return candidate.id == inline_todo_task_id_;
            });
            if (item == group.items.end()) {
                FinishEditor();
                return;
            }
            item->text = inline_quick_.first;
            item->updated_at_utc = now;
        }
        group.updated_at_utc = now;
        if (!app_->database_.SaveTodoGroup(group)) {
            app_->ShowDatabaseError();
        }
        FinishEditor();
    }
}
