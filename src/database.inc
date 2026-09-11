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
                     "Url TEXT NOT NULL DEFAULT '',"
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
                if (!HasColumn(L"url") &&
                    !Execute(legacy_schema_
                                 ? "ALTER TABLE notes ADD COLUMN url TEXT NOT NULL DEFAULT '';"
                                 : "ALTER TABLE notes ADD COLUMN Url TEXT NOT NULL DEFAULT '';")) {
                    sqlite3_close(database_);
                    database_ = nullptr;
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
                              ? "SELECT id, title, body, url, kind FROM notes ORDER BY updated_at DESC;"
                              : "SELECT Id, Title, Body, Url, Kind FROM notes ORDER BY UpdatedAtUtc DESC;";
        if (sqlite3_prepare_v2(database_, sql, -1, &statement, nullptr) != SQLITE_OK) {
            SetLastError();
            return notes;
        }

        while (sqlite3_step(statement) == SQLITE_ROW) {
            Note note{
                WideFromSqlite16(sqlite3_column_text16(statement, 0), sqlite3_column_bytes16(statement, 0)),
                WideFromSqlite16(sqlite3_column_text16(statement, 1), sqlite3_column_bytes16(statement, 1)),
                WideFromSqlite16(sqlite3_column_text16(statement, 2), sqlite3_column_bytes16(statement, 2)),
                WideFromSqlite16(sqlite3_column_text16(statement, 3), sqlite3_column_bytes16(statement, 3)),
                sqlite3_column_int(statement, 4) == 1
                    ? Note::Kind::Secret
                    : sqlite3_column_int(statement, 4) == 2 ? Note::Kind::Link : Note::Kind::Text,
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
                              ? "INSERT INTO notes (id, title, body, url, kind, created_at, updated_at) "
                                "VALUES (?, ?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
                                "ON CONFLICT(id) DO UPDATE SET title = excluded.title, body = excluded.body, "
                                "url = excluded.url, kind = excluded.kind, updated_at = strftime('%s','now');"
                              : "INSERT INTO notes (Id, Title, Body, Url, Kind, CreatedAtUtc, UpdatedAtUtc, SourceApplication) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, NULL) "
                                "ON CONFLICT(Id) DO UPDATE SET Title = excluded.Title, Body = excluded.Body, "
                                "Url = excluded.Url, Kind = excluded.Kind, UpdatedAtUtc = excluded.UpdatedAtUtc;";
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
        sqlite3_bind_text16(statement, 4, note.url.c_str(), -1, SQLITE_TRANSIENT);
        const std::wstring now = NowUtcIso();
        if (legacy_schema_) {
            sqlite3_bind_int(statement, 5,
                            note.kind == Note::Kind::Secret ? 1 : note.kind == Note::Kind::Link ? 2 : 0);
        } else {
            sqlite3_bind_int(statement, 5,
                            note.kind == Note::Kind::Secret ? 1 : note.kind == Note::Kind::Link ? 2 : 0);
            sqlite3_bind_text16(statement, 6, now.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text16(statement, 7, now.c_str(), -1, SQLITE_TRANSIENT);
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
