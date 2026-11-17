# QtGuiTestingMCP

[![CI](https://github.com/chrispolewczuk/QtGuiTestingMCP/actions/workflows/ci.yml/badge.svg)](https://github.com/chrispolewczuk/QtGuiTestingMCP/actions/workflows/ci.yml)

QtGuiTestingMCP is a localhost MCP bridge for testing a running C++ Qt GUI
application headlessly. An MCP-enabled agent can discover UI elements, interact
with named controls, detect popup errors, inspect Qt logs, capture screenshots,
and run readable UI-test scripts.

Local validation is performed on Windows. CI is configured to build and test
the project on Windows, Ubuntu, and macOS for pushes, pull requests, and manual
workflow dispatches.

The bridge runs inside the Qt GUI process, so widget operations remain on the
GUI thread. The Python process is only the MCP adapter and script runner.

## Quick start

Build the Debug demo, start it headlessly, and register QtGuiTestingMCP with your
agent:

```powershell
cmake --build build
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
python scripts/setup_mcp.py --codex --write
```

Replace `--codex` with `--claude`, `--opencode`, or `--cursor`. Use `--all` to
configure all four clients. The setup command previews changes unless
`--write` is supplied and creates a backup before changing an existing file.
See [docs/SETUP.md](docs/SETUP.md) for client paths and advanced options.

## Features

- Qt UI-tree discovery using stable `objectName` selectors for QWidget and QWindow
- Button activation and deterministic text entry
- Menu and toolbar `QAction` discovery and semantic activation
- Keyboard press/release and mouse move/click/double-click event simulation
- Popup error detection for visible `QMessageBox` instances
- Rich UI metadata: geometry, focus, accessibility name, text, visibility,
  active/exposed state, and widget enabled state
- Application health/status inspection
- Qt debug/info/warning/critical log capture
- Qt event-loop settling for asynchronous UI updates
- PNG screenshots, automatic per-step screenshots, and file saving
- Human-readable UI-test scripts without quotes or JSON structures
- Natural-language-to-script workflow through MCP tool descriptions and prompts
- Offscreen execution through `QT_QPA_PLATFORM=offscreen`
- QtTest integration coverage through CTest
- GitHub Actions validation on Windows, Ubuntu, and macOS

## Architecture

```text
AI agent / MCP client  <── MCP stdio ──>  Python MCP adapter
                                              │ localhost TCP
                                              ▼
                                      C++ Qt application
                                      AgentBridge / GUI thread
```

The C++ application must be started separately. This project intentionally
does not launch or terminate the application on behalf of the MCP server.

## Requirements

- Qt 6 with Gui, Widgets, and Network components
- C++17 compiler supported by the selected Qt kit
- CMake 3.21 or newer
- Ninja or another supported CMake generator
- Python 3.11 or newer with the MCP SDK dependency from `requirements.txt`
- A Debug build to enable the bridge, or an explicit
  `QT_GUI_TESTING_MCP_ENABLE_BRIDGE=ON` release-package option

The project version is maintained in [`VERSION`](VERSION) and follows
Semantic Versioning.

On Windows, use the Visual Studio 2022 generator so the MSVC compiler and
Windows SDK are selected explicitly. Ninja is also supported from a Visual
Studio Developer PowerShell with the MSVC environment initialized.

## Build

Replace the Qt path with the kit installed on your machine:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/msvc2022_64 `
  -DCMAKE_BUILD_TYPE=Debug `
  -DBUILD_TESTING=ON
cmake --build build --config Debug
```

The bridge is enabled by the project-owned `QT_GUI_TESTING_MCP_ENABLED` macro in
Debug builds. Release packaging enables it explicitly with the project option;
Qt's internal `QT_DEBUG` macro is not overridden.

## Release binaries

Tagged releases build self-contained archives for Windows, Linux, and macOS.
The packages contain the demo executable with its deployed Qt runtime, the
Python MCP adapter, setup utility, documentation, and license. Download the
archive matching the target platform from the GitHub release instead of
installing Qt just to run the demo.

The release workflow validates the tag against `VERSION`, builds each platform
package, and attaches all archives to the GitHub release.

## Run the demo headlessly

```powershell
# Set this to the Qt kit used by CMake. The Qt bin directory provides the
# runtime DLLs and the plugins directory provides qoffscreen.dll.
$qtRoot = "C:\Qt\6.x.x\msvc2022_64"
$env:PATH = "$qtRoot\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qtRoot\plugins"
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
```

For a distributable Windows directory, use Qt's deployment tool after the
build instead of relying on a developer environment:

```powershell
& "$qtRoot\bin\windeployqt.exe" `
  --dir build\deploy `
  build\qtgui-testing-mcp.exe
```

Run `build\deploy\qtgui-testing-mcp.exe` from that deployed directory.

The demo exposes these selectors:

| Object name | Qt class | Purpose |
|---|---|---|
| `mainWindow` | `QWidget` | Main window and screenshot target |
| `nameEdit` | `QLineEdit` | Text input |
| `messageEdit` | `QTextEdit` | Output text |
| `helloButton` | `QPushButton` | Button action |

## Configure the MCP client

Install the Python dependency:

```powershell
python -m pip install -r requirements.txt
```

Register the server with an MCP client using an absolute path:

```json
{
  "mcpServers": {
    "qt-gui-testing": {
      "command": "python",
      "args": ["/absolute/path/to/QtGuiTestingMCP/src/qtgui_mcp_server.py"]
    }
  }
}
```

Restart or reload the MCP client after changing its configuration. The server
uses stdio for MCP and connects to the Qt process at `127.0.0.1:9999`.

### One-command setup

QtGuiTestingMCP includes a safe setup command for common MCP clients. It previews
the exact file changes by default and creates a timestamped backup when
`--write` updates an existing file:

```powershell
python scripts/setup_mcp.py --client claude
python scripts/setup_mcp.py --client claude --write
```

Configure Claude Desktop, Codex, OpenCode, and Cursor together:

```powershell
python scripts/setup_mcp.py --all --write
```

Use `--scope global` for a user-level Codex, OpenCode, or Cursor installation,
`--name` to choose another server key, and `--path` for a non-standard config
location. Existing unrelated servers are preserved. Use `--force` only when an
entry with the selected name already exists and should be replaced. See
[docs/SETUP.md](docs/SETUP.md) for platform paths, format details, and the
complete option reference.

## Available MCP tools

| Tool | Purpose |
|---|---|
| `show_ui_test_commands` | Show the complete script command reference |
| `get_widget_tree` | Discover visible QWidget and QWindow elements and selectors |
| `get_app_status` | Check bridge, window, and log-buffer health |
| `wait_for_idle` | Process queued Qt events for a bounded interval |
| `get_recent_logs` | Retrieve captured Qt diagnostic messages |
| `clear_recent_logs` | Clear the in-memory log buffer |
| `click_widget` | Activate a `QAbstractButton` by `objectName` |
| `activate_action` | Trigger a menu or toolbar `QAction` by `objectName` |
| `type_text` | Set a `QLineEdit` or `QTextEdit` value |
| `press_key` | Send a keyboard press/release pair |
| `mouse_event` | Send mouse move, click, or double-click events |
| `get_popup_errors` | Inspect visible `QMessageBox` dialogs |
| `take_screenshot` | Return a PNG image to the MCP client |
| `run_test_script` | Run a bounded line-oriented UI test |

The MCP prompt `create_ui_test_script` helps clients that expose MCP prompts to
translate a natural-language request into a test script. The MCP server does
not contain a second language model; the agent performs that translation.

## Natural-language workflow

Users can describe a test normally:

```text
Test the greeting flow, enter Ada Lovelace, click Say hello, verify the
message, check for popup errors, and save a screenshot to results/greeting.png.
```

The agent should discover selectors, show or summarize the generated script,
run it, and report each step plus the final UI state.

## UI-test scripts

Scripts use one command per line. Quotes and JSON are not required:

```text
# Greeting workflow
start ui test
status
wait idle 250
type nameEdit Ada Lovelace
click helloButton
assert contains messageEdit Hello, Ada Lovelace!
assert no-errors
logs
save screenshot to results/greeting.png
end ui test
```

Available commands include:

```text
start ui test
end ui test
show ui test commands
tree
status
wait idle 250
logs
clear logs
screenshot mainWindow
save screenshot to results/current.png
click helloButton
type nameEdit Ada Lovelace
key nameEdit End
key nameEdit Ctrl+A
mouse click helloButton
mouse double-click helloButton
activate openAction
wait 250
wait-text statusLabel Ready
assert visible helloButton true
assert enabled helloButton true
assert text messageEdit Hello, Ada Lovelace!
assert contains messageEdit Lovelace
assert popup critical Connection refused
assert no-errors
```

Text consumes the rest of a line, so spaces do not require quoting. A PNG is
captured automatically after every command. `save screenshot to PATH` writes
the most recent buffer; parent directories must already exist.

Scripts stop on the first failed step by default, are limited to 100 steps, and
never execute arbitrary Python, shell commands, or application code. See
[docs/USER_GUIDE.md](docs/USER_GUIDE.md) for the full guide.

## Application integration

Include the bridge in the application entry point and construct it after
`QApplication`:

```cpp
#ifdef QT_GUI_TESTING_MCP_ENABLED
#include "AgentBridge.h"
#endif

QApplication app(argc, argv);
#ifdef QT_GUI_TESTING_MCP_ENABLED
AgentBridge bridge(9999);
#endif
```

Give agent-facing controls stable, unique names:

```cpp
loginButton->setObjectName("loginButton");
usernameEdit->setObjectName("usernameEdit");
statusLabel->setObjectName("statusLabel");
```

## Tests

Run the QtTest integration suite:

```powershell
ctest --test-dir build --output-on-failure
```

The suite uses the offscreen platform and exercises the real localhost JSON/TCP
bridge, including widget discovery, interaction, screenshots, popup detection,
menu and toolbar actions, QWindow input, validation errors, status, logs, and
event-loop settling. Python tests cover setup configuration, script parsing,
action lookup, error propagation, and automatic screenshot reporting.

The GitHub Actions workflow runs these checks on Windows, Ubuntu, and macOS.
Pushes and pull requests start the matrix automatically; maintainers can also
run it from the workflow's **Run workflow** control.

## Troubleshooting

### The MCP tools cannot connect

Confirm that the Debug Qt application is already running and listening on
`127.0.0.1:9999`. Check `get_app_status` and verify that another process is not
using the port.

### The application does not appear in screenshots

Set `QT_QPA_PLATFORM=offscreen` before launching the application, call
`wait_for_idle`, and ensure at least one top-level QWidget or QWindow is shown.
On Windows, also ensure the selected Qt kit's `bin` directory is on `PATH` and
its `plugins` directory is available through `QT_PLUGIN_PATH`; otherwise Qt
may report that it could not initialize the `offscreen` platform plugin.

### A widget cannot be found

Call `get_widget_tree` and use the exact `objectName`. Display text is not a
selector, and unnamed widgets are intentionally not reliable targets.

### Errors are missing

`get_popup_errors` recognizes standard `QMessageBox` dialogs. Custom dialogs
should be inspected with `get_widget_tree`; application diagnostics emitted via
`qDebug`, `qInfo`, `qWarning`, or `qCritical` are available through
`get_recent_logs`.

### The C++ build cannot find `type_traits` or `kernel32.lib`

The compiler is running without the MSVC/Windows SDK environment. Configure and
build from a Visual Studio Developer PowerShell, then rerun CMake.

## Security and limitations

The bridge binds only to loopback but still grants local clients control of the
application. Keep it disabled in production builds. Do not expose the port
remotely without authentication and authorization.

Current limitations include:

- application process launch/stop is managed externally;
- popup classification is automatic for `QMessageBox` only;
- the log buffer is bounded in memory and is not a persistent log store;
- screenshots are PNG images saved by the MCP server process; QWindow captures
  use Qt's associated screen surface. Generic QWindow captures may be
  unavailable with the offscreen QPA plugin because it has no composited native
  screen surface; inspection and event delivery still work.

## Development

- [docs/USER_GUIDE.md](docs/USER_GUIDE.md) — user and agent workflow
- [docs/AGENT_USAGE.md](docs/AGENT_USAGE.md) — runtime agent guidance
- [docs/SETUP.md](docs/SETUP.md) — setup command and client configuration
- [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) — project layout and development
- [docs/RELEASING.md](docs/RELEASING.md) — versioning and release workflow
- [CONTRIBUTING.md](CONTRIBUTING.md) — contribution and validation conventions
- [LICENSE](LICENSE) — MIT license
