# Qt GUI MCP user guide

Qt GUI MCP lets an agent test a running Qt application through a small
localhost bridge. The user describes the behavior to test, the agent discovers
the live UI, creates a readable test script, runs it, and reports the state and
evidence collected along the way.

Local validation is performed on Windows. The repository CI workflow also
builds and tests the project on Ubuntu and macOS.

## What the user needs to do

Start the application in a Debug build with the offscreen Qt platform:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
```

Start the MCP server through the MCP client configuration:

```json
{
  "mcpServers": {
    "qt-gui-testing": {
      "command": "python",
      "args": ["/absolute/path/to/src/qtgui_mcp_server.py"]
    }
  }
}
```

The Qt application and MCP server run as separate processes. The application
owns the widgets; the MCP server translates agent tool calls into localhost
commands.

## Natural-language usage

The preferred user experience is to describe the test normally:

```text
Test the greeting flow. Enter Ada Lovelace, click Say hello, verify that the
message says Hello, Ada Lovelace!, check for popup errors, and save a screenshot
to results/greeting.png.
```

The agent should:

1. Call `get_widget_tree` to discover live `objectName` values.
2. Translate the request into the line-oriented script syntax.
3. Show or summarize the generated script when the request is ambiguous or
   destructive.
4. Call `run_test_script`.
5. Report per-step results, final UI state, popup errors, and saved evidence.

The MCP server does not contain a second language model. Natural-language
interpretation belongs to the agent; the server provides the safe command
language and deterministic execution.

## Asking for help

The agent can call `show_ui_test_commands`, or a script can contain:

```text
show ui test commands
```

MCP clients that expose prompts can use `create_ui_test_script` to guide the
agent in converting a user request into a runnable script.

The agent can also call `get_app_status` before a test, `wait_for_idle` after
an asynchronous action, and `get_recent_logs` when a test fails. Use
`clear_recent_logs` at the beginning of a test when only new diagnostics matter.

## Script language

Scripts use one command per line. Blank lines and lines beginning with `#` are
ignored. Quotes and JSON are not needed.

```text
start ui test
status
type nameEdit Ada Lovelace
click helloButton
wait idle 250
assert contains messageEdit Hello, Ada Lovelace!
assert no-errors
logs
save screenshot to results/greeting.png
end ui test
```

### Inspection commands

`tree` discovers the current visible Qt UI. QWidget roots and descendants are
returned in `widgets`; standalone QWindow roots are returned in `windows`.
Both use `objectName` as the stable selector.

`status` reports whether the bridge is listening, the port, top-level QWidget
and QWindow counts, visible window count, active window, and captured-log count.

`wait idle [MILLISECONDS]` lets the Qt event loop process queued work for a
bounded interval. It is useful after actions that start asynchronous UI work.

`logs` returns recent Qt diagnostic messages. `clear logs` removes the bounded
in-memory log buffer.

`screenshot [windowName]` captures a QWidget or QWindow. If no name is
supplied, a visible popup is preferred, followed by the first visible
application window.

For a standalone QWindow, capture uses the QWindow's associated screen
surface. The Qt `offscreen` platform can inspect and receive events for a
QWindow but may not provide pixels for a native-surface capture; the bridge
returns a capture error in that case rather than fabricating an image.

`save screenshot to PATH` saves the latest automatically captured PNG buffer.
The path is resolved by the MCP server process. Parent directories must already
exist.

### Interaction commands

```text
click submitButton
activate openAction
type usernameEdit Ada Lovelace
wait 250
wait-text statusLabel Ready
```

`type` consumes the rest of the line, so values may contain spaces. It assigns
widget text directly; it does not simulate individual keyboard events.

Menu and toolbar commands should use `activate ACTION_NAME`. The action's
`objectName` is shown in the `actions` collection of the relevant menu bar,
menu, or toolbar entry in `get_widget_tree`. Activation uses Qt's semantic
`QAction::trigger()` path, so the same action can be tested regardless of
whether the application exposes it through a menu, toolbar, or both.

For event-level input, use:

```text
key usernameEdit Ctrl+A
key usernameEdit Backspace
mouse click submitButton
mouse double-click canvasWidget
mouse move canvasWidget
```

The `key` command sends a key press and release pair. Supported named keys
include `Enter`, `Tab`, `Escape`, `Backspace`, `Delete`, arrows, navigation
keys, function keys, and printable characters. Modifier combinations use `+`,
such as `Ctrl+S` or `Shift+Tab`.

The `mouse` command sends synthetic Qt mouse events to the target QWidget or
QWindow. The default click position is the target center; the direct
`mouse_event` MCP tool also accepts coordinates and mouse-button selection.

### Assertions

```text
assert visible submitButton true
assert enabled submitButton true
assert text statusLabel Ready
assert contains statusLabel Ready
assert popup critical Connection refused
assert no-errors
```

The runner stops on the first failed command by default. Each command also gets
a screenshot automatically, and the final report includes the QWidget tree,
QWindow roots, and visible popup errors.

## Popup errors

`get_popup_errors` detects visible `QMessageBox` instances and reports their
severity, title, main text, informative text, detailed text, and object name.
Custom dialogs that are not `QMessageBox` instances can still be inspected with
`get_widget_tree` and targeted with `take_screenshot`. Qt GUI applications can
target standalone QWindow objects in the same way.

## Naming interactive widgets

Applications should assign unique object names to controls the agent must use:

```cpp
loginButton->setObjectName("loginButton");
usernameEdit->setObjectName("usernameEdit");
statusLabel->setObjectName("statusLabel");
```

The agent should never guess selectors from display text. It should discover
them from `get_widget_tree` first.

## Safety and limitations

- The bridge is intended for Debug/offscreen testing.
- It listens on localhost and should not be exposed remotely.
- Button activation can submit forms, delete data, or close windows.
- Scripts are limited to 100 commands.
- Scripts cannot run arbitrary Python, shell commands, or application code.
- Screenshots are PNG images; automatic screenshots are kept in memory until
  saved or the script finishes.
