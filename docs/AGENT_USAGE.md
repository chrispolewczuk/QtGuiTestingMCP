# Qt GUI MCP agent guide

This document describes how an MCP-enabled agent should use the Qt GUI tools
exposed by `src/qtgui_mcp_server.py`. The bridge supports QWidget hierarchies
and standalone Qt GUI `QWindow` objects.

## Required workflow

1. Call `get_widget_tree` before attempting an interaction.
2. Select widgets by their exact `objectName` values from that tree.
3. Call `get_app_status` and clear logs when starting a diagnostic test.
4. Use `type_text` for `QLineEdit` and `QTextEdit` widgets.
5. Use `click_widget` for buttons and other `QAbstractButton` widgets.
6. Use `activate_action` for menu and toolbar `QAction` objects.
7. Call `wait_for_idle` after actions that start asynchronous UI work.
8. Call `get_popup_errors` and `get_recent_logs` after an action that may fail.
9. Call `get_widget_tree` again when an action is expected to change the UI.
10. Call `take_screenshot` when visual confirmation is needed.

For repeatable workflows, use `run_test_script` with the simple line syntax
below. It executes the same safe operations and returns a result for every step
plus the final QWidget, QWindow, and popup state.

Do not guess object names, rely on widget captions, or assume that a click
succeeded without checking the returned tool result or updated UI state.

## Interaction semantics

`click_widget` performs semantic Qt button activation. It emits the button's
normal signals but does not reproduce physical mouse movement, hover, or
platform-specific pointer behavior.

`activate_action` calls `QAction::trigger()` on a named menu or toolbar action.
It is the preferred way to test a command exposed in more than one place,
because it exercises the action's shared command path without depending on
menu geometry or pointer coordinates.

`type_text` assigns the final text value directly. It does not generate
individual key presses, keyboard shortcuts, focus changes, or key events. If a
test specifically requires keyboard behavior, this bridge currently does not
provide that operation; use `press_key` or the script `key` command instead.

`press_key` sends a real Qt key press/release pair to a QWidget or QWindow. Use names such as `Enter`,
`Tab`, `Escape`, `Backspace`, `Delete`, `Left`, `Right`, `Up`, `Down`, `F1`, or
printable characters. Modifier combinations can be written as `Ctrl+S` or
provided separately to the MCP tool.

`mouse_event` sends Qt mouse move, click, or double-click events to a QWidget or
QWindow. Script syntax uses `mouse click OBJECT_NAME`, `mouse double-click
OBJECT_NAME`, or `mouse move OBJECT_NAME`; events default to the target's center.

`take_screenshot` captures the first visible top-level Qt window. It requires a
visible window even when the application runs with
`QT_QPA_PLATFORM=offscreen`. If a popup is present, it is preferred; pass its
`objectName` to capture it explicitly. Standalone QWindow roots can be
targeted by name.

`get_popup_errors` reports visible `QMessageBox` instances, including critical
errors, warnings, informational messages, titles, detailed text, and object
names. An empty result means that no standard QMessageBox is currently visible;
custom application dialogs may still need to be inspected through
`get_widget_tree`.

## Error handling

Tool failures are returned as MCP errors. Common causes include:

- the Qt application is not running;
- the bridge is not listening on `127.0.0.1:9999`;
- the requested object name does not exist;
- the target is not the expected Qt UI type; or
- no top-level window is visible for a screenshot.

After an error, inspect the widget tree again instead of retrying an unknown
selector repeatedly.

## Safety

The bridge can activate any exposed button, including buttons that delete data,
submit forms, close windows, or change application state. Before invoking a
state-changing action, identify the target from the current widget tree and
confirm that the requested action is within the task scope.

The bridge is a local debug interface. It must run on the same machine as the
Qt application and is intended for debug/offscreen builds, not production use.

## Simple script example

```text
# Greeting workflow
start ui test
status
type nameEdit Ada Lovelace
click helloButton
wait idle 250
assert text messageEdit Hello, Ada Lovelace!
assert no-errors
logs
screenshot mainWindow
save screenshot to artifacts\\after-greeting.png
end ui test
```

Available commands include `start ui test`, `end ui test`, `tree`, `status`,
`wait idle`, `logs`, `clear logs`, `click`, `activate`, `type`, `wait`, `screenshot`,
and `save screenshot to PATH`,
`assert visible`, `assert enabled`, `assert text`, `assert contains`,
`assert popup`, `assert no-errors`, and `wait-text`. Text consumes the rest of
the line, so quotes are not needed. A screenshot is captured automatically
after every command. Saved paths are resolved by the MCP server process; create
the parent directory before running the script. Scripts are limited to 100
steps and do not execute arbitrary code.

For programmatic callers, the previous JSON script format remains supported.

## Startup assumptions

The Qt application must be started separately with the bridge enabled. A typical
headless launch is:

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
./build/qtgui-testing-mcp.exe
```

The MCP server is then registered with the MCP client using:

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
