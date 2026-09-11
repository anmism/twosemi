class TwoSemiApp;

enum class LauncherItemKind {
    NewNote,
    NewLink,
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
    ViewLinks,
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

enum class LauncherEditorKind {
    None,
    Note,
    Reminder,
    TodoGroup,
    TodoTask,
    Streak,
};

struct LauncherItem {
    LauncherItemKind kind;
    std::wstring title;
    std::wstring detail;
    size_t index = 0;
};

constexpr wchar_t kLauncherThisProperty[] = L"TwoSemi.Launcher.This";
constexpr wchar_t kLauncherOriginalProperty[] = L"TwoSemi.Launcher.Original";
constexpr wchar_t kLauncherInlineFieldProperty[] = L"TwoSemi.Launcher.InlineField";

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
    void OpenLinks();
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
    void UpdateEditorHint();
    void BeginNewNoteEditor();
    void BeginNewLinkEditor();
    void BeginEditNoteEditor(const Note& note);
    void BeginNewReminderEditor();
    void BeginEditReminderEditor(const Reminder& reminder);
    void BeginNewTodoGroupEditor();
    void BeginEditTodoGroupEditor(const TodoGroup& group);
    void BeginNewTodoTaskEditor();
    void BeginEditTodoTaskEditor(const TodoGroup& group, const TodoItem& task);
    void BeginNewStreakEditor();
    void BeginEditStreakEditor(const Streak& streak);
    void CreateEditorControls();
    void LayoutEditorControls();
    void UpdateInlineNoteKind();
    void EnableEditorField(HWND control);
    void FocusNextEditorField(bool backwards);
    void CommitEditor();
    void CancelEditor();
    void CloseEditor();
    void FinishEditor();
    void DestroyEditorControls();
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
    bool link_view_ = false;
    bool reminder_view_ = false;
    bool streak_view_ = false;
    bool chat_view_ = false;
    size_t active_todo_group_ = 0;
    std::wstring view_title_ = L"twosemi :: launcher";
    LauncherEditorKind editor_kind_ = LauncherEditorKind::None;
    std::wstring editor_return_title_;
    NoteDraft inline_note_;
    QuickDraft inline_quick_{QuickEditorKind::Reminder, {}, {}};
    std::optional<Note> inline_note_existing_;
    std::optional<Reminder> inline_reminder_existing_;
    std::optional<TodoGroup> inline_todo_group_existing_;
    std::optional<Streak> inline_streak_existing_;
    std::wstring inline_todo_task_id_;
    HWND editor_cancel_button_ = nullptr;
    std::vector<HWND> editor_focus_order_;
};
