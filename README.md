# TwoSemi C++ prototype

Project page: <https://anmism.github.io/twosemi/>
Source: <https://github.com/anmism/twosemi>

This is the first native Windows milestone described in `project.md`:

- tray icon and single-instance behavior
- global `Ctrl` + `;` twice activation within 650 ms
- keyboard-first mixed launcher with search and arrow-key navigation
- SQLite-backed notes stored at `%LOCALAPPDATA%\TwoSemi\twosemi.db`
- note creation with automatic titles
- text and DPAPI-protected secret notes
- reminders with completion, editing, deletion, and one-hour snooze
- todo groups with task creation, editing, deletion, and completion
- daily and weekday streaks with current/best run tracking
- timed Focus Mode sessions with optional domain blocking
- saved AI chat threads with Gemini conversations
- Enter-to-fill into the previously active window using Unicode keyboard input

## Build

From a Visual Studio Developer PowerShell or a shell with the Visual Studio C++ tools available:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run `build\Release\TwoSemi.exe`. The app starts quietly in the system tray. Use `Ctrl` + `;` twice to open the launcher.

## Package

Create the install tree and a portable ZIP package:

```powershell
cmake --install build --config Release --prefix package\TwoSemi
New-Item -ItemType Directory -Force dist | Out-Null
cpack --config build\CPackConfig.cmake -G ZIP -B dist
```

The ZIP is written to `dist\TwoSemi-0.1.0-win64.zip`. It contains `TwoSemi.exe` and its `assets` folder.

Create a normal Windows installer with Start Menu, Desktop shortcut, and an uninstaller:

```powershell
cpack --config build\CPackConfig.cmake -G NSIS -B dist
```

This creates `dist\TwoSemi-0.1.0-win64.exe`. During installation, the user is asked whether to create a desktop shortcut. A Start Menu shortcut is also installed.

TwoSemi does not currently start automatically with Windows. The installed app and desktop shortcut remain available after a Windows restart, and notes, chats, settings, and encrypted model keys are stored under `%LOCALAPPDATA%\TwoSemi`.

The installer does not remove `%LOCALAPPDATA%\TwoSemi`, so notes, chats, settings, and encrypted model keys remain available after uninstall or upgrade.

GitHub Actions builds the Windows packages on pushes, pull requests, and `v*` tags. A tag such as `v0.1.0` publishes the installer and ZIP as a GitHub Release. The Pages workflow publishes the plain HTML site from `docs/`; enable **Settings > Pages > Source: GitHub Actions** once in the repository.

The launcher shows creation actions first, followed by view actions and a mixed list of notes, reminders, todo groups, streaks, and chats. `View notes` and `View reminders` open focused lists for those item types. Select a todo group with `Enter` to open its tasks; use `Alt` + `Left` to return to the launcher.

Keyboard actions:

- `Enter` opens or executes the selected item; it fills a selected note and checks tasks or streaks
- `Ctrl` + `Enter` edits a supported saved item
- `Ctrl` + `Enter` sends a chat message
- `Configure models` opens the AI model manager
- `Delete` removes the selected saved item
- `F2` edits the selected saved item
- `Alt` + `Left` returns from a focused list to the launcher
- `Escape` closes the launcher or current editor
- `Tab` switches between search and results
- `Escape` closes the launcher

The prototype uses the Windows SDK's `winsqlite3` library, so it does not bundle a separate SQLite DLL.

## Source layout

The native code is split into small reusable pieces:

- `src/app_types.h` contains the shared data models.
- `src/ui_theme.h/.cpp` contains colors, dimensions, and font setup.
- `src/ui_controls.h/.cpp` contains reusable text fields, choice fields, and action buttons.
- `src/database.inc` contains SQLite storage.
- `src/modal_editors.inc`, `src/chat.inc`, and `src/model_manager.inc` contain their feature editors.
- `src/launcher_window_*.inc` and `src/app_controller.inc` contain the launcher and application behavior.

`main.cpp` keeps the shared Windows setup and assembles these internal implementation units.

## Focus Mode

Focus settings are saved in SQLite. Enter one domain per line, for example `youtube.com` or `reddit.com`.
When a session starts, TwoSemi adds marked rules to the Windows hosts file and may request administrator permission.
Stopping or completing the session removes only TwoSemi's marked rules; the rest of the hosts file is preserved.

## AI chat

Chat threads and messages are saved in SQLite. The native build reuses an encrypted Gemini profile from the older TwoSemi database when one exists. For a new setup, provide `TWOSEMI_GEMINI_API_KEY`; `TWOSEMI_GEMINI_MODEL` is optional and defaults to `gemini-3.6-flash`.

The model manager can add, edit, enable, disable, choose a default, and delete Gemini profiles. Existing API keys stay encrypted with Windows DPAPI and are shown only as `key saved`.
