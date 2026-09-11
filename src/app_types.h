#pragma once

#include <string>
#include <vector>

namespace twosemi::model {

struct Note {
    enum class Kind {
        Text = 0,
        Secret = 1,
        Link = 2,
    };

    std::wstring id;
    std::wstring title;
    std::wstring body;
    std::wstring url;
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

} // namespace twosemi::model
